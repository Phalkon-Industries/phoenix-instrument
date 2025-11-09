"""Execution harness for Phoenix mock BLE workflows."""

from __future__ import annotations

import json
import time
from dataclasses import dataclass, field
from typing import Iterable, List, Optional

from .transport import BleTransport, BleTransportError, create_ble_transport
from .workflows import BleWorkflow, WorkflowStep, load_default_workflows


@dataclass
class StepResultDetail:
    """Outcome for a single workflow step."""

    workflow: str
    step: str
    command: str
    outcome: str
    message: str = ""
    duration_seconds: float = 0.0


@dataclass
class BleTestSummary:
    """Aggregated results for a BLE workflow run."""

    passed: int
    failed: int
    warnings: int
    duration_seconds: float
    details: List[StepResultDetail] = field(default_factory=list)

    def pytest_style_line(self) -> str:
        return (
            f"{self.passed} passed, {self.failed} failed, {self.warnings} warnings "
            f"in {self.duration_seconds:.2f}s"
        )

    @property
    def exit_code(self) -> int:
        return 0 if self.failed == 0 else 1


class MockBleTestRunner:
    """Runs Phoenix mock BLE workflows against a target device."""

    def __init__(
        self,
        *,
        transport: Optional[BleTransport] = None,
        device_name: str = "Phoenix Mock",
        connect_timeout: float = 15.0,
        response_timeout: float = 5.0,
        workflows: Optional[Iterable[BleWorkflow]] = None,
    ) -> None:
        self._transport = transport or create_ble_transport()
        self._device_name = device_name
        self._connect_timeout = connect_timeout
        self._response_timeout = response_timeout
        self._workflows = list(workflows) if workflows is not None else load_default_workflows()

    @property
    def transport(self) -> BleTransport:
        return self._transport

    def run_all(self) -> BleTestSummary:
        start_time = time.monotonic()
        details: List[StepResultDetail] = []
        passed = failed = warnings = 0

        try:
            self.transport.connect(self._device_name, self._connect_timeout)
        except BleTransportError as error:
            duration = time.monotonic() - start_time
            details.append(
                StepResultDetail(
                    workflow="connection",
                    step="connect",
                    command="n/a",
                    outcome="failed",
                    message=str(error),
                    duration_seconds=duration,
                )
            )
            return BleTestSummary(passed=0, failed=1, warnings=0, duration_seconds=duration, details=details)

        try:
            for workflow in self._workflows:
                for step in workflow.steps:
                    step_start = time.monotonic()
                    try:
                        request_body = step.build_request_body()
                        payload_text = json.dumps(request_body)
                        response_text = self.transport.exchange(payload_text, step.response_timeout)
                        detail, outcome = self._evaluate_step(workflow, step, response_text, time.monotonic() - step_start)
                    except BleTransportError as error:
                        detail = StepResultDetail(
                            workflow=workflow.name,
                            step=step.name,
                            command=step.command,
                            outcome="warning",
                            message=str(error),
                            duration_seconds=time.monotonic() - step_start,
                        )
                        outcome = "warning"
                    except Exception as error:  # pragma: no cover - defensive path
                        detail = StepResultDetail(
                            workflow=workflow.name,
                            step=step.name,
                            command=step.command,
                            outcome="failed",
                            message=str(error),
                            duration_seconds=time.monotonic() - step_start,
                        )
                        outcome = "failed"

                    details.append(detail)

                    if outcome == "passed":
                        passed += 1
                    elif outcome == "failed":
                        failed += 1
                    else:
                        warnings += 1
        finally:
            self.transport.disconnect()

        duration = time.monotonic() - start_time
        return BleTestSummary(
            passed=passed,
            failed=failed,
            warnings=warnings,
            duration_seconds=duration,
            details=details,
        )

    def _evaluate_step(
        self,
        workflow: BleWorkflow,
        step: WorkflowStep,
        response_text: str,
        duration_seconds: float,
    ) -> tuple[StepResultDetail, str]:
        try:
            decoded = json.loads(response_text)
        except json.JSONDecodeError as error:
            message = f"Invalid JSON response: {error}"
            return StepResultDetail(workflow.name, step.name, step.command, "failed", message, duration_seconds), "failed"

        command = decoded.get("command")
        if command != step.command:
            message = f"Expected command '{step.command}', received '{command}'"
            return StepResultDetail(workflow.name, step.name, step.command, "failed", message, duration_seconds), "failed"

        parameters = decoded.get("parameters")
        if not isinstance(parameters, dict):
            message = "Response missing 'parameters' object"
            return StepResultDetail(workflow.name, step.name, step.command, "failed", message, duration_seconds), "failed"

        if not self._matches_expected(step.expected_payload, parameters):
            message = f"Payload mismatch: expected subset {step.expected_payload}, received {parameters}"
            return StepResultDetail(workflow.name, step.name, step.command, "failed", message, duration_seconds), "failed"

        return StepResultDetail(workflow.name, step.name, step.command, "passed", duration_seconds=duration_seconds), "passed"

    def _matches_expected(self, expected: dict, actual: dict) -> bool:
        for key, expected_value in expected.items():
            if key not in actual:
                return False
            actual_value = actual[key]
            if isinstance(expected_value, dict):
                if not isinstance(actual_value, dict):
                    return False
                if not self._matches_expected(expected_value, actual_value):
                    return False
            else:
                if actual_value != expected_value:
                    return False
        return True


def build_runner(**kwargs) -> MockBleTestRunner:
    """Factory used by the CLI and tests to construct a runner."""

    return MockBleTestRunner(**kwargs)

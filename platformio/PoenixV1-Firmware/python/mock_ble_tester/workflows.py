"""Workflow definitions for the Phoenix mock BLE tester."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List, Optional


@dataclass(frozen=True)
class WorkflowStep:
    """Represents a single BLE command/response expectation."""

    name: str
    command: str
    expected_payload: Dict[str, object]
    request_payload: Optional[Dict[str, object]] = None
    response_timeout: float = 5.0

    def build_request_body(self) -> Dict[str, object]:
        """Return the JSON payload that should be written to the command characteristic."""

        if self.request_payload is not None:
            return self.request_payload
        return {"command": self.command}


@dataclass(frozen=True)
class BleWorkflow:
    """Collection of ordered workflow steps that form an integration scenario."""

    name: str
    steps: List[WorkflowStep]


def load_default_workflows() -> List[BleWorkflow]:
    """Return the canonical Phoenix mock workflows covering happy-path and guard rails."""

    reference_step = WorkflowStep(
        name="reference_baseline",
        command="reference_start",
        expected_payload={
            "status": "ok",
            "sequence": 1,
            "dark_counts": 195000,
            "signal_counts": 210000,
            "absorbance": 0.15,
            "temperature_c": 25.0,
            "salinity_ppt": 35.0,
        },
    )

    sample_without_reference = WorkflowStep(
        name="sample_without_reference",
        command="sample_start",
        expected_payload={
            "status": "not_ready",
        },
    )

    sample_with_reference = WorkflowStep(
        name="sample_after_reference",
        command="sample_start",
        expected_payload={
            "status": "ok",
            "sequence": 1,
            "dark_counts": 196250,
            "signal_counts": 224500,
            "absorbance": 0.2825,
            "temperature_c": 25.5,
            "salinity_ppt": 35.2,
            "ph_value": 7.42,
        },
    )

    settings_get_default = WorkflowStep(
        name="settings_get_default",
        command="settings_get",
        expected_payload={
            "status": "ok",
            "snapshot": {
                "temperature_c": 25.0,
                "salinity_ppt": 35.0,
                "interval_ms": 120000,
                "alerts_enabled": True,
                "configuration_hash": "ABCD1234",
            },
        },
    )

    settings_update_step = WorkflowStep(
        name="settings_update",
        command="settings_update",
        request_payload={
            "command": "settings_update",
            "temperature_c": 24.5,
            "salinity_ppt": 34.7,
            "interval_ms": 90000,
            "alerts_enabled": False,
        },
        expected_payload={
            "status": "ok",
            "applied": {
                "temperature_c": 24.5,
                "salinity_ppt": 34.7,
                "interval_ms": 90000,
                "alerts_enabled": False,
                "configuration_hash": "FACEB00C",
            },
        },
    )

    settings_get_after_update = WorkflowStep(
        name="settings_get_after_update",
        command="settings_get",
        expected_payload={
            "status": "ok",
            "snapshot": {
                "temperature_c": 24.5,
                "salinity_ppt": 34.7,
                "interval_ms": 90000,
                "alerts_enabled": False,
                "configuration_hash": "FACEB00C",
            },
        },
    )

    battery_status_step = WorkflowStep(
        name="battery_status",
        command="battery_status",
        expected_payload={
            "status": "ok",
            "percentage": 78,
            "voltage_v": 3.92,
            "is_low": False,
            "response_delay_ms": 200,
        },
    )

    alert_inject_step = WorkflowStep(
        name="alert_inject",
        command="alert_inject",
        expected_payload={
            "status": "ok",
            "alert_code": "0004",
            "severity": 1,
            "message": "Mock sensor fault detected.",
        },
    )

    session_teardown_step = WorkflowStep(
        name="session_teardown",
        command="session_teardown",
        expected_payload={
            "status": "ok",
            "summary": {
                "completed_references": 1,
                "completed_samples": 1,
                "uptime_ms": 600000,
                "ble_connected": False,
            },
        },
    )

    return [
        BleWorkflow(name="sample_precondition", steps=[sample_without_reference]),
        BleWorkflow(name="reference_measurement", steps=[reference_step]),
        BleWorkflow(name="sample_measurement", steps=[sample_with_reference]),
        BleWorkflow(name="settings_defaults", steps=[settings_get_default]),
        BleWorkflow(name="settings_update", steps=[settings_update_step, settings_get_after_update]),
        BleWorkflow(name="battery_status", steps=[battery_status_step]),
        BleWorkflow(name="alert_inject", steps=[alert_inject_step]),
        BleWorkflow(name="session_teardown", steps=[session_teardown_step]),
    ]

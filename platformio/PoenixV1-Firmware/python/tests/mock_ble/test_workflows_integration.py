import pytest

from mock_ble_tester.workflows import load_default_workflows


_WORKFLOW_NAMES = [workflow.name for workflow in load_default_workflows()]


@pytest.mark.mock_ble
@pytest.mark.parametrize("workflow_name", _WORKFLOW_NAMES)
def test_mock_ble_workflow_passes(workflow_name, mock_ble_summary):
    matching_details = [detail for detail in mock_ble_summary.details if detail.workflow == workflow_name]
    assert matching_details, f"Workflow '{workflow_name}' did not produce any step results"
    assert all(detail.outcome == "passed" for detail in matching_details), (
        f"Workflow '{workflow_name}' had failing or warning steps: "
        + ", ".join(f"{detail.step}:{detail.outcome}" for detail in matching_details if detail.outcome != "passed")
    )

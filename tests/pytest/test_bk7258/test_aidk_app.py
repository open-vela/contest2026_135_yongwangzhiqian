#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""AIDK normal-product HIL over the official OpenVela UART0 fixture."""

import os

import pytest

from .aidk_hil_contract import (
    ACTIVE_CASES,
    ACTIVE_ENV,
    AIDK_BOARD,
    REQUIRED_COMMANDS,
    SAFE_CASES,
)


def _run_case(p, case):
    for repetition in range(case.repetitions):
        for step in case.steps:
            result = p.sendCommand(
                step.command,
                *step.expected_regex,
                p.PROMPT,
                timeout=step.timeout_sec,
            )
            assert result == 0, (
                f"{case.case_id} failed at repetition {repetition + 1}/"
                f"{case.repetitions}: {step.command}"
            )


@pytest.fixture(scope="module", autouse=True)
def require_aidk_product_profile(p):
    if p.board != AIDK_BOARD:
        pytest.skip("AIDK product HIL applies only to aidk_ai_toy")

    for command in REQUIRED_COMMANDS:
        result = p.sendCommand("help", command, p.PROMPT, timeout=20)
        assert result == 0, (
            "AIDK product HIL requires the normal app image; "
            f"missing command: {command}"
        )


@pytest.mark.parametrize("case", SAFE_CASES, ids=lambda case: case.case_id)
def test_aidk_functional_hil(p, record_property, case):
    record_property("physical_gate", case.physical_gate)
    record_property("active_device_action", "no")
    record_property("repetitions", str(case.repetitions))
    _run_case(p, case)
    record_property("evidence_level", "FUNCTION_PASS")


@pytest.mark.parametrize("case", ACTIVE_CASES, ids=lambda case: case.case_id)
def test_aidk_active_hil(p, record_property, case):
    if os.environ.get(ACTIVE_ENV) != "1":
        pytest.skip(f"set {ACTIVE_ENV}=1 with an operator present")

    record_property("physical_gate", case.physical_gate)
    record_property("active_device_action", "yes")
    record_property("repetitions", str(case.repetitions))
    _run_case(p, case)
    record_property("evidence_level", "FUNCTION_PASS")

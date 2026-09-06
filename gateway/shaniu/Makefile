# SPDX-License-Identifier: Apache-2.0

PYTHON ?= python3

.PHONY: test
test:
	PYTHONPATH=. $(PYTHON) -m unittest discover -s tests -v
	@echo SHANIU_GATEWAY_TEST_PASS

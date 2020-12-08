# Copyright (c) 2020, XMOS Ltd, All rights reserved

import pytest

from tflite2xcore.xcore_schema import ExternalOpCodes, XCOREOpCodes  # type: ignore # TODO: fix this
from tflite2xcore.model_generation import Configuration

from . import (
    BinarizedTestRunner,
    BConv2dGenericTestModelGenerator,
    LarqConverter,
)

from . import (  # pylint: disable=unused-import
    test_reference_model_regression,
    test_converted_single_op_model,
    test_output,
)


#  ----------------------------------------------------------------------------
#                                   GENERATORS
#  ----------------------------------------------------------------------------


class BConv2dInt8TestModelGenerator(BConv2dGenericTestModelGenerator):
    def _set_config(self, cfg: Configuration) -> None:
        cfg.setdefault("padding", "valid")
        super()._set_config(cfg)


GENERATOR = BConv2dInt8TestModelGenerator

#  ----------------------------------------------------------------------------
#                                   RUNNERS
#  ----------------------------------------------------------------------------


class BConv2dInt8TestRunner(BinarizedTestRunner):
    def make_lce_converter(self) -> LarqConverter:
        return LarqConverter(self, self.get_built_model, strip=True)


RUNNER = BConv2dInt8TestRunner

#  ----------------------------------------------------------------------------
#                                   FIXTURES
#  ----------------------------------------------------------------------------


@pytest.fixture  # type: ignore
def bitpacked_outputs() -> bool:
    return False


@pytest.fixture  # type: ignore
def reference_op_code() -> ExternalOpCodes:
    return ExternalOpCodes.LceBconv2d


@pytest.fixture  # type: ignore
def converted_op_code() -> XCOREOpCodes:
    return XCOREOpCodes.XC_bconv2d_int8


if __name__ == "__main__":
    pytest.main()

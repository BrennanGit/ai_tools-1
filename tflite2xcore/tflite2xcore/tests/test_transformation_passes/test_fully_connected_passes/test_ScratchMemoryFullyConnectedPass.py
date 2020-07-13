# Copyright (c) 2020, XMOS Ltd, All rights reserved

import pytest

from copy import deepcopy

from tflite2xcore.transformation_passes import ScratchMemoryFullyConnectedPass

from tflite2xcore.tests.test_transformation_passes.model_builders import (
    build_XC_fc_deepin_anyout,
)

from .conftest import PARAMS


#  ----------------------------------------------------------------------------
#                                   FIXTURES
#  ----------------------------------------------------------------------------


@pytest.fixture()
def trf_pass():
    return ScratchMemoryFullyConnectedPass()


@pytest.fixture()
def model(outputs, input_channels):
    return build_XC_fc_deepin_anyout(outputs=outputs, input_channels=input_channels)


#  ----------------------------------------------------------------------------
#                               TEST FUNCTIONS
#  ----------------------------------------------------------------------------


def test_matching(trf_pass, model):
    assert trf_pass.match(model.subgraphs[0].operators[-1])


def test_mutate(trf_pass, model):
    op = model.subgraphs[0].operators[0]
    assert "mem" not in op.custom_options
    trf_pass.run(model)
    model.sanity_check()
    assert "mem" in op.custom_options


if __name__ == "__main__":
    pytest.main()
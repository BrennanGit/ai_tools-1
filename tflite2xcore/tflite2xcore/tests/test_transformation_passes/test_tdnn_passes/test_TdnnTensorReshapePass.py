# Copyright 2021 XMOS LIMITED. This Software is subject to the terms of the
# XMOS Public License: Version 1

import pytest
from typing import Tuple
import numpy as np


from tflite2xcore.transformation_passes.tdnn_passes import (
    TdnnReshapePass,
    TdnnTensorPass,
)
from tflite2xcore.xcore_model import XCOREModel


from tflite2xcore.tests.test_transformation_passes.model_builders import (
    build_reshape,
    ModelBuilder,
)

PARAMS = {"default": {"input_height": [6], "input_width": [9], "input_channels": [4],}}


#  ----------------------------------------------------------------------------
#                                   FIXTURES
#  ----------------------------------------------------------------------------
@pytest.fixture()
def input_size(input_height: int, input_width: int) -> Tuple[int, int]:
    return (input_height, input_width)


@pytest.fixture()
def input_shape(
    input_size: Tuple[int, int], input_channels: int
) -> Tuple[int, int, int]:
    return (*input_size, input_channels)


@pytest.fixture()
def build_model() -> ModelBuilder:
    return build_reshape


@pytest.fixture()
def trf_pass() -> TdnnReshapePass:
    return TdnnReshapePass()


@pytest.fixture()
def tensor_pass() -> TdnnTensorPass:
    return TdnnTensorPass()


@pytest.fixture()
def model(build_model: ModelBuilder, input_shape: Tuple[int, int, int]) -> XCOREModel:
    return build_model(input_shape=input_shape, output_shape=(np.prod(input_shape),))


#  ----------------------------------------------------------------------------
#                                   TESTS
#  ----------------------------------------------------------------------------


def test_tdnn_mutate(
    trf_pass: TdnnReshapePass, model: XCOREModel, tensor_pass: TdnnTensorPass
) -> None:
    # run replacement pass
    trf_pass.run(model)
    model.sanity_check()
    tensor_pass.run(model)
    model.sanity_check()

    # check operators
    subgraph = model.subgraphs[0]
    operators = subgraph.operators
    assert len(operators) == 2

    # check tensors
    op = operators[0]  # reshape op
    assert len(op.inputs) == 1
    assert len(op.outputs) == 1

    op = operators[1]  # ring buffer op
    assert len(op.inputs) == 2
    assert len(op.outputs) == 1

    # check wiring
    assert len(subgraph.get_tensor("original_shape").consumers) == 1

    

if __name__ == "__main__":
    pytest.main()
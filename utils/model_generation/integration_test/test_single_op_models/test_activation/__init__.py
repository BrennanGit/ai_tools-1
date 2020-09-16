# Copyright (c) 2020, XMOS Ltd, All rights reserved

import tensorflow as tf  # type: ignore
import _pytest  # type: ignore # NOTE: for typing only
from abc import abstractmethod
from typing import Callable, Optional, Tuple

from .. import (
    IntegrationTestRunner,
    ChannelAgnosticOpTestModelGenerator,
    _test_output,
    test_converted_single_op_model,
    test_idempotence,
)


#  ----------------------------------------------------------------------------
#                                   GENERATORS
#  ----------------------------------------------------------------------------


class LUTActivationOpTestModelGenerator(ChannelAgnosticOpTestModelGenerator):
    @property
    @abstractmethod
    def act_fun(self) -> Callable[[tf.Tensor], tf.Tensor]:
        raise NotImplementedError()

    def _op_layer(
        self, *, input_shape: Optional[Tuple[int, int, int]] = None
    ) -> tf.keras.layers.Layer:
        kwargs = {"input_shape": input_shape} if input_shape else {}
        return tf.keras.layers.Lambda(self.act_fun, **kwargs)


#  ----------------------------------------------------------------------------
#                                   TESTS
#  ----------------------------------------------------------------------------


def test_output(
    run: IntegrationTestRunner, request: _pytest.fixtures.SubRequest
) -> None:
    _test_output(run.outputs, request, tolerance=0)
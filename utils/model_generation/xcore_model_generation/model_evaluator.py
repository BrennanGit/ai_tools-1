# Copyright (c) 2020, XMOS Ltd, All rights reserved

from abc import ABC, abstractmethod
import tensorflow as tf  # type: ignore
import numpy as np  # type: ignore

from typing import Callable, Union

from tflite2xcore.xcore_interpreter import XCOREInterpreter  # type: ignore # TODO: fix this

from .model_converter import TFLiteModel
from .utils import apply_interpreter_to_examples, quantize, Quantization


class ModelEvaluator(ABC):
    input_data: np.ndarray
    output_data: np.ndarray

    def __init__(
        self, input_data_hook: Callable[[], Union[tf.Tensor, np.ndarray]]
    ) -> None:
        self._input_data_hook = input_data_hook

    @abstractmethod
    def evaluate(self) -> None:
        raise NotImplementedError()


class TFLiteEvaluator(ModelEvaluator):
    _interpreter: tf.lite.Interpreter

    def __init__(
        self,
        input_data_hook: Callable[[], Union[tf.Tensor, np.ndarray]],
        model_hook: Callable[[], TFLiteModel],
    ) -> None:
        super().__init__(input_data_hook)
        self._model_hook = model_hook

    def evaluate(self) -> None:
        self._interpreter = tf.lite.Interpreter(model_content=self._model_hook())
        self._interpreter.allocate_tensors()

        self.input_data = np.array(self._input_data_hook())
        self.output_data = apply_interpreter_to_examples(
            self._interpreter, self.input_data
        )


class TFLiteQuantEvaluator(TFLiteEvaluator):
    def __init__(
        self,
        input_data_hook: Callable[[], Union[tf.Tensor, np.ndarray]],
        model_hook: Callable[[], TFLiteModel],
        input_quant_hook: Callable[[], Quantization],
        output_quant_hook: Callable[[], Quantization],
    ) -> None:
        super().__init__(input_data_hook, model_hook)
        self._input_quant_hook = input_quant_hook
        self._output_quant_hook = output_quant_hook

    @property
    def input_data_quant(self) -> np.ndarray:
        return quantize(self.input_data, *self._input_quant_hook())

    @property
    def output_data_quant(self) -> np.ndarray:
        return quantize(self.output_data, *self._output_quant_hook())


class XCoreEvaluator(TFLiteEvaluator):
    input_quant: Quantization
    output_quant: Quantization

    def evaluate(self) -> None:
        self._interpreter = XCOREInterpreter(model_content=self._model_hook())
        self._interpreter.allocate_tensors()

        self.input_quant = Quantization(
            *self._interpreter.get_input_details()[0]["quantization"]
        )
        self.output_quant = Quantization(
            *self._interpreter.get_output_details()[0]["quantization"]
        )

        self.input_data_float = np.array(self._input_data_hook())
        self.input_data = quantize(self.input_data_float, *self.input_quant)

        self.output_data = apply_interpreter_to_examples(
            self._interpreter, self.input_data
        )

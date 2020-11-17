# Copyright (c) 2020, XMOS Ltd, All rights reserved

from typing import Iterable

from tflite2xcore.xcore_model import Operator, Subgraph
from tflite2xcore.xcore_schema import (
    BuiltinOpCodes,
    OperatorCode,
    XCOREOpCodes,
)

from .transformation_passes import ReplaceQuantizedOperatorPass


class ReplaceAddPass(ReplaceQuantizedOperatorPass):
    @property
    def matching_opcode(self) -> BuiltinOpCodes:
        return BuiltinOpCodes.ADD

    @property
    def new_opcode(self) -> OperatorCode:
        return OperatorCode(XCOREOpCodes.XC_add_8)

    def match(self, op: Operator) -> bool:
        return (
            super().match(op)
            and len(op.inputs) == 2
            and op.inputs[0].type is self.matching_input_type
            and op.inputs[0].type is op.inputs[1].type is op.outputs[0].type
            and op.inputs[0].shape == op.inputs[1].shape == op.outputs[0].shape
        )
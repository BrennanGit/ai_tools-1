#!/usr/bin/env python
#
# Copyright (c) 2018-2019, XMOS Ltd, All rights reserved
from pathlib import Path
from tflite2xcore.model_generation import utils
import tensorflow as tf
import op_test_models_common as common

DEFAULT_INPUTS = 3
DEFAULT_OUTPUTS = 16
DEFAULT_HEIGHT = 5
DEFAULT_WIDTH = DEFAULT_HEIGHT
DEFAULT_KERNEL_HEIGHT = 3
DEFAULT_KERNEL_WIDTH = DEFAULT_KERNEL_HEIGHT
DEFAULT_PADDING = 'same'
DEFAULT_PATH = Path(__file__).parent.joinpath(
    'debug', 'conv2d_shallowin_deepout_relu').resolve()


class Conv2dShallowinDeepoutRelu(common.OpTestDefaultConvModel):
    def build_core_model(self, *args, **kwargs):
        K_w, input_channels = args[1], args[4]
        assert input_channels <= 4, "Number of input channels must be at most 4"
        assert K_w <= 8, "Kernel width must be at most 8"
        super().build_core_model(*args, **kwargs)


def main(path=DEFAULT_PATH, *,
         input_channels=DEFAULT_INPUTS,
         output_channels=DEFAULT_OUTPUTS,
         height=DEFAULT_HEIGHT,
         width=DEFAULT_WIDTH,
         K_h=DEFAULT_KERNEL_HEIGHT,
         K_w=DEFAULT_KERNEL_WIDTH,
         padding=DEFAULT_PADDING,
         inits=common.DEFAULT_INITS):
    kwargs = {
        'name': 'conv2d_shallowin_deepout_relu',
        'path': path if path else DEFAULT_PATH
    }
    model = Conv2dShallowinDeepoutRelu(**kwargs)
    model.run(num_threads=None,
              input_channels=input_channels,
              output_channels=output_channels,
              height=height,
              width=width,
              K_h=K_h,
              K_w=K_w,
              padding=padding,
              inits=inits)


if __name__ == "__main__":
    parser = common.OpTestConvParser(defaults={
        'path': DEFAULT_PATH,
        'inputs': DEFAULT_INPUTS,
        'outputs': DEFAULT_OUTPUTS,
        'width': DEFAULT_WIDTH,
        'height': DEFAULT_HEIGHT,
        'padding': DEFAULT_PADDING,
        'kernel_width': DEFAULT_KERNEL_WIDTH,
        'kernel_height': DEFAULT_KERNEL_HEIGHT,
        'inits': {
            'input_init': common.OpTestInitializers.UNIF,
            'bias_init': common.OpTestInitializers.CONST,
            'weight_init': common.OpTestInitializers.UNIF}
    })
    args = parser.parse_args()
    utils.set_gpu_usage(False, args.verbose)

    main(path=args.path,
         input_channels=args.inputs,
         output_channels=args.outputs,
         K_h=args.kernel_height,
         K_w=args.kernel_width,
         height=args.height,
         width=args.width,
         padding=args.padding,
         inits=args.inits)

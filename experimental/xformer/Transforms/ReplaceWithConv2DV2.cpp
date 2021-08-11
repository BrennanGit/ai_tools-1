// Copyright 2021 XMOS LIMITED. This Software is subject to the terms of the
// XMOS Public License: Version 1

#include "IR/XCoreOps.h"

#include "lib_nn/api/Conv2d.hpp"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "tensorflow/compiler/mlir/lite/ir/tfl_ops.h"
#include "tensorflow/core/framework/kernel_shape_util.h"
#include <numeric>

namespace mlir {
namespace xcore {

namespace {
// Replace TFL Conv2D with XC Conv2DV2 ops.
struct ReplaceWithConv2DV2
    : public PassWrapper<ReplaceWithConv2DV2, FunctionPass> {
  void getDependentDialects(DialectRegistry &registry) const final {
    registry.insert<TFL::TensorFlowLiteDialect>();
    registry.insert<XCoreDialect>();
  }
  void runOnFunction() override;
};

struct Conv2DArgs {
  int outputHeight, outputWidth, outputDepth, outputZeroPoint;
  int inputHeight, inputWidth, inputDepth, inputZeroPoint;
  int filterHeight, filterWidth, filterDepth;
  int strideH, strideW;
  int dilationH, dilationW;
  std::vector<int8_t> filter;
  std::vector<int32_t> bias;
  std::vector<float> effectiveMultiplier;
  int16_t topPad, leftPad, bottomPad, rightPad;
  int8_t padValue;
};

struct ReplaceWithConv2DV2Pattern : public OpRewritePattern<TFL::Conv2DOp> {
  using OpRewritePattern<TFL::Conv2DOp>::OpRewritePattern;

  //
  llvm::SmallVector<std::string>
  getConv2DPaddedIndirectParams(const Conv2DArgs &args) const {
    llvm::SmallVector<std::string> conv2DParams;

    nn::padding_t padding = {args.topPad, args.leftPad, args.bottomPad,
                             args.rightPad};
    nn::ImageGeometry Y(args.outputHeight, args.outputWidth, args.outputDepth);
    nn::ImageGeometry X(args.inputHeight, args.inputWidth, args.inputDepth);
    nn::WindowGeometry K(args.filterHeight, args.filterWidth, args.filterDepth,
                         -padding.top, -padding.left, args.strideH,
                         args.strideW, 1, args.dilationH, args.dilationW);
    nn::Filter2dGeometry geom(X, Y, K);

    nn::ImToColPadded::Params imToColParams(X, K, padding, args.inputDepth,
                                            args.inputZeroPoint);

    int inputBytes = args.filterHeight * args.filterWidth * args.inputDepth;
    std::array<int, 4> shape = {args.outputDepth, args.filterHeight,
                                args.filterWidth, args.inputDepth};
    nn::Conv2dReorderedWeights rw = nn::MatMulInt8::reorder_kernel_weights(
        (int8_t *)args.filter.data(), shape, 8, args.padValue);
    nn::MatMulInt8::Params afParams(args.outputDepth, inputBytes,
                                    rw.weights.data());

    nn::OutputTransformFnInt8::CanonicalMulAndBias canonical_values =
        nn::OutputTransformFnInt8::canonicalise_mul_and_bias(
            args.effectiveMultiplier, args.bias, args.filter,
            args.inputZeroPoint, args.outputZeroPoint, args.outputDepth);
    nn::QuantisationParams qp = nn::OutputTransformFnInt8::quantise_activation(
        canonical_values.f_multipliers, canonical_values.f_biases,
        canonical_values.accu_min, canonical_values.accu_max);
    nn::OT_int8::Params otParams((int32_t)args.outputDepth, &qp.otv, qp.biases,
                                 qp.multipliers);

    auto ir = nn::ImageRegion(0, 0, 0, Y.height, Y.width, Y.depth);
    nn::Filter2D::Params akParams(Y, ir, VPU_INT8_ACC_PERIOD);

    // TODO: Check serialization
    std::string akpStr = akParams.serialise<nn::Filter2D::Params>();
    std::string mfStr = imToColParams.serialise<nn::ImToColPadded::Params>();
    std::string afStr = afParams.serialise<nn::MatMulInt8::Params>();
    std::string otStr = otParams.serialise<nn::OT_int8::Params>();

    conv2DParams.push_back(akpStr);
    conv2DParams.push_back(mfStr);
    conv2DParams.push_back(afStr);
    conv2DParams.push_back(otStr);

    return conv2DParams;
  }

  //
  llvm::SmallVector<std::string>
  getConv2DValidIndirectParams(const Conv2DArgs &args) const {
    llvm::SmallVector<std::string> conv2DParams;

    nn::padding_t padding = {args.topPad, args.leftPad, args.bottomPad,
                             args.rightPad};
    nn::ImageGeometry Y(args.outputHeight, args.outputWidth, args.outputDepth);
    nn::ImageGeometry X(args.inputHeight, args.inputWidth, args.inputDepth);
    nn::WindowGeometry K(args.filterHeight, args.filterWidth, args.filterDepth,
                         -padding.top, -padding.left, args.strideH,
                         args.strideW, 1, args.dilationH, args.dilationW);
    nn::Filter2dGeometry geom(X, Y, K);

    nn::ImToColValid::Params imToColParams(X, K, args.inputDepth);

    // TODO: What is overread bytes for?
    // int overread_bytes = memcpy.get_overread_bytes();
    // input.resize(input.size() + overread_bytes / sizeof(int8_t), 0);

    std::array<int, 4> shape = {args.outputDepth, args.filterHeight,
                                args.filterWidth, args.inputDepth};
    nn::Conv2dReorderedWeights rw = nn::MatMulInt8::reorder_kernel_weights(
        (int8_t *)args.filter.data(), shape, 8, args.padValue);
    int inputBytes = args.filterHeight * args.filterWidth * args.inputDepth;
    nn::MatMulInt8::Params afParams(args.outputDepth, inputBytes,
                                    rw.weights.data());

    nn::OutputTransformFnInt8::CanonicalMulAndBias canonicalValues =
        nn::OutputTransformFnInt8::canonicalise_mul_and_bias(
            args.effectiveMultiplier, args.bias, args.filter,
            args.inputZeroPoint, args.outputZeroPoint, args.outputDepth);
    nn::QuantisationParams qp = nn::OutputTransformFnInt8::quantise_activation(
        canonicalValues.f_multipliers, canonicalValues.f_biases,
        canonicalValues.accu_min, canonicalValues.accu_max);
    nn::OT_int8::Params otParams((int32_t)args.outputDepth, &qp.otv, qp.biases,
                                 qp.multipliers);

    auto ir = nn::ImageRegion(0, 0, 0, Y.height, Y.width, Y.depth);
    nn::Filter2D::Params akParams(Y, ir, VPU_INT8_ACC_PERIOD);

    // TODO: Check serialization
    std::string akpStr = akParams.serialise<nn::Filter2D::Params>();
    std::string mfStr = imToColParams.serialise<nn::ImToColValid::Params>();
    std::string afStr = afParams.serialise<nn::MatMulInt8::Params>();
    std::string otStr = otParams.serialise<nn::OT_int8::Params>();

    conv2DParams.push_back(akpStr);
    conv2DParams.push_back(mfStr);
    conv2DParams.push_back(afStr);
    conv2DParams.push_back(otStr);

    return conv2DParams;
  }

  //
  llvm::SmallVector<std::string>
  getConv2DValidDirectParams(const Conv2DArgs &args) const {
    llvm::SmallVector<std::string> conv2DParams;

    nn::padding_t padding = {args.topPad, args.leftPad, args.bottomPad,
                             args.rightPad};
    nn::ImageGeometry X(args.inputHeight, args.inputWidth, args.inputDepth);
    nn::ImageGeometry Y(args.outputHeight, args.outputWidth, args.outputDepth);
    nn::WindowGeometry K(args.filterHeight, args.filterWidth, args.filterDepth,
                         -padding.top, -padding.left, args.strideH,
                         args.strideW, 1, args.dilationH, args.dilationW);

    nn::DerefInputFn::Params imToColParams(X, K);

    std::array<int, 4> shape = {args.outputDepth, args.filterHeight,
                                args.filterWidth, args.inputDepth};
    nn::Conv2dReorderedWeights rw = nn::MatMulInt8::reorder_kernel_weights(
        (int8_t *)args.filter.data(), shape, 8, args.padValue);
    nn::MatMulDirectFn::Params afParams(
        X, K, args.inputDepth, rw.weights.data(), (int)rw.weights.size());

    llvm::dbgs() << "      \n\n      reordered weights \n";
    for (int i = 0; i < 16; i++) {
      llvm::dbgs() << (int)rw.weights[i] << " ";
    }

    nn::OutputTransformFnInt8::CanonicalMulAndBias canonicalValues =
        nn::OutputTransformFnInt8::canonicalise_mul_and_bias(
            args.effectiveMultiplier, args.bias, args.filter,
            args.inputZeroPoint, args.outputZeroPoint, args.outputDepth);
    nn::QuantisationParams qp = nn::OutputTransformFnInt8::quantise_activation(
        canonicalValues.f_multipliers, canonicalValues.f_biases,
        canonicalValues.accu_min, canonicalValues.accu_max);
    nn::OT_int8::Params otParams((int32_t)args.outputDepth, &qp.otv, qp.biases,
                                 qp.multipliers);

    auto ir = nn::ImageRegion(0, 0, 0, Y.height, Y.width, Y.depth);
    nn::Filter2D::Params akParams(Y, ir, VPU_INT8_ACC_PERIOD);

    // TODO: Check serialization
    std::string akpStr = akParams.serialise<nn::Filter2D::Params>();
    std::string mfStr = imToColParams.serialise<nn::DerefInputFn::Params>();
    std::string afStr = afParams.serialise<nn::MatMulDirectFn::Params>();
    std::string otStr = otParams.serialise<nn::OT_int8::Params>();

    conv2DParams.push_back(akpStr);
    conv2DParams.push_back(mfStr);
    conv2DParams.push_back(afStr);
    conv2DParams.push_back(otStr);

    return conv2DParams;
  }

  LogicalResult matchAndRewrite(TFL::Conv2DOp conv2DOp,
                                PatternRewriter &rewriter) const override {
    // Check for invalid types and return
    // Input type must be QI8
    if (!(conv2DOp.input()
              .getType()
              .cast<ShapedType>()
              .getElementType()
              .isa<quant::QuantizedType>() &&
          conv2DOp.input()
              .getType()
              .cast<ShapedType>()
              .getElementType()
              .cast<quant::QuantizedType>()
              .isSigned() &&
          conv2DOp.input()
                  .getType()
                  .cast<ShapedType>()
                  .getElementType()
                  .cast<quant::QuantizedType>()
                  .getStorageTypeIntegralWidth() == 8)) {
      return failure();
    }

    // Filter type must be QI8
    if (!(conv2DOp.filter()
              .getType()
              .cast<ShapedType>()
              .getElementType()
              .isa<quant::QuantizedType>() &&
          conv2DOp.filter()
              .getType()
              .cast<ShapedType>()
              .getElementType()
              .cast<quant::QuantizedType>()
              .isSigned() &&
          conv2DOp.filter()
                  .getType()
                  .cast<ShapedType>()
                  .getElementType()
                  .cast<quant::QuantizedType>()
                  .getStorageTypeIntegralWidth() == 8)) {
      return failure();
    }

    // TODO: What to do if no bias?
    // Bias type must be QI32
    if (!(conv2DOp.bias()
              .getType()
              .cast<ShapedType>()
              .getElementType()
              .isa<quant::QuantizedType>() &&
          conv2DOp.bias()
              .getType()
              .cast<ShapedType>()
              .getElementType()
              .cast<quant::QuantizedType>()
              .isSigned() &&
          conv2DOp.bias()
                  .getType()
                  .cast<ShapedType>()
                  .getElementType()
                  .cast<quant::QuantizedType>()
                  .getStorageTypeIntegralWidth() == 32)) {
      return failure();
    }

    // Output depth must be a multiple of four
    // If this is not the case, we return to the reference
    // implementation
    auto outputDepth =
        conv2DOp.output().getType().cast<ShapedType>().getDimSize(3);
    if (outputDepth % 4 != 0) {
      return failure();
    }

    auto inputDepth =
        conv2DOp.input().getType().cast<ShapedType>().getDimSize(3);
    auto filterHeight =
        conv2DOp.filter().getType().cast<ShapedType>().getDimSize(1);
    auto filterWidth =
        conv2DOp.filter().getType().cast<ShapedType>().getDimSize(2);

    // TODO: With multithreading support, we could have multiple kernel types
    // per thread
    Conv2DType kernelType;
    if (inputDepth % 32 == 0 && outputDepth % 16 == 0 &&
        (symbolizePadding(conv2DOp.padding()) == Padding::VALID ||
         (filterHeight == 1 && filterWidth == 1))) {
      kernelType = Conv2DType::ValidDirect;
    } else if (symbolizePadding(conv2DOp.padding()) == Padding::VALID &&
               inputDepth % 4 == 0) {
      kernelType = Conv2DType::ValidIndirect;
    } else {
      kernelType = Conv2DType::PaddedIndirect;
    }

    // Retrieve the remaining args
    auto outputHeight =
        conv2DOp.output().getType().cast<ShapedType>().getDimSize(1);
    auto outputWidth =
        conv2DOp.output().getType().cast<ShapedType>().getDimSize(2);

    auto inputHeight =
        conv2DOp.input().getType().cast<ShapedType>().getDimSize(1);
    auto inputWidth =
        conv2DOp.input().getType().cast<ShapedType>().getDimSize(2);

    auto filterDepth =
        conv2DOp.filter().getType().cast<ShapedType>().getDimSize(3);

    // Get output zero point
    RankedTensorType outputType =
        conv2DOp.output().getType().dyn_cast<RankedTensorType>();
    auto outputQType = outputType.getElementType()
                           .dyn_cast<mlir::quant::UniformQuantizedType>();
    auto outputScale = outputQType.getScale();
    auto outputZeroPoint = outputQType.getZeroPoint();

    // Get input zero point
    RankedTensorType inputType =
        conv2DOp.input().getType().dyn_cast<RankedTensorType>();
    auto inputQType = inputType.getElementType()
                          .dyn_cast<mlir::quant::UniformQuantizedType>();
    auto inputScale = inputQType.getScale();
    auto inputZeroPoint = inputQType.getZeroPoint();

    llvm::dbgs() << "      \n\n      input height " << inputHeight;
    llvm::dbgs() << "      \n\n      input width " << inputWidth;
    llvm::dbgs() << "      \n\n      input depth " << inputDepth;
    llvm::dbgs() << "      \n\n      input scale " << inputScale;
    llvm::dbgs() << "      \n\n      input zero point " << inputZeroPoint;

    llvm::dbgs() << "      \n\n      output height " << outputHeight;
    llvm::dbgs() << "      \n\n      output width " << outputWidth;
    llvm::dbgs() << "      \n\n      output depth " << outputDepth;
    llvm::dbgs() << "      \n\n      output scale " << outputScale;
    llvm::dbgs() << "      \n\n      output zero point " << outputZeroPoint;

    llvm::dbgs() << "      \n\n      filter height " << filterHeight;
    llvm::dbgs() << "      \n\n      filter width " << filterWidth;
    llvm::dbgs() << "      \n\n      filter depth " << filterDepth;

    // Get filter values
    auto filterQConstOp =
        dyn_cast<TFL::QConstOp>(conv2DOp.filter().getDefiningOp());
    auto filter = filterQConstOp.value().cast<DenseElementsAttr>();
    auto filterVector = std::vector<int8_t>{filter.getValues<int8_t>().begin(),
                                            filter.getValues<int8_t>().end()};

    llvm::dbgs() << "      \n\n      filter \n";
    for (auto i : filterVector) {
      llvm::dbgs() << (int)i << " ";
    }

    // Get bias values
    auto biasQConstOp =
        dyn_cast<TFL::QConstOp>(conv2DOp.bias().getDefiningOp());
    auto biases = biasQConstOp.value().cast<DenseElementsAttr>();
    auto biasVector = std::vector<int32_t>{biases.getValues<int32_t>().begin(),
                                           biases.getValues<int32_t>().end()};

    llvm::dbgs() << "      \n\n      bias \n";
    for (auto i : biasVector) {
      llvm::dbgs() << i << " ";
    }

    // Calculate effectiveOutputScale
    std::vector<float> effectiveOutputScaleVector;
    auto filterType = filterQConstOp.qtype().cast<RankedTensorType>();
    bool isPerChannelQuantized = false;
    double filterScale;
    ArrayRef<double> filterScales;
    if (auto filterQType = filterType.getElementType()
                               .dyn_cast<mlir::quant::UniformQuantizedType>()) {
      filterScale = filterQType.getScale();
    } else if (auto filterQType =
                   filterType.getElementType()
                       .dyn_cast<mlir::quant::UniformQuantizedPerAxisType>()) {
      isPerChannelQuantized = true;
      filterScales = filterQType.getScales();
    } else {
      return failure();
    }

    // Conv is quantized along dimension 0:
    // https://www.tensorflow.org/lite/performance/quantization_spec
    auto numOutputChannels =
        conv2DOp.filter().getType().cast<ShapedType>().getDimSize(0);
    for (int i = 0; i < numOutputChannels; ++i) {
      auto scale = isPerChannelQuantized ? filterScales[i] : filterScale;
      assert(outputScale != 0 && "outputScale should not be zero!");
      effectiveOutputScaleVector.push_back(inputScale * scale / outputScale);
    }

    // Find padding values when the kernel type is PaddedIndirect
    // For the other two cases, padding values are set to zero
    tensorflow::int64 newHeight, newWidth;
    tensorflow::int64 padTop, padBottom, padLeft, padRight;
    if (kernelType == Conv2DType::PaddedIndirect) {
      if (tensorflow::GetWindowedOutputSizeVerboseV2(
              inputHeight, filterHeight, conv2DOp.dilation_h_factor(),
              conv2DOp.stride_h(), tensorflow::Padding::SAME, &newHeight,
              &padTop, &padBottom) != tensorflow::Status::OK()) {
        return failure();
      }
      if (tensorflow::GetWindowedOutputSizeVerboseV2(
              inputWidth, filterWidth, conv2DOp.dilation_w_factor(),
              conv2DOp.stride_w(), tensorflow::Padding::SAME, &newWidth,
              &padLeft, &padRight) != tensorflow::Status::OK()) {
        return failure();
      }
    } else {
      padTop = padBottom = padLeft = padRight = 0;
    }

    llvm::dbgs() << "      \n\n      pad top " << padTop;
    llvm::dbgs() << "      \n\n      pad left " << padLeft;
    llvm::dbgs() << "      \n\n      pad bottom " << padBottom;
    llvm::dbgs() << "      \n\n      pad right " << padRight;

    llvm::dbgs() << "      \n\n      strideh " << conv2DOp.stride_h();
    llvm::dbgs() << "      \n\n      stridew " << conv2DOp.stride_w();
    llvm::dbgs() << "      \n\n      dilationh "
                 << conv2DOp.dilation_h_factor();
    llvm::dbgs() << "      \n\n      dilationw "
                 << conv2DOp.dilation_w_factor();

    llvm::dbgs() << "      \n\n      kernel type "
                 << stringifyConv2DType(kernelType) << " \n\n";

    // Create a struct of Conv2DArgs to pass in parameters
    Conv2DArgs args = {
        .outputHeight = static_cast<int>(outputHeight),
        .outputWidth = static_cast<int>(outputWidth),
        .outputDepth = static_cast<int>(outputDepth),
        .outputZeroPoint = static_cast<int>(outputZeroPoint),
        .inputHeight = static_cast<int>(inputHeight),
        .inputWidth = static_cast<int>(inputWidth),
        .inputDepth = static_cast<int>(inputDepth),
        .inputZeroPoint = static_cast<int>(inputZeroPoint),
        .filterHeight = static_cast<int>(filterHeight),
        .filterWidth = static_cast<int>(filterWidth),
        .filterDepth = static_cast<int>(filterDepth),
        .strideH = static_cast<int>(conv2DOp.stride_h()),
        .strideW = static_cast<int>(conv2DOp.stride_w()),
        .dilationH = static_cast<int>(conv2DOp.dilation_h_factor()),
        .dilationW = static_cast<int>(conv2DOp.dilation_w_factor()),
        .filter = filterVector,
        .bias = biasVector,
        .effectiveMultiplier = effectiveOutputScaleVector,
        .topPad = static_cast<int16_t>(padTop),
        .leftPad = static_cast<int16_t>(padLeft),
        .bottomPad = static_cast<int16_t>(padBottom),
        .rightPad = static_cast<int16_t>(padRight),
        // TODO: For BNNs, pad value cannot be zero
        // We should be ideally using a different Conv2D operator for BNNs
        .padValue = 0};

    llvm::SmallVector<std::string> abstractKernelParams, memcpyFnParams,
        aggregateFnParams, outputTransformFnParams, kernelTypeEnumParams;
    llvm::SmallVector<int32_t> scratchByteParams;

    // TODO: Get thread count as command-line option
    // Currently thread count is one
    const int threadCount = 1;

    // TODO: Multithread analysis to determine how to split up the data
    // between threads. Might be better to do this as an analysis pass and
    // access the analysis results here
    for (int i = 0; i < threadCount; ++i) {
      llvm::SmallVector<std::string> conv2DParams;
      // TODO: Determine which kernel type for each thread.
      kernelTypeEnumParams.push_back(stringifyConv2DType(kernelType).str());

      // TODO: Call the right kernel type function
      // Call the kernel type function which returns a vector of four strings
      // for the four Conv2D params
      switch (kernelType) {
      case Conv2DType::ValidDirect:
        conv2DParams = getConv2DValidDirectParams(args);
        break;
      case Conv2DType::ValidIndirect:
        conv2DParams = getConv2DValidIndirectParams(args);
        break;
      case Conv2DType::PaddedIndirect:
        conv2DParams = getConv2DPaddedIndirectParams(args);
        break;
      }

      abstractKernelParams.push_back(conv2DParams[0]);
      memcpyFnParams.push_back(conv2DParams[1]);
      aggregateFnParams.push_back(conv2DParams[2]);
      outputTransformFnParams.push_back(conv2DParams[3]);

      // Find out the scratch size needed
      int inputBytes = args.filterHeight * args.filterWidth * args.inputDepth;
      int scratchBytes =
          nn::MatMulInt8::get_scratch_mem_bytes(inputBytes) + 32; //[asj] FIXME
      scratchByteParams.push_back(scratchBytes);
    }

    // Create a string array attr from a vector of strings
    auto getStringArrayAttr = [&](llvm::SmallVector<std::string> value) {
      auto attrs = llvm::to_vector<8>(
          llvm::map_range(value, [&](std::string v) -> Attribute {
            return rewriter.getStringAttr(v);
          }));
      return rewriter.getArrayAttr(attrs);
    };

    // Create the Conv2DV2 Op with the params and kernel type
    auto newConv2DV2Op = rewriter.create<Conv2DV2Op>(
        conv2DOp.getLoc(), conv2DOp.getType(), conv2DOp.input(),
        rewriter.getI32IntegerAttr(threadCount),
        rewriter.getI32ArrayAttr(scratchByteParams),
        getStringArrayAttr(abstractKernelParams),
        getStringArrayAttr(memcpyFnParams),
        getStringArrayAttr(aggregateFnParams),
        getStringArrayAttr(outputTransformFnParams),
        getStringArrayAttr(kernelTypeEnumParams));
    rewriter.replaceOp(conv2DOp, newConv2DV2Op.output());

    return success();
  }
};

void ReplaceWithConv2DV2::runOnFunction() {
  auto *ctx = &getContext();
  auto func = getFunction();

  OwningRewritePatternList patterns(ctx);
  patterns.insert<ReplaceWithConv2DV2Pattern>(ctx);
  (void)applyPatternsAndFoldGreedily(func, std::move(patterns));
}
} // namespace

// Creates an instance of the ReplaceWithConv2DV2 pass.
std::unique_ptr<OperationPass<FuncOp>> createReplaceWithConv2DV2Pass() {
  return std::make_unique<ReplaceWithConv2DV2>();
}

static PassRegistration<ReplaceWithConv2DV2>
    pass("xcore-replace-with-conv2dv2",
         "Replace TFL Conv2D with XC Conv2DV2 operations.");

} // namespace xcore
} // namespace mlir

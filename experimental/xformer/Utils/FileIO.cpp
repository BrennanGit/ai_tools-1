// Copyright 2021 XMOS LIMITED. This Software is subject to the terms of the
// XMOS Public License: Version 1

#include "Utils/FileIO.h"

#include "mlir/Support/FileUtilities.h"
#include "tensorflow/compiler/mlir/lite/flatbuffer_export.h"
#include "tensorflow/compiler/mlir/lite/flatbuffer_import.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/ToolOutputFile.h"

namespace mlir {
namespace xcore {
namespace utils {

LogicalResult writeMLIRToFlatBufferFile(std::string &filename,
                                        mlir::ModuleOp module) {
  std::string serialized_flatbuffer;
  tflite::FlatbufferExportOptions options;
  options.emit_builtin_tflite_ops = true;
  options.emit_select_tf_ops = true;
  options.emit_custom_ops = true;

  if (!tflite::MlirToFlatBufferTranslateFunction(module, options,
                                                 &serialized_flatbuffer)) {
    auto outputFile = openOutputFile(filename);
    if (!outputFile) {
      llvm::errs() << "Could not open output file: " << filename << "\n";
      return failure();
    }

    outputFile->os() << serialized_flatbuffer;
    outputFile->keep();
    return success();
  } else {
    llvm::errs() << "Error converting MLIR to flatbuffer, no file written"
                 << "\n";
    return failure();
  }
}

mlir::OwningModuleRef readFlatBufferFileToMLIR(std::string &filename,
                                               mlir::MLIRContext *context) {
  std::string errorMessage;
  auto inputFile = openInputFile(filename, &errorMessage);
  if (!inputFile) {
    llvm::errs() << errorMessage << "\n";
    return mlir::OwningModuleRef(nullptr);
  }

  auto loc = mlir::FileLineColLoc::get(context,
                                       inputFile->getBufferIdentifier(), 0, 0);
  OwningModuleRef mod =
      tflite::FlatBufferToMlir(absl::string_view(inputFile->getBufferStart(),
                                                 inputFile->getBufferSize()),
                               context, loc);
  return mod;
}

} // namespace utils
} // namespace xcore
} // namespace mlir

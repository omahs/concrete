// Part of the Concrete Compiler Project, under the BSD3 License with Zama
// Exceptions. See
// https://github.com/zama-ai/concrete-compiler-internal/blob/main/LICENSE.txt
// for license information.

#ifndef CONCRETELANG_CONVERSION_FHETOTFHECRT_PASS_H_
#define CONCRETELANG_CONVERSION_FHETOTFHECRT_PASS_H_

#include "mlir/Pass/Pass.h"
#include "llvm/Support/Casting.h"
#include <list>

namespace mlir {
namespace concretelang {

struct CrtLoweringParameters {
  mlir::SmallVector<int64_t> mods;
  mlir::SmallVector<int64_t> bits;
  size_t nMods;
  size_t modsProd;
  size_t bitsTotal;
  size_t polynomialSize;
  size_t lutSize;

  CrtLoweringParameters(mlir::SmallVector<int64_t> mods, size_t polySize)
      : mods(mods), polynomialSize(polySize) {
    nMods = mods.size();
    modsProd = 1;
    bitsTotal = 0;
    bits.clear();
    for (auto &mod : mods) {
      modsProd *= mod;
      uint64_t nbits =
          static_cast<uint64_t>(ceil(log2(static_cast<double>(mod))));
      bits.push_back(nbits);
      bitsTotal += nbits;
    }
    size_t lutCrtSize = size_t(1) << bitsTotal;
    lutCrtSize = std::max(lutCrtSize, polynomialSize);
    lutSize = mods.size() * lutCrtSize;
  }
};

/// Create a pass to convert `FHE` dialect to `TFHE` dialect with the crt
// strategy.
std::unique_ptr<OperationPass<mlir::ModuleOp>>
createConvertFHEToTFHECrtPass(CrtLoweringParameters lowering);
} // namespace concretelang
} // namespace mlir

#endif

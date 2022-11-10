// Part of the Concrete Compiler Project, under the BSD3 License with Zama
// Exceptions. See
// https://github.com/zama-ai/concrete-compiler-internal/blob/main/LICENSE.txt
// for license information.

#include <iostream>
#include <mlir/Dialect/Arithmetic/IR/Arithmetic.h>
#include <mlir/Dialect/Bufferization/IR/Bufferization.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/Linalg/IR/Linalg.h>
#include <mlir/IR/Operation.h>

#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

#include "concretelang/Conversion/FHEToTFHEScalar/Pass.h"
#include "concretelang/Conversion/Passes.h"
#include "concretelang/Conversion/Tools.h"
#include "concretelang/Conversion/Utils/FuncConstOpConversion.h"
#include "concretelang/Conversion/Utils/RegionOpTypeConverterPattern.h"
#include "concretelang/Conversion/Utils/TensorOpTypeConversion.h"
#include "concretelang/Dialect/FHE/IR/FHEDialect.h"
#include "concretelang/Dialect/FHE/IR/FHEOps.h"
#include "concretelang/Dialect/FHE/IR/FHETypes.h"
#include "concretelang/Dialect/RT/IR/RTDialect.h"
#include "concretelang/Dialect/RT/IR/RTOps.h"
#include "concretelang/Dialect/RT/IR/RTTypes.h"
#include "concretelang/Dialect/TFHE/IR/TFHEDialect.h"
#include "concretelang/Dialect/TFHE/IR/TFHEOps.h"
#include "concretelang/Dialect/TFHE/IR/TFHETypes.h"

namespace FHE = mlir::concretelang::FHE;
namespace TFHE = mlir::concretelang::TFHE;
namespace concretelang = mlir::concretelang;

namespace fhe_to_tfhe_scalar_conversion {

namespace typing {

/// Converts `FHE::EncryptedInteger` into `TFHE::GlweCiphetext`.
TFHE::GLWECipherTextType convertEint(mlir::MLIRContext *context,
                                     FHE::EncryptedIntegerType eint) {
  return TFHE::GLWECipherTextType::get(context, -1, -1, -1, eint.getWidth());
}

/// Converts `Tensor<FHE::EncryptedInteger>` into a
/// `Tensor<TFHE::GlweCiphertext>` if the element type is appropriate.
/// Otherwise return the input type.
mlir::Type maybeConvertEintTensor(mlir::MLIRContext *context,
                                  mlir::RankedTensorType maybeEintTensor) {
  if (!maybeEintTensor.getElementType().isa<FHE::EncryptedIntegerType>()) {
    return (mlir::Type)(maybeEintTensor);
  }
  auto eint =
      maybeEintTensor.getElementType().cast<FHE::EncryptedIntegerType>();
  auto currentShape = maybeEintTensor.getShape();
  return mlir::RankedTensorType::get(
      currentShape,
      TFHE::GLWECipherTextType::get(context, -1, -1, -1, eint.getWidth()));
}

/// Converts the type `FHE::EncryptedInteger` to `TFHE::GlweCiphetext` if the
/// input type is appropriate. Otherwise return the input type.
mlir::Type maybeConvertEint(mlir::MLIRContext *context, mlir::Type t) {
  if (auto eint = t.dyn_cast<FHE::EncryptedIntegerType>())
    return convertEint(context, eint);

  return t;
}

/// The type converter used to convert `FHE` to `TFHE` types using the scalar
/// strategy.
class TypeConverter : public mlir::TypeConverter {

public:
  TypeConverter() {
    addConversion([](mlir::Type type) { return type; });
    addConversion([](FHE::EncryptedIntegerType type) {
      return convertEint(type.getContext(), type);
    });
    addConversion([](mlir::RankedTensorType type) {
      return maybeConvertEintTensor(type.getContext(), type);
    });
    addConversion([&](concretelang::RT::FutureType type) {
      return concretelang::RT::FutureType::get(this->convertType(
          type.dyn_cast<concretelang::RT::FutureType>().getElementType()));
    });
    addConversion([&](concretelang::RT::PointerType type) {
      return concretelang::RT::PointerType::get(this->convertType(
          type.dyn_cast<concretelang::RT::PointerType>().getElementType()));
    });
  }

  /// Returns a lambda that uses this converter to turn one type into another.
  std::function<mlir::Type(mlir::MLIRContext *, mlir::Type)>
  getConversionLambda() {
    return [&](mlir::MLIRContext *, mlir::Type t) { return convertType(t); };
  }
};

} // namespace typing

namespace lowering {

/// A pattern rewriter superclass used by most op rewriters during the
/// conversion.
template <typename T>
struct ScalarOpPattern : public mlir::OpRewritePattern<T> {

  ScalarOpPattern(mlir::MLIRContext *context, mlir::PatternBenefit benefit = 1)
      : mlir::OpRewritePattern<T>(context, benefit) {}

  /// Writes the encoding of a plaintext of arbitrary precision using shift.
  mlir::Value
  writePlaintextShiftEncoding(mlir::Location location, mlir::Value rawPlaintext,
                              int64_t encryptedWidth,
                              mlir::PatternRewriter &rewriter) const {
    int64_t intShift = 64 - 1 - encryptedWidth;
    mlir::Value castedInt = rewriter.create<mlir::arith::ExtUIOp>(
        location, rewriter.getIntegerType(64), rawPlaintext);
    mlir::Value constantShiftOp = rewriter.create<mlir::arith::ConstantOp>(
        location, rewriter.getI64IntegerAttr(intShift));
    mlir::Value encodedInt = rewriter.create<mlir::arith::ShLIOp>(
        location, rewriter.getI64Type(), castedInt, constantShiftOp);
    return encodedInt;
  }
};

/// Rewriter for the `FHE::zero` operation.
struct ZeroEintOpPattern : public mlir::OpRewritePattern<FHE::ZeroEintOp> {
  ZeroEintOpPattern(mlir::MLIRContext *context,
                    mlir::PatternBenefit benefit = 1)
      : mlir::OpRewritePattern<FHE::ZeroEintOp>(context, benefit) {}

  mlir::LogicalResult
  matchAndRewrite(FHE::ZeroEintOp op,
                  mlir::PatternRewriter &rewriter) const override {
    mlir::Location location = op.getLoc();
    typing::TypeConverter converter;
    TFHE::ZeroGLWEOp newOp =
        rewriter.create<TFHE::ZeroGLWEOp>(location, op.getType());
    concretelang::convertOperandAndResultTypes(rewriter, newOp,
                                               converter.getConversionLambda());
    rewriter.replaceOp(op, {newOp.getResult()});
    return mlir::success();
  }
};

/// Rewriter for the `FHE::add_eint_int` operation.
struct AddEintIntOpPattern : public ScalarOpPattern<FHE::AddEintIntOp> {
  AddEintIntOpPattern(mlir::MLIRContext *context,
                      mlir::PatternBenefit benefit = 1)
      : ScalarOpPattern<FHE::AddEintIntOp>(context, benefit) {}

  mlir::LogicalResult
  matchAndRewrite(FHE::AddEintIntOp op,
                  mlir::PatternRewriter &rewriter) const override {
    mlir::Location location = op.getLoc();
    mlir::Value eintOperand = op.a();
    mlir::Value intOperand = op.b();

    // Write the plaintext encoding
    mlir::Value encodedInt = writePlaintextShiftEncoding(
        op.getLoc(), intOperand,
        eintOperand.getType().cast<FHE::EncryptedIntegerType>().getWidth(),
        rewriter);

    // Write the new op
    auto newOp = rewriter.create<TFHE::AddGLWEIntOp>(location, op.getType(),
                                                     eintOperand, encodedInt);
    typing::TypeConverter converter;
    concretelang::convertOperandAndResultTypes(rewriter, newOp,
                                               converter.getConversionLambda());

    rewriter.replaceOp(op, {newOp.getResult()});

    return mlir::success();
  }
};

/// Rewriter for the `FHE::sub_eint_int` operation.
struct SubEintIntOpPattern : public ScalarOpPattern<FHE::SubEintIntOp> {
  SubEintIntOpPattern(mlir::MLIRContext *context,
                      mlir::PatternBenefit benefit = 1)
      : ScalarOpPattern<FHE::SubEintIntOp>(context, benefit) {}

  mlir::LogicalResult
  matchAndRewrite(FHE::SubEintIntOp op,
                  mlir::PatternRewriter &rewriter) const override {
    mlir::Location location = op.getLoc();
    mlir::Value eintOperand = op.a();
    mlir::Value intOperand = op.b();

    // Write the integer negation
    mlir::Type intType = intOperand.getType();
    mlir::Attribute minusOneAttr = mlir::IntegerAttr::get(intType, -1);
    mlir::Value minusOne =
        rewriter.create<mlir::arith::ConstantOp>(location, minusOneAttr)
            .getResult();
    mlir::Value negative =
        rewriter.create<mlir::arith::MulIOp>(location, intOperand, minusOne)
            .getResult();

    // Write the plaintext encoding
    mlir::Value encodedInt = writePlaintextShiftEncoding(
        op.getLoc(), negative,
        eintOperand.getType().cast<FHE::EncryptedIntegerType>().getWidth(),
        rewriter);

    // Write the new op
    auto newOp = rewriter.create<TFHE::AddGLWEIntOp>(location, op.getType(),
                                                     eintOperand, encodedInt);
    typing::TypeConverter converter;

    // Convert the types
    concretelang::convertOperandAndResultTypes(rewriter, newOp,
                                               converter.getConversionLambda());

    rewriter.replaceOp(op, {newOp.getResult()});

    return mlir::success();
  };
};

/// Rewriter for the `FHE::sub_int_eint` operation.
struct SubIntEintOpPattern : public ScalarOpPattern<FHE::SubIntEintOp> {
  SubIntEintOpPattern(mlir::MLIRContext *context,
                      mlir::PatternBenefit benefit = 1)
      : ScalarOpPattern<FHE::SubIntEintOp>(context, benefit) {}

  mlir::LogicalResult
  matchAndRewrite(FHE::SubIntEintOp op,
                  mlir::PatternRewriter &rewriter) const override {
    mlir::Location location = op.getLoc();
    mlir::Value intOperand = op.a();
    mlir::Value eintOperand = op.b();

    // Write the plaintext encoding
    mlir::Value encodedInt = writePlaintextShiftEncoding(
        op.getLoc(), intOperand,
        eintOperand.getType().cast<FHE::EncryptedIntegerType>().getWidth(),
        rewriter);

    // Write the new op
    auto newOp = rewriter.create<TFHE::SubGLWEIntOp>(location, op.getType(),
                                                     encodedInt, eintOperand);
    typing::TypeConverter converter;
    concretelang::convertOperandAndResultTypes(rewriter, newOp,
                                               converter.getConversionLambda());

    rewriter.replaceOp(op, {newOp.getResult()});

    return mlir::success();
  };
};

/// Rewriter for the `FHE::sub_eint` operation.
struct SubEintOpPattern : public ScalarOpPattern<FHE::SubEintOp> {
  SubEintOpPattern(mlir::MLIRContext *context, mlir::PatternBenefit benefit = 1)
      : ScalarOpPattern<FHE::SubEintOp>(context, benefit) {}

  mlir::LogicalResult
  matchAndRewrite(FHE::SubEintOp op,
                  mlir::PatternRewriter &rewriter) const override {
    mlir::Location location = op.getLoc();
    mlir::Value lhsOperand = op.a();
    mlir::Value rhsOperand = op.b();

    // Write rhs negation
    auto negative = rewriter.create<TFHE::NegGLWEOp>(
        location, rhsOperand.getType(), rhsOperand);
    typing::TypeConverter converter;
    concretelang::convertOperandAndResultTypes(rewriter, negative,
                                               converter.getConversionLambda());

    // Write new op.
    auto newOp = rewriter.create<TFHE::AddGLWEOp>(
        location, op.getType(), lhsOperand, negative.getResult());
    concretelang::convertOperandAndResultTypes(rewriter, newOp,
                                               converter.getConversionLambda());

    rewriter.replaceOp(op, {newOp.getResult()});
    return mlir::success();
  };
};

/// Rewriter for the `FHE::mul_eint_int` operation.
struct MulEintIntOpPattern : public ScalarOpPattern<FHE::MulEintIntOp> {
  MulEintIntOpPattern(mlir::MLIRContext *context,
                      mlir::PatternBenefit benefit = 1)
      : ScalarOpPattern<FHE::MulEintIntOp>(context, benefit) {}

  mlir::LogicalResult
  matchAndRewrite(FHE::MulEintIntOp op,
                  mlir::PatternRewriter &rewriter) const override {

    mlir::Location location = op.getLoc();
    mlir::Value eintOperand = op.a();
    mlir::Value intOperand = op.b();

    // Write the cleartext "encoding"
    mlir::Value castedCleartext = rewriter.create<mlir::arith::ExtSIOp>(
        location, rewriter.getIntegerType(64), intOperand);

    // Write the new op.
    auto newOp = rewriter.create<TFHE::MulGLWEIntOp>(
        location, op.getType(), eintOperand, castedCleartext);
    typing::TypeConverter converter;
    concretelang::convertOperandAndResultTypes(rewriter, newOp,
                                               converter.getConversionLambda());

    rewriter.replaceOp(op, {newOp.getResult()});

    return mlir::success();
  }
};

/// Rewriter for the `FHE::apply_lookup_table` operation.
struct ApplyLookupTableEintOpPattern
    : public ScalarOpPattern<FHE::ApplyLookupTableEintOp> {
  ApplyLookupTableEintOpPattern(
      mlir::MLIRContext *context,
      concretelang::ScalarLoweringParameters loweringParams,
      mlir::PatternBenefit benefit = 1)
      : ScalarOpPattern<FHE::ApplyLookupTableEintOp>(context, benefit),
        loweringParameters(loweringParams) {}

  mlir::LogicalResult
  matchAndRewrite(FHE::ApplyLookupTableEintOp op,
                  mlir::PatternRewriter &rewriter) const override {

    size_t outputBits =
        op.getResult().getType().cast<FHE::EncryptedIntegerType>().getWidth();
    mlir::Value newLut =
        rewriter
            .create<TFHE::EncodeExpandLutForBootstrapOp>(
                op.getLoc(),
                mlir::RankedTensorType::get(
                    mlir::ArrayRef<int64_t>(loweringParameters.polynomialSize),
                    rewriter.getI64Type()),
                op.lut(),
                rewriter.getI32IntegerAttr(loweringParameters.polynomialSize),
                rewriter.getI32IntegerAttr(outputBits))
            .getResult();

    // Insert keyswitch
    auto ksOp = rewriter.create<TFHE::KeySwitchGLWEOp>(
        op.getLoc(), op.a().getType(), op.a(), -1, -1);
    typing::TypeConverter converter;
    concretelang::convertOperandAndResultTypes(rewriter, ksOp,
                                               converter.getConversionLambda());

    // Insert bootstrap
    auto bsOp = rewriter.replaceOpWithNewOp<TFHE::BootstrapGLWEOp>(
        op, op.getType(), ksOp, newLut, -1, -1, -1, -1);
    concretelang::convertOperandAndResultTypes(rewriter, bsOp,
                                               converter.getConversionLambda());

    return mlir::success();
  };

private:
  concretelang::ScalarLoweringParameters loweringParameters;
};

} // namespace lowering

struct FHEToTFHEScalarPass : public FHEToTFHEScalarBase<FHEToTFHEScalarPass> {

  FHEToTFHEScalarPass(concretelang::ScalarLoweringParameters loweringParams)
      : loweringParameters(loweringParams){};

  void runOnOperation() override {
    auto op = this->getOperation();

    mlir::ConversionTarget target(getContext());
    typing::TypeConverter converter;

    //------------------------------------------- Marking legal/illegal dialects
    target.addIllegalDialect<FHE::FHEDialect>();
    target.addLegalDialect<TFHE::TFHEDialect>();
    target.addLegalDialect<mlir::arith::ArithmeticDialect>();
    target.addDynamicallyLegalOp<mlir::linalg::GenericOp,
                                 mlir::tensor::GenerateOp>(
        [&](mlir::Operation *op) {
          return (
              converter.isLegal(op->getOperandTypes()) &&
              converter.isLegal(op->getResultTypes()) &&
              converter.isLegal(op->getRegion(0).front().getArgumentTypes()));
        });
    target.addDynamicallyLegalOp<mlir::func::FuncOp>(
        [&](mlir::func::FuncOp funcOp) {
          return converter.isSignatureLegal(funcOp.getFunctionType()) &&
                 converter.isLegal(&funcOp.getBody());
        });
    target.addDynamicallyLegalOp<mlir::func::ConstantOp>(
        [&](mlir::func::ConstantOp op) {
          return FunctionConstantOpConversion<typing::TypeConverter>::isLegal(
              op, converter);
        });
    target.addLegalOp<mlir::func::CallOp>();
    concretelang::addDynamicallyLegalTypeOp<
        concretelang::RT::MakeReadyFutureOp>(target, converter);
    concretelang::addDynamicallyLegalTypeOp<concretelang::RT::AwaitFutureOp>(
        target, converter);
    concretelang::addDynamicallyLegalTypeOp<
        concretelang::RT::CreateAsyncTaskOp>(target, converter);
    concretelang::addDynamicallyLegalTypeOp<
        concretelang::RT::BuildReturnPtrPlaceholderOp>(target, converter);
    concretelang::addDynamicallyLegalTypeOp<
        concretelang::RT::DerefWorkFunctionArgumentPtrPlaceholderOp>(target,
                                                                     converter);
    concretelang::addDynamicallyLegalTypeOp<
        concretelang::RT::DerefReturnPtrPlaceholderOp>(target, converter);
    concretelang::addDynamicallyLegalTypeOp<
        concretelang::RT::WorkFunctionReturnOp>(target, converter);
    concretelang::addDynamicallyLegalTypeOp<
        concretelang::RT::RegisterTaskWorkFunctionOp>(target, converter);

    //---------------------------------------------------------- Adding patterns
    mlir::RewritePatternSet patterns(&getContext());

    // Patterns for the `FHE` dialect operations
    patterns.add<
        //    |_ `FHE::zero_eint`
        concretelang::GenericTypeAndOpConverterPattern<FHE::ZeroEintOp,
                                                       TFHE::ZeroGLWEOp>,
        //    |_ `FHE::zero_tensor`
        concretelang::GenericTypeAndOpConverterPattern<FHE::ZeroTensorOp,
                                                       TFHE::ZeroTensorGLWEOp>,
        //    |_ `FHE::neg_eint`
        concretelang::GenericTypeAndOpConverterPattern<FHE::NegEintOp,
                                                       TFHE::NegGLWEOp>,
        //    |_ `FHE::add_eint`
        concretelang::GenericTypeAndOpConverterPattern<FHE::AddEintOp,
                                                       TFHE::AddGLWEOp>>(
        &getContext(), converter);
    //    |_ `FHE::add_eint_int`
    patterns.add<lowering::AddEintIntOpPattern,
                 //    |_ `FHE::sub_int_eint`
                 lowering::SubIntEintOpPattern,
                 //    |_ `FHE::sub_eint_int`
                 lowering::SubEintIntOpPattern,
                 //    |_ `FHE::sub_eint`
                 lowering::SubEintOpPattern,
                 //    |_ `FHE::mul_eint_int`
                 lowering::MulEintIntOpPattern>(&getContext());
    //    |_ `FHE::apply_lookup_table`
    patterns.add<lowering::ApplyLookupTableEintOpPattern>(&getContext(),
                                                          loweringParameters);

    // Patterns for the relics of the `FHELinalg` dialect operations.
    //    |_ `linalg::generic` turned to nested `scf::for`
    patterns
        .add<concretelang::GenericTypeConverterPattern<mlir::linalg::YieldOp>>(
            patterns.getContext(), converter);
    patterns.add<RegionOpTypeConverterPattern<mlir::tensor::GenerateOp,
                                              typing::TypeConverter>>(
        &getContext(), converter);
    patterns.add<
        RegionOpTypeConverterPattern<mlir::scf::ForOp, typing::TypeConverter>>(
        &getContext(), converter);
    concretelang::populateWithTensorTypeConverterPatterns(patterns, target,
                                                          converter);

    // Patterns for `func` dialect operations.
    mlir::populateFunctionOpInterfaceTypeConversionPattern<mlir::func::FuncOp>(
        patterns, converter);
    patterns
        .add<concretelang::GenericTypeConverterPattern<mlir::func::ReturnOp>>(
            patterns.getContext(), converter);
    patterns.add<FunctionConstantOpConversion<typing::TypeConverter>>(
        &getContext(), converter);

    // Patterns for the `RT` dialect operations.
    patterns
        .add<concretelang::GenericTypeConverterPattern<mlir::func::ReturnOp>,
             concretelang::GenericTypeConverterPattern<mlir::scf::YieldOp>,
             concretelang::GenericTypeConverterPattern<
                 concretelang::RT::MakeReadyFutureOp>,
             concretelang::GenericTypeConverterPattern<
                 concretelang::RT::AwaitFutureOp>,
             concretelang::GenericTypeConverterPattern<
                 concretelang::RT::CreateAsyncTaskOp>,
             concretelang::GenericTypeConverterPattern<
                 concretelang::RT::BuildReturnPtrPlaceholderOp>,
             concretelang::GenericTypeConverterPattern<
                 concretelang::RT::DerefWorkFunctionArgumentPtrPlaceholderOp>,
             concretelang::GenericTypeConverterPattern<
                 concretelang::RT::DerefReturnPtrPlaceholderOp>,
             concretelang::GenericTypeConverterPattern<
                 concretelang::RT::WorkFunctionReturnOp>,
             concretelang::GenericTypeConverterPattern<
                 concretelang::RT::RegisterTaskWorkFunctionOp>>(&getContext(),
                                                                converter);

    //--------------------------------------------------------- Apply conversion
    if (mlir::applyPartialConversion(op, target, std::move(patterns))
            .failed()) {
      this->signalPassFailure();
    }
  }

private:
  concretelang::ScalarLoweringParameters loweringParameters;
};

} // namespace fhe_to_tfhe_scalar_conversion

namespace mlir {
namespace concretelang {
std::unique_ptr<OperationPass<ModuleOp>>
createConvertFHEToTFHEScalarPass(ScalarLoweringParameters loweringParameters) {
  return std::make_unique<fhe_to_tfhe_scalar_conversion::FHEToTFHEScalarPass>(
      loweringParameters);
}
} // namespace concretelang
} // namespace mlir

// RUN: concretecompiler --passes concrete-to-bconcrete --action=dump-bconcrete %s 2>&1| FileCheck %s

//CHECK: func.func @mul_lwe_const_int(%[[A0:.*]]: tensor<1025xi64>) -> tensor<1025xi64> {
//CHECK:   %c1_i64 = arith.constant 1 : i64
//CHECK:   %[[V1:.*]] = "BConcrete.mul_cleartext_lwe_tensor"(%[[A0]], %c1_i64) : (tensor<1025xi64>, i64) -> tensor<1025xi64>
//CHECK:   return %[[V1]] : tensor<1025xi64>
//CHECK: }
func.func @mul_lwe_const_int(%arg0: !Concrete.lwe_ciphertext<1024,7>) -> !Concrete.lwe_ciphertext<1024,7> {
  %0 = arith.constant 1 : i64
  %2 = "Concrete.mul_cleartext_lwe_ciphertext"(%arg0, %0) : (!Concrete.lwe_ciphertext<1024,7>, i64) -> !Concrete.lwe_ciphertext<1024,7>
  return %2 : !Concrete.lwe_ciphertext<1024,7>
}

//CHECK: func.func @mul_lwe_int(%[[A0:.*]]: tensor<1025xi64>, %[[A1:.*]]: i64) -> tensor<1025xi64> {
//CHECK:   %[[V1:.*]] = "BConcrete.mul_cleartext_lwe_tensor"(%[[A0]], %[[A1]]) : (tensor<1025xi64>, i64) -> tensor<1025xi64>
//CHECK:   return %[[V1]] : tensor<1025xi64>
//CHECK: }
func.func @mul_lwe_int(%arg0: !Concrete.lwe_ciphertext<1024,4>, %arg1: i64) -> !Concrete.lwe_ciphertext<1024,4> {
  %1 = "Concrete.mul_cleartext_lwe_ciphertext"(%arg0, %arg1) : (!Concrete.lwe_ciphertext<1024,4>, i64) -> !Concrete.lwe_ciphertext<1024,4>
  return %1 : !Concrete.lwe_ciphertext<1024,4>
}

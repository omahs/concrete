// RUN: concretecompiler --action=dump-tfhe %s --large-integer-crt-decomposition=2,3,5,7,11 --large-integer-circuit-bootstrap=2,9 --large-integer-packing-keyswitch=694,1024,4,9 --v0-parameter=2,10,693,4,9,7,2 2>&1| FileCheck %s
// RUN: concretecompiler --action=dump-tfhe %s --large-integer-crt-decomposition=2,3,5,7,11 --large-integer-circuit-bootstrap=2,9 --large-integer-packing-keyswitch=694,1024,4,9 --v0-parameter=2,10,693,4,9,7,2 2>&1| FileCheck %s

// CHECK-LABEL: func.func @sub_int_eint(%arg0: tensor<5x!TFHE.glwe<{_,_,_}{7}>>) -> tensor<5x!TFHE.glwe<{_,_,_}{7}>>
func.func @sub_int_eint(%arg0: !FHE.eint<7>) -> !FHE.eint<7> {
  // CHECK-NEXT:  %c1_i8 = arith.constant 1 : i8
  // CHECK-NEXT:  %0 = arith.extui %c1_i8 : i8 to i64
  // CHECK-NEXT:  %1 = "TFHE.encode_plaintext_with_crt"(%0) {mods = [2, 3, 5, 7, 11], modsProd = 2310 : i64} : (i64) -> tensor<5xi64>
  // CHECK-NEXT:  %c0 = arith.constant 0 : index
  // CHECK-NEXT:  %c1 = arith.constant 1 : index
  // CHECK-NEXT:  %c5 = arith.constant 5 : index
  // CHECK-NEXT:  %2:2 = scf.for %arg1 = %c0 to %c5 step %c1 iter_args(%arg2 = %arg0, %arg3 = %1) -> (tensor<5x!TFHE.glwe<{_,_,_}{7}>>, tensor<5xi64>) {
  // CHECK-NEXT:    %3 = tensor.extract %arg2[%arg1] : tensor<5x!TFHE.glwe<{_,_,_}{7}>>
  // CHECK-NEXT:    %4 = tensor.extract %arg3[%arg1] : tensor<5xi64>
  // CHECK-NEXT:    %5 = "TFHE.sub_int_glwe"(%4, %3) : (i64, !TFHE.glwe<{_,_,_}{7}>) -> !TFHE.glwe<{_,_,_}{7}>
  // CHECK-NEXT:    %6 = tensor.insert %5 into %arg2[%arg1] : tensor<5x!TFHE.glwe<{_,_,_}{7}>>
  // CHECK-NEXT:    scf.yield %6, %arg3 : tensor<5x!TFHE.glwe<{_,_,_}{7}>>, tensor<5xi64>
  // CHECK-NEXT:  }
  // CHECK-NEXT:  return %2#0 : tensor<5x!TFHE.glwe<{_,_,_}{7}>>

  %0 = arith.constant 1 : i8
  %1 = "FHE.sub_int_eint"(%0, %arg0): (i8, !FHE.eint<7>) -> (!FHE.eint<7>)
  return %1: !FHE.eint<7>
}

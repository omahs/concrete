// RUN: concretecompiler --split-input-file --verify-diagnostics --action=roundtrip %s

// GLWE p parameter
func.func @mul_glwe_int(%arg0: !TFHE.glwe<{1024,12,64}{7}>) -> !TFHE.glwe<{1024,12,64}{6}> {
  %0 = arith.constant 1 : i8
  // expected-error @+1 {{'TFHE.mul_glwe_int' op should have the same GLWE 'p' parameter}}
  %1 = "TFHE.mul_glwe_int"(%arg0, %0): (!TFHE.glwe<{1024,12,64}{7}>, i8) -> (!TFHE.glwe<{1024,12,64}{6}>)
  return %1: !TFHE.glwe<{1024,12,64}{6}>
}

// -----

// GLWE dimension parameter
func.func @mul_glwe_int(%arg0: !TFHE.glwe<{1024,12,64}{7}>) -> !TFHE.glwe<{512,12,64}{7}> {
  %0 = arith.constant 1 : i8
  // expected-error @+1 {{'TFHE.mul_glwe_int' op should have the same GLWE 'dimension' parameter}}
  %1 = "TFHE.mul_glwe_int"(%arg0, %0): (!TFHE.glwe<{1024,12,64}{7}>, i8) -> (!TFHE.glwe<{512,12,64}{7}>)
  return %1: !TFHE.glwe<{512,12,64}{7}>
}

// -----

// GLWE polynomialSize parameter
func.func @mul_glwe_int(%arg0: !TFHE.glwe<{1024,12,64}{7}>) -> !TFHE.glwe<{1024,11,64}{7}> {
  %0 = arith.constant 1 : i8
  // expected-error @+1 {{'TFHE.mul_glwe_int' op should have the same GLWE 'polynomialSize' parameter}}
  %1 = "TFHE.mul_glwe_int"(%arg0, %0): (!TFHE.glwe<{1024,12,64}{7}>, i8) -> (!TFHE.glwe<{1024,11,64}{7}>)
  return %1: !TFHE.glwe<{1024,11,64}{7}>
}

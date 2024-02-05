// Part of the Concrete Compiler Project, under the BSD3 License with Zama
// Exceptions. See
// https://github.com/zama-ai/concrete-compiler-internal/blob/main/LICENSE.txt
// for license information.

#include "concretelang/Runtime/context.h"
#include "concretelang/Common/Error.h"
#include "concretelang/Common/Keysets.h"
#include "concretelang/Runtime/dfr_debug_interface.h"
#include "concretelang/Runtime/key_manager.hpp"
#include <assert.h>
#include <stdio.h>

namespace mlir {
namespace concretelang {
namespace dfr {} // namespace dfr
} // namespace concretelang
} // namespace mlir

HPX_PLAIN_ACTION(mlir::concretelang::dfr::getKsk, _dfr_get_ksk);
HPX_PLAIN_ACTION(mlir::concretelang::dfr::getBsk, _dfr_get_bsk);
HPX_PLAIN_ACTION(mlir::concretelang::dfr::getPKsk, _dfr_get_pksk);

namespace mlir {
namespace concretelang {

FFT::FFT(size_t polynomial_size)
    : fft(nullptr), polynomial_size(polynomial_size) {
  fft = (struct Fft *)aligned_alloc(CONCRETE_FFT_ALIGN, CONCRETE_FFT_SIZE);
  concrete_cpu_construct_concrete_fft(fft, polynomial_size);
}

FFT::FFT(FFT &&other) : fft(other.fft), polynomial_size(other.polynomial_size) {
  other.fft = nullptr;
}

FFT::~FFT() {
  if (fft != nullptr) {
    concrete_cpu_destroy_concrete_fft(fft);
    free(fft);
  }
}

RuntimeContext::RuntimeContext(ServerKeyset serverKeyset)
    : serverKeyset(serverKeyset) {
  {

    // Initialize for each bootstrap key the fourier one
    for (size_t i = 0; i < serverKeyset.lweBootstrapKeys.size(); i++) {

      auto bsk = serverKeyset.lweBootstrapKeys[i];
      auto info = bsk.getInfo().asReader();

      size_t decomposition_level_count = info.getParams().getLevelCount();
      size_t decomposition_base_log = info.getParams().getBaseLog();
      size_t glwe_dimension = info.getParams().getGlweDimension();
      size_t polynomial_size = info.getParams().getPolynomialSize();
      size_t input_lwe_dimension = info.getParams().getInputLweDimension();

      // Create the FFT
      FFT fft(polynomial_size);

      // Allocate scratch for key conversion
      size_t scratch_size;
      size_t scratch_align;
      concrete_cpu_bootstrap_key_convert_u64_to_fourier_scratch(
          &scratch_size, &scratch_align, fft.fft);
      auto scratch = (uint8_t *)aligned_alloc(scratch_align, scratch_size);

      // Allocate the fourier_bootstrap_key
      auto &bsk_buffer = bsk.getBuffer();
      auto fourier_data = std::make_shared<std::vector<std::complex<double>>>();
      fourier_data->resize(bsk_buffer.size() / 2);
      auto bsk_data = bsk_buffer.data();

      // Convert bootstrap_key to the fourier domain
      concrete_cpu_bootstrap_key_convert_u64_to_fourier(
          bsk_data, fourier_data->data(), decomposition_level_count,
          decomposition_base_log, glwe_dimension, polynomial_size,
          input_lwe_dimension, fft.fft, scratch, scratch_size);

      // Store the fourier_bootstrap_key in the context
      fourier_bootstrap_keys.push_back(fourier_data);
      ffts.push_back(std::move(fft));
      free(scratch);
    }

#ifdef CONCRETELANG_CUDA_SUPPORT
    assert(cudaGetDeviceCount(&num_devices) == cudaSuccess);
    bsk_gpu.resize(num_devices, nullptr);
    ksk_gpu.resize(num_devices, nullptr);
    for (int i = 0; i < num_devices; ++i) {
      bsk_gpu_mutex.push_back(std::make_unique<std::mutex>());
      ksk_gpu_mutex.push_back(std::make_unique<std::mutex>());
    }
#endif
  }
}

#ifndef CONCRETELANG_DATAFLOW_EXECUTION_ENABLED
const uint64_t *RuntimeContext::keyswitch_key_buffer(size_t keyId) {
  return serverKeyset.lweKeyswitchKeys[keyId].getBuffer().data();
}

const std::complex<double> *
RuntimeContext::fourier_bootstrap_key_buffer(size_t keyId) {
  return fourier_bootstrap_keys[keyId]->data();
}

const uint64_t *RuntimeContext::fp_keyswitch_key_buffer(size_t keyId) {
  return serverKeyset.packingKeyswitchKeys[keyId].getRawPtr();
}

const struct Fft *RuntimeContext::fft(size_t keyId) { return ffts[keyId].fft; }
#else
const uint64_t *RuntimeContext::keyswitch_key_buffer(size_t keyId) {
  if (dfr::_dfr_is_root_node())
    return serverKeyset.lweKeyswitchKeys[keyId].getBuffer().data();

  std::lock_guard<std::mutex> guard(dc.cm_guard);
  if (dc.ksks.find(keyId) == dc.ksks.end()) {
    _dfr_get_ksk getKskAction;
    dfr::KeyWrapper<LweKeyswitchKey> kskw =
        getKskAction(hpx::find_root_locality(), keyId);
    dc.ksks.insert(std::pair<size_t, LweKeyswitchKey>(keyId, kskw.keys[0]));
  }
  auto it = dc.ksks.find(keyId);
  assert(it != dc.ksks.end());
  return it->second.getBuffer().data();
}

void RuntimeContext::getBSKonNode(size_t keyId) {
  assert(dc.fbks.find(keyId) == dc.fbks.end());
  assert(dc.ffts.find(keyId) == dc.ffts.end());
  _dfr_get_bsk getBskAction;
  dfr::KeyWrapper<LweBootstrapKey> bskw =
      getBskAction(hpx::find_root_locality(), keyId);

  auto bsk = bskw.keys[0];
  auto info = bsk.getInfo().asReader();

  size_t decomposition_level_count = info.getParams().getLevelCount();
  size_t decomposition_base_log = info.getParams().getBaseLog();
  size_t glwe_dimension = info.getParams().getGlweDimension();
  size_t polynomial_size = info.getParams().getPolynomialSize();
  size_t input_lwe_dimension = info.getParams().getInputLweDimension();

  // Create the FFT
  FFT fft(polynomial_size);

  // Allocate scratch for key conversion
  size_t scratch_size;
  size_t scratch_align;
  concrete_cpu_bootstrap_key_convert_u64_to_fourier_scratch(
      &scratch_size, &scratch_align, fft.fft);
  auto scratch = (uint8_t *)aligned_alloc(scratch_align, scratch_size);

  // Allocate the fourier_bootstrap_key
  auto &bsk_buffer = bsk.getBuffer();
  auto fourier_data = std::make_shared<std::vector<std::complex<double>>>();
  fourier_data->resize(bsk_buffer.size() / 2);
  auto bsk_data = bsk_buffer.data();

  // Convert bootstrap_key to the fourier domain
  concrete_cpu_bootstrap_key_convert_u64_to_fourier(
      bsk_data, fourier_data->data(), decomposition_level_count,
      decomposition_base_log, glwe_dimension, polynomial_size,
      input_lwe_dimension, fft.fft, scratch, scratch_size);

  // Store the fourier_bootstrap_key in the context
  dc.fbks.insert(
      std::pair<size_t, std::shared_ptr<std::vector<std::complex<double>>>>(
          keyId, fourier_data));
  dc.ffts.insert(std::pair<size_t, FFT>(keyId, std::move(fft)));
  free(scratch);
}

const std::complex<double> *
RuntimeContext::fourier_bootstrap_key_buffer(size_t keyId) {
  if (dfr::_dfr_is_root_node())
    return fourier_bootstrap_keys[keyId]->data();

  std::lock_guard<std::mutex> guard(dc.cm_guard);
  if (dc.fbks.find(keyId) == dc.fbks.end())
    getBSKonNode(keyId);
  auto it = dc.fbks.find(keyId);
  assert(it != dc.fbks.end());
  return it->second->data();
}

const uint64_t *RuntimeContext::fp_keyswitch_key_buffer(size_t keyId) {
  if (dfr::_dfr_is_root_node())
    return serverKeyset.packingKeyswitchKeys[keyId].getRawPtr();

  std::lock_guard<std::mutex> guard(dc.cm_guard);
  if (dc.ksks.find(keyId) == dc.ksks.end()) {
    _dfr_get_pksk getPKskAction;
    dfr::KeyWrapper<PackingKeyswitchKey> pkskw =
        getPKskAction(hpx::find_root_locality(), keyId);
    dc.pksks.insert(
        std::pair<size_t, PackingKeyswitchKey>(keyId, pkskw.keys[0]));
  }
  auto it = dc.pksks.find(keyId);
  assert(it != dc.pksks.end());
  return it->second.getRawPtr();
}

const struct Fft *RuntimeContext::fft(size_t keyId) {
  if (dfr::_dfr_is_root_node())
    return ffts[keyId].fft;

  std::lock_guard<std::mutex> guard(dc.cm_guard);
  if (dc.ffts.find(keyId) == dc.ffts.end())
    getBSKonNode(keyId);
  auto it = dc.ffts.find(keyId);
  assert(it != dc.ffts.end());
  return it->second.fft;
}
#endif

} // namespace concretelang
} // namespace mlir

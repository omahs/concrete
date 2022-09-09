// Part of the Concrete Compiler Project, under the BSD3 License with Zama
// Exceptions. See
// https://github.com/zama-ai/concrete-compiler-internal/blob/main/LICENSE.txt
// for license information.

#include "CompilerAPIModule.h"
#include "concretelang-c/Support/CompilerEngine.h"
#include "concretelang/Dialect/FHE/IR/FHEOpsDialect.h.inc"
#include "concretelang/Support/JITSupport.h"
#include "concretelang/Support/Jit.h"
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/MemRef/IR/MemRef.h>
#include <mlir/ExecutionEngine/OptUtils.h>

#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h>
#include <stdexcept>
#include <string>

using mlir::concretelang::CompilationOptions;
using mlir::concretelang::JITSupport;
using mlir::concretelang::LambdaArgument;

/// Populate the compiler API python module.
void mlir::concretelang::python::populateCompilerAPISubmodule(
    pybind11::module &m) {
  m.doc() = "Concretelang compiler python API";

  m.def("round_trip",
        [](std::string mlir_input) { return roundTrip(mlir_input.c_str()); });

  m.def("terminate_parallelization", &terminateParallelization);

  pybind11::class_<CompilationOptions>(m, "CompilationOptions")
      .def(pybind11::init(
          [](std::string funcname) { return CompilationOptions(funcname); }))
      .def("set_funcname",
           [](CompilationOptions &options, std::string funcname) {
             options.clientParametersFuncName = funcname;
           })
      .def("set_verify_diagnostics",
           [](CompilationOptions &options, bool b) {
             options.verifyDiagnostics = b;
           })
      .def("set_auto_parallelize", [](CompilationOptions &options,
                                      bool b) { options.autoParallelize = b; })
      .def("set_loop_parallelize", [](CompilationOptions &options,
                                      bool b) { options.loopParallelize = b; })
      .def("set_dataflow_parallelize",
           [](CompilationOptions &options, bool b) {
             options.dataflowParallelize = b;
           })
      .def("set_optimize_concrete",
           [](CompilationOptions &options, bool b) {
             options.optimizeConcrete = b;
           })
      .def("set_p_error",
           [](CompilationOptions &options, double p_error) {
             options.optimizerConfig.p_error = p_error;
           })
      .def("set_display_optimizer_choice",
           [](CompilationOptions &options, bool display) {
             options.optimizerConfig.display = display;
           })
      .def("set_strategy_v0",
           [](CompilationOptions &options, bool strategy_v0) {
             options.optimizerConfig.strategy_v0 = strategy_v0;
           })
      .def("set_global_p_error",
           [](CompilationOptions &options, double global_p_error) {
             options.optimizerConfig.global_p_error = global_p_error;
           });

  pybind11::class_<mlir::concretelang::CompilationFeedback>(
      m, "CompilationFeedback")
      .def_readonly("complexity",
                    &mlir::concretelang::CompilationFeedback::complexity)
      .def_readonly(
          "total_secret_keys_size",
          &mlir::concretelang::CompilationFeedback::totalSecretKeysSize)
      .def_readonly(
          "total_bootstrap_keys_size",
          &mlir::concretelang::CompilationFeedback::totalBootstrapKeysSize)
      .def_readonly(
          "total_keyswitch_keys_size",
          &mlir::concretelang::CompilationFeedback::totalKeyswitchKeysSize)
      .def_readonly("total_inputs_size",
                    &mlir::concretelang::CompilationFeedback::totalInputsSize)
      .def_readonly("total_output_size",
                    &mlir::concretelang::CompilationFeedback::totalOutputsSize);

  pybind11::class_<mlir::concretelang::JitCompilationResult>(
      m, "JITCompilationResult");
  pybind11::class_<mlir::concretelang::JITLambda,
                   std::shared_ptr<mlir::concretelang::JITLambda>>(m,
                                                                   "JITLambda");
  pybind11::class_<JITSupport_C>(m, "JITSupport")
      .def(pybind11::init([](std::string runtimeLibPath) {
        return jit_support(runtimeLibPath);
      }))
      .def("compile",
           [](JITSupport_C &support, std::string mlir_program,
              CompilationOptions options) {
             return jit_compile(support, mlir_program.c_str(), options);
           })
      .def("load_client_parameters",
           [](JITSupport_C &support,
              mlir::concretelang::JitCompilationResult &result) {
             return jit_load_client_parameters(support, result);
           })
      .def("load_compilation_feedback",
           [](JITSupport_C &support,
              mlir::concretelang::JitCompilationResult &result) {
             return jit_load_compilation_feedback(support, result);
           })
      .def(
          "load_server_lambda",
          [](JITSupport_C &support,
             mlir::concretelang::JitCompilationResult &result) {
            return jit_load_server_lambda(support, result);
          },
          pybind11::return_value_policy::reference)
      .def("server_call",
           [](JITSupport_C &support, concretelang::JITLambda &lambda,
              clientlib::PublicArguments &publicArguments,
              clientlib::EvaluationKeys &evaluationKeys) {
             return jit_server_call(support, lambda, publicArguments,
                                    evaluationKeys);
           });

  pybind11::class_<mlir::concretelang::LibraryCompilationResult>(
      m, "LibraryCompilationResult")
      .def(pybind11::init([](std::string outputDirPath, std::string funcname) {
        return mlir::concretelang::LibraryCompilationResult{
            outputDirPath,
            funcname,
        };
      }));
  pybind11::class_<concretelang::serverlib::ServerLambda>(m, "LibraryLambda");
  pybind11::class_<LibrarySupport_C>(m, "LibrarySupport")
      .def(pybind11::init(
          [](std::string outputPath, std::string runtimeLibraryPath,
             bool generateSharedLib, bool generateStaticLib,
             bool generateClientParameters, bool generateCompilationFeedback,
             bool generateCppHeader) {
            return library_support(
                outputPath.c_str(), runtimeLibraryPath.c_str(),
                generateSharedLib, generateStaticLib, generateClientParameters,
                generateCompilationFeedback, generateCppHeader);
          }))
      .def("compile",
           [](LibrarySupport_C &support, std::string mlir_program,
              mlir::concretelang::CompilationOptions options) {
             return library_compile(support, mlir_program.c_str(), options);
           })
      .def("load_client_parameters",
           [](LibrarySupport_C &support,
              mlir::concretelang::LibraryCompilationResult &result) {
             return library_load_client_parameters(support, result);
           })
      .def("load_compilation_feedback",
           [](LibrarySupport_C &support,
              mlir::concretelang::LibraryCompilationResult &result) {
             return library_load_compilation_feedback(support, result);
           })
      .def(
          "load_server_lambda",
          [](LibrarySupport_C &support,
             mlir::concretelang::LibraryCompilationResult &result) {
            return library_load_server_lambda(support, result);
          },
          pybind11::return_value_policy::reference)
      .def("server_call",
           [](LibrarySupport_C &support, serverlib::ServerLambda lambda,
              clientlib::PublicArguments &publicArguments,
              clientlib::EvaluationKeys &evaluationKeys) {
             return library_server_call(support, lambda, publicArguments,
                                        evaluationKeys);
           })
      .def("get_shared_lib_path",
           [](LibrarySupport_C &support) {
             return library_get_shared_lib_path(support);
           })
      .def("get_client_parameters_path", [](LibrarySupport_C &support) {
        return library_get_client_parameters_path(support);
      });

  class ClientSupport {};
  pybind11::class_<ClientSupport>(m, "ClientSupport")
      .def(pybind11::init())
      .def_static(
          "key_set",
          [](clientlib::ClientParameters clientParameters,
             clientlib::KeySetCache *cache) {
            auto optCache =
                cache == nullptr
                    ? llvm::None
                    : llvm::Optional<clientlib::KeySetCache>(*cache);
            return key_set(clientParameters, optCache);
          },
          pybind11::arg().none(false), pybind11::arg().none(true))
      .def_static("encrypt_arguments",
                  [](clientlib::ClientParameters clientParameters,
                     clientlib::KeySet &keySet,
                     std::vector<lambdaArgument> args) {
                    std::vector<mlir::concretelang::LambdaArgument *> argsRef;
                    for (auto i = 0u; i < args.size(); i++) {
                      argsRef.push_back(args[i].ptr.get());
                    }
                    return encrypt_arguments(clientParameters, keySet, argsRef);
                  })
      .def_static("decrypt_result", [](clientlib::KeySet &keySet,
                                       clientlib::PublicResult &publicResult) {
        return decrypt_result(keySet, publicResult);
      });
  pybind11::class_<clientlib::KeySetCache>(m, "KeySetCache")
      .def(pybind11::init<std::string &>());

  pybind11::class_<mlir::concretelang::ClientParameters>(m, "ClientParameters")
      .def_static("unserialize",
                  [](const pybind11::bytes &buffer) {
                    return clientParametersUnserialize(buffer);
                  })
      .def("serialize",
           [](mlir::concretelang::ClientParameters &clientParameters) {
             return pybind11::bytes(
                 clientParametersSerialize(clientParameters));
           });

  pybind11::class_<clientlib::KeySet>(m, "KeySet")
      .def("get_evaluation_keys",
           [](clientlib::KeySet &keySet) { return keySet.evaluationKeys(); });

  pybind11::class_<clientlib::PublicArguments,
                   std::unique_ptr<clientlib::PublicArguments>>(
      m, "PublicArguments")
      .def_static("unserialize",
                  [](mlir::concretelang::ClientParameters &clientParameters,
                     const pybind11::bytes &buffer) {
                    return publicArgumentsUnserialize(clientParameters, buffer);
                  })
      .def("serialize", [](clientlib::PublicArguments &publicArgument) {
        return pybind11::bytes(publicArgumentsSerialize(publicArgument));
      });
  pybind11::class_<clientlib::PublicResult>(m, "PublicResult")
      .def_static("unserialize",
                  [](mlir::concretelang::ClientParameters &clientParameters,
                     const pybind11::bytes &buffer) {
                    return publicResultUnserialize(clientParameters, buffer);
                  })
      .def("serialize", [](clientlib::PublicResult &publicResult) {
        return pybind11::bytes(publicResultSerialize(publicResult));
      });

  pybind11::class_<clientlib::EvaluationKeys>(m, "EvaluationKeys")
      .def_static("unserialize",
                  [](const pybind11::bytes &buffer) {
                    return evaluationKeysUnserialize(buffer);
                  })
      .def("serialize", [](clientlib::EvaluationKeys &evaluationKeys) {
        return pybind11::bytes(evaluationKeysSerialize(evaluationKeys));
      });

  pybind11::class_<lambdaArgument>(m, "LambdaArgument")
      .def_static("from_tensor_8",
                  [](std::vector<uint8_t> tensor, std::vector<int64_t> dims) {
                    return lambdaArgumentFromTensorU8(tensor, dims);
                  })
      .def_static("from_tensor_16",
                  [](std::vector<uint16_t> tensor, std::vector<int64_t> dims) {
                    return lambdaArgumentFromTensorU16(tensor, dims);
                  })
      .def_static("from_tensor_32",
                  [](std::vector<uint32_t> tensor, std::vector<int64_t> dims) {
                    return lambdaArgumentFromTensorU32(tensor, dims);
                  })
      .def_static("from_tensor_64",
                  [](std::vector<uint64_t> tensor, std::vector<int64_t> dims) {
                    return lambdaArgumentFromTensorU64(tensor, dims);
                  })
      .def_static("from_scalar", lambdaArgumentFromScalar)
      .def("is_tensor",
           [](lambdaArgument &lambda_arg) {
             return lambdaArgumentIsTensor(lambda_arg);
           })
      .def("get_tensor_data",
           [](lambdaArgument &lambda_arg) {
             return lambdaArgumentGetTensorData(lambda_arg);
           })
      .def("get_tensor_shape",
           [](lambdaArgument &lambda_arg) {
             return lambdaArgumentGetTensorDimensions(lambda_arg);
           })
      .def("is_scalar",
           [](lambdaArgument &lambda_arg) {
             return lambdaArgumentIsScalar(lambda_arg);
           })
      .def("get_scalar", [](lambdaArgument &lambda_arg) {
        return lambdaArgumentGetScalar(lambda_arg);
      });
}

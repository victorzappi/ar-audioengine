#pragma once

#include "IOTensor.hpp"
#include "SampleApp.hpp"
#include "TensorTools.h"

namespace qnn
{
  namespace tools
  {
    enum class StatusCode
    {
      SUCCESS,
      FAILURE,
      FAILURE_SYSTEM_ERROR,
      FAILURE_SYSTEM_COMMUNICATION_ERROR,
      QNN_FEATURE_UNSUPPORTED
    };

    /**
     * QnnModel provides an API on top of the QNN SDK to easily load models from Qualcomm's DLC format
     * and execute them within this audio engine.
     */
    class QnnModel
    {
    public:
      QnnModel(
          std::string backendPath,
          std::string dlcPath,
          std::string systemLibraryPath);

      ~QnnModel();

      // Load the model DLC, backend library, and system library
      // and prepare the model for execution
      StatusCode setup();

      // Read-only view on a graph's input tensors
      const std::vector<iotensor::TensorWrapper> &getInputTensors(int graphIdx) const;

      // Read-only view on a graph's input tensors
      const std::vector<iotensor::TensorWrapper> &getOutputTensors(int graphIdx) const;

      // Execute the given graph with input values from `inputs`, writing model outputs to `outputs`
      StatusCode executeGraph(int graphIdx, float **inputs, float **outputs);

      // Free any resources used by the model, e.g. backend library handles
      StatusCode cleanup();

      uint32_t getNumGraphs() const;

    private:
      StatusCode loadQnnFunctionPointers();

      StatusCode initializeBackendLogging();

      StatusCode initializeBackend();

      StatusCode createContext();

      StatusCode composeGraphs();

      StatusCode finalizeGraphs();

      StatusCode setupIOTensors();

      StatusCode freeContext();

      StatusCode terminateBackend();

      bool isDevicePropertySupported();

      StatusCode createDevice();

      StatusCode freeDevice();

      StatusCode verifyFailReturnStatus(Qnn_ErrorHandle_t errCode);

      std::string m_dlcPath;
      std::string m_backendPath;
      std::string m_systemLibraryPath;

      iotensor::IOTensor m_ioTensor;

      // QNN backend/context members
      sample_app::QnnFunctionPointers m_qnnFunctionPointers;
      QnnBackend_Config_t **m_backendConfig = nullptr;
      Qnn_ContextHandle_t m_context = nullptr;
      QnnContext_Config_t **m_contextConfig = nullptr;
      bool m_isBackendInitialized;
      bool m_isContextCreated;

      // Graph-related members
      qnn_wrapper_api::GraphInfo_t **m_graphsInfo = nullptr;
      uint32_t m_graphsCount;
      qnn_wrapper_api::GraphConfigInfo_t **m_graphConfigsInfo = nullptr;
      uint32_t m_graphConfigsInfoCount;

      // Handles on external resources
      Qnn_ProfileHandle_t m_profileBackendHandle = nullptr;
      Qnn_LogHandle_t m_logHandle = nullptr;
      Qnn_BackendHandle_t m_backendHandle = nullptr;
      Qnn_DeviceHandle_t m_deviceHandle = nullptr;
      QnnSystemDlc_Handle_t m_dlcHandle = nullptr;
      Qnn_LogHandle_t m_dlcLogHandle = nullptr;
      void *m_backendLibHandle = nullptr;

      // IO tensors by graph
      // Vector of input/output tensors for each graph
      std::vector<std::vector<iotensor::TensorWrapper>> m_inputTensors;
      std::vector<std::vector<iotensor::TensorWrapper>> m_outputTensors;

      // Underlying Qnn_Tensor_t structs to be passed to executeGraphs()
      // std::vector<Qnn_Tensor_t *> m_graphInputTensors;
      // std::vector<Qnn_Tensor_t *> m_graphOutputTensors;
    };
  } // namespace tools
} // namespace qnn

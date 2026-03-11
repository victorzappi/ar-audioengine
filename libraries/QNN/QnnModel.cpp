#include <inttypes.h>
#include <cstring>
#include <fstream>
#include <iostream>

// QNN SDK
#include <Logger.hpp>
#include <PAL/DynamicLoading.hpp>
#include <QnnDlcUtils.hpp>
#include <QnnWrapperUtils.hpp>
#include <DynamicLoadUtil.hpp>
#include <QnnTypeMacros.hpp>

// QNN library
#include "TensorTools.h"
#include "QnnModel.h"

using namespace qnn;
using namespace qnn::tools;

QnnModel::QnnModel(
    std::string backendPath,
    std::string dlcPath,
    std::string systemLibraryPath)
    : m_backendPath(backendPath),
      m_dlcPath(dlcPath),
      m_systemLibraryPath(systemLibraryPath),
      m_isBackendInitialized(false),
      m_isContextCreated(false)
{
}

QnnModel::~QnnModel()
{
  cleanup();
}

StatusCode QnnModel::setup()
{
  if (StatusCode::SUCCESS != loadQnnFunctionPointers())
  {
    QNN_ERROR("Load QNN function pointers failure");
    return StatusCode::FAILURE;
  }

  if (StatusCode::SUCCESS != initializeBackendLogging())
  {
    QNN_ERROR("Initialize backend logging failure");
    return StatusCode::FAILURE;
  }

  if (StatusCode::SUCCESS != initializeBackend())
  {
    QNN_ERROR("Initialize backend failure");
    return StatusCode::FAILURE;
  }

  if (StatusCode::SUCCESS != createContext())
  {
    QNN_ERROR("Create context failure");
    return StatusCode::FAILURE;
  }

  if (StatusCode::SUCCESS != composeGraphs())
  {
    QNN_ERROR("Compose graphs failure");
    return StatusCode::FAILURE;
  }

  if (StatusCode::SUCCESS != finalizeGraphs())
  {
    QNN_ERROR("Finalize graphs failure");
    return StatusCode::FAILURE;
  }

  if (StatusCode::SUCCESS != setupIOTensors())
  {
    QNN_ERROR("Set up IO tensors failure");
    return StatusCode::FAILURE;
  }

  return StatusCode::SUCCESS;
}

StatusCode QnnModel::executeGraph(int graphIdx, float **inputs, float **outputs)
{
  auto graphInfo = (*m_graphsInfo)[graphIdx];
  uint32_t numInputTensors = graphInfo.numInputTensors;
  uint32_t numOutputTensors = graphInfo.numOutputTensors;

  for (int i = 0; i < numInputTensors; i++)
  {
    if (iotensor::StatusCode::SUCCESS != iotensor::copyBufferToInputTensor(inputs[i], &m_inputTensors[graphIdx][i]))
    {
      QNN_ERROR("Copy Input Data failure");
      return StatusCode::FAILURE;
    }
  }

  Qnn_ErrorHandle_t executeStatus = m_qnnFunctionPointers.qnnInterface.graphExecute(graphInfo.graph,
                                                                                    graphInfo.inputTensors,
                                                                                    numInputTensors,
                                                                                    graphInfo.outputTensors,
                                                                                    numOutputTensors,
                                                                                    m_profileBackendHandle,
                                                                                    nullptr);
  if (QNN_GRAPH_NO_ERROR != executeStatus)
  {
    QNN_ERROR("Graph execution failed");
    return StatusCode::FAILURE;
  }

  for (int i = 0; i < numOutputTensors; i++)
  {
    if (iotensor::StatusCode::SUCCESS != iotensor::copyOutputTensorToBuffer(&m_outputTensors[graphIdx][i], outputs[i]))
    {
      QNN_ERROR("Copy Output Data failure");
      return StatusCode::FAILURE;
    }
  }

  return StatusCode::SUCCESS;
}

StatusCode QnnModel::cleanup()
{
  // Free underlying tensors and graph info
  if (m_graphsInfo != nullptr)
  {
    for (int g = 0; g < m_graphsCount; g++)
    {
      auto graphInfo = (*m_graphsInfo)[g];
      uint32_t numInputTensors = graphInfo.numInputTensors;
      uint32_t numOutputTensors = graphInfo.numOutputTensors;

      m_ioTensor.tearDownInputAndOutputTensors(
          graphInfo.inputTensors, graphInfo.outputTensors, graphInfo.numInputTensors, graphInfo.numOutputTensors);
    }

    qnn_wrapper_api::freeGraphsInfo(&m_graphsInfo, m_graphsCount);
    m_graphsInfo = nullptr;
  }

  if (m_isContextCreated && StatusCode::SUCCESS != freeContext())
  {
    QNN_ERROR("Context free failure");
    return StatusCode::FAILURE;
  }

  if (StatusCode::SUCCESS != terminateBackend())
  {
    QNN_ERROR("Terminate backend failure");
    return StatusCode::FAILURE;
  }

  sample_app::dlc_utils::freeDlcResources(m_qnnFunctionPointers.qnnSystemInterfaceHandle,
                                          m_dlcHandle,
                                          m_dlcLogHandle);

  if (m_backendLibHandle != nullptr)
  {
    pal::dynamicloading::dlClose(m_backendLibHandle);
    m_backendLibHandle = nullptr;
  }

  return StatusCode::SUCCESS;
}

const std::vector<iotensor::TensorWrapper> &QnnModel::getInputTensors(int graphIdx) const
{
  return m_inputTensors.at(graphIdx);
}

const std::vector<iotensor::TensorWrapper> &QnnModel::getOutputTensors(int graphIdx) const
{
  return m_outputTensors.at(graphIdx);
}

uint32_t QnnModel::getNumGraphs() const
{
  return m_graphsCount;
}

/**
 * ======================================
 * PRIVATE
 * ======================================
 */

// Load QNN function pointers from the provided backend
StatusCode QnnModel::loadQnnFunctionPointers()
{
  // QnnModel only supports loading from DLC: provide no model path
  // Provided model handle is nullptr, but it won't be used since loadModel is false
  auto statusCode = dynamicloadutil::getQnnFunctionPointers(
      m_backendPath, "", &m_qnnFunctionPointers, &m_backendLibHandle, false, nullptr);

  if (dynamicloadutil::StatusCode::SUCCESS != statusCode)
  {
    if (dynamicloadutil::StatusCode::FAIL_LOAD_BACKEND == statusCode)
    {
      QNN_ERROR("Error initializing QNN Function Pointers: could not load backend: %s", m_backendPath.c_str());
    }
    else
    {
      QNN_ERROR("Error initializing QNN Function Pointers");
    }
    return StatusCode::FAILURE;
  }

  // Also load in system library function pointers for loading DLC
  statusCode =
      dynamicloadutil::getQnnSystemFunctionPointers(m_systemLibraryPath, &m_qnnFunctionPointers);
  if (dynamicloadutil::StatusCode::SUCCESS != statusCode)
  {
    QNN_ERROR("Error initializing QNN System Function Pointers");
    return StatusCode::FAILURE;
  }

  return StatusCode::SUCCESS;
}

StatusCode QnnModel::initializeBackendLogging()
{
  // initialize logging in the backend
  if (log::isLogInitialized())
  {
    auto logCallback = log::getLogCallback();
    auto logLevel = log::getLogLevel();
    QNN_INFO("Initializing logging in the backend. Callback: [%p], Log Level: [%d]",
             logCallback,
             logLevel);
    if (QNN_SUCCESS !=
        m_qnnFunctionPointers.qnnInterface.logCreate(logCallback, logLevel, &m_logHandle))
    {
      QNN_WARN("Unable to initialize logging in the backend.");
    }
  }
  else
  {
    QNN_WARN("Logging not available in the backend.");
  }
  return StatusCode::SUCCESS;
}

// Initialize a QnnBackend.
StatusCode QnnModel::initializeBackend()
{
  auto qnnStatus = m_qnnFunctionPointers.qnnInterface.backendCreate(
      m_logHandle, (const QnnBackend_Config_t **)m_backendConfig, &m_backendHandle);
  if (QNN_BACKEND_NO_ERROR != qnnStatus)
  {
    QNN_ERROR("Could not initialize backend due to error = %d", qnnStatus);
    return StatusCode::FAILURE;
  }
  QNN_INFO("Initialize Backend Returned Status = %d", qnnStatus);
  m_isBackendInitialized = true;
  return StatusCode::SUCCESS;
}

// Terminate the backend after done.
StatusCode QnnModel::terminateBackend()
{
  // Free context if not already done
  if (m_isContextCreated)
  {
    if (QNN_CONTEXT_NO_ERROR !=
        m_qnnFunctionPointers.qnnInterface.contextFree(m_context, nullptr))
    {
      QNN_ERROR("Could not free context");
      return StatusCode::FAILURE;
    }
    m_isContextCreated = false;
  }

  // Terminate backend
  if (m_isBackendInitialized && nullptr != m_qnnFunctionPointers.qnnInterface.backendFree)
  {
    if (QNN_BACKEND_NO_ERROR != m_qnnFunctionPointers.qnnInterface.backendFree(m_backendHandle))
    {
      QNN_ERROR("Could not free backend");
      return StatusCode::FAILURE;
    }
    m_isBackendInitialized = false;
  }

  // Terminate logging in the backend
  if (nullptr != m_qnnFunctionPointers.qnnInterface.logFree && nullptr != m_logHandle)
  {
    if (QNN_SUCCESS != m_qnnFunctionPointers.qnnInterface.logFree(m_logHandle))
    {
      QNN_WARN("Unable to terminate logging in the backend.");
      return StatusCode::FAILURE;
    }
    m_logHandle = nullptr;
  }

  return StatusCode::SUCCESS;
}

// Create a Context in a backend.
StatusCode QnnModel::createContext()
{
  if (QNN_CONTEXT_NO_ERROR !=
      m_qnnFunctionPointers.qnnInterface.contextCreate(m_backendHandle,
                                                       m_deviceHandle,
                                                       (const QnnContext_Config_t **)m_contextConfig,
                                                       &m_context))
  {
    QNN_ERROR("Could not create context");
    return StatusCode::FAILURE;
  }
  m_isContextCreated = true;
  return StatusCode::SUCCESS;
}

// Free context after done.
StatusCode QnnModel::freeContext()
{
  // clear graph info first
  if (m_graphsInfo)
  {
    for (uint32_t gIdx = 0; gIdx < m_graphsCount; gIdx++)
    {
      if (m_graphsInfo[gIdx])
      {
        if (nullptr != m_graphsInfo[gIdx]->graphName)
        {
          free(m_graphsInfo[gIdx]->graphName);
          m_graphsInfo[gIdx]->graphName = nullptr;
        }
        qnn_wrapper_api::freeQnnTensors(m_graphsInfo[gIdx]->inputTensors,
                                        m_graphsInfo[gIdx]->numInputTensors);
        qnn_wrapper_api::freeQnnTensors(m_graphsInfo[gIdx]->outputTensors,
                                        m_graphsInfo[gIdx]->numOutputTensors);
      }
    }
    free(*m_graphsInfo);
  }
  free(m_graphsInfo);
  m_graphsInfo = nullptr;

  if (QNN_CONTEXT_NO_ERROR !=
      m_qnnFunctionPointers.qnnInterface.contextFree(m_context, m_profileBackendHandle))
  {
    QNN_ERROR("Could not free context");
    return StatusCode::FAILURE;
  }
  m_isContextCreated = false;
  return StatusCode::SUCCESS;
}

// Compose a graph by populating graph related
// information in m_graphsInfo and m_graphsCount.
StatusCode QnnModel::composeGraphs()
{
  QNN_INFO("Composing graphs from DLC");

  // Create DLC handle using utility function
  auto dlcStatus = sample_app::dlc_utils::createDlcHandle(m_qnnFunctionPointers.qnnSystemInterfaceHandle,
                                                          m_dlcPath,
                                                          log::getLogCallback(),
                                                          log::getLogLevel(),
                                                          m_dlcLogHandle,
                                                          m_dlcHandle);

  if (sample_app::dlc_utils::StatusCode::SUCCESS != dlcStatus)
  {
    QNN_ERROR("Failed to create DLC handle");
    return StatusCode::FAILURE;
  }

  // Compose graphs from DLC using utility function
  dlcStatus = sample_app::dlc_utils::composeGraphsFromDlc(m_qnnFunctionPointers.qnnSystemInterfaceHandle,
                                                          m_dlcHandle,
                                                          m_backendHandle,
                                                          m_context,
                                                          m_qnnFunctionPointers.qnnInterfaceHandle,
                                                          m_graphsInfo,
                                                          m_graphsCount);

  if (sample_app::dlc_utils::StatusCode::SUCCESS != dlcStatus)
  {
    QNN_ERROR("Failed to compose graphs from DLC");
    return StatusCode::FAILURE;
  }

  QNN_INFO("Successfully composed %d graphs from DLC", m_graphsCount);
  return StatusCode::SUCCESS;
}

StatusCode QnnModel::finalizeGraphs()
{
  for (size_t graphIdx = 0; graphIdx < m_graphsCount; graphIdx++)
  {
    if (QNN_GRAPH_NO_ERROR !=
        m_qnnFunctionPointers.qnnInterface.graphFinalize(
            (*m_graphsInfo)[graphIdx].graph, m_profileBackendHandle, nullptr))
    {
      return StatusCode::FAILURE;
    }
  }
  return StatusCode::SUCCESS;
}

StatusCode QnnModel::setupIOTensors()
{
  // m_graphInputTensors.resize(m_graphsCount);
  // m_graphOutputTensors.resize(m_graphsCount);
  m_inputTensors.resize(m_graphsCount);
  m_outputTensors.resize(m_graphsCount);

  for (int graphIdx = 0; graphIdx < m_graphsCount; graphIdx++)
  {
    Qnn_Tensor_t *graphInputs = nullptr;
    Qnn_Tensor_t *graphOutputs = nullptr;

    const auto graphInfo = (*m_graphsInfo)[graphIdx];

    if (iotensor::StatusCode::SUCCESS !=
        m_ioTensor.setupInputAndOutputTensors(&graphInputs, &graphOutputs, graphInfo))
    {
      QNN_ERROR("Error in setting up Input and output Tensors for graphIdx: %d", graphIdx);
      return StatusCode::FAILURE;
    }

    // m_graphInputTensors[graphIdx] = graphInputs;
    // m_graphOutputTensors[graphIdx] = graphOutputs;

    const uint32_t numInputs = graphInfo.numInputTensors;
    const uint32_t numOutputs = graphInfo.numOutputTensors;
    m_inputTensors[graphIdx].resize(numInputs);
    m_outputTensors[graphIdx].resize(numOutputs);

    for (int i = 0; i < numInputs; i++)
    {
      m_inputTensors[graphIdx][i] = iotensor::createTensorWrapper(&graphInputs[i]);
    }
    for (int i = 0; i < numOutputs; i++)
    {
      m_outputTensors[graphIdx][i] = iotensor::createTensorWrapper(&graphOutputs[i]);
    }
  }
  return StatusCode::SUCCESS;
}

StatusCode QnnModel::verifyFailReturnStatus(Qnn_ErrorHandle_t errCode)
{
  auto returnStatus = StatusCode::FAILURE;
  switch (errCode)
  {
  case QNN_COMMON_ERROR_SYSTEM_COMMUNICATION:
    returnStatus = StatusCode::FAILURE_SYSTEM_COMMUNICATION_ERROR;
    break;
  case QNN_COMMON_ERROR_SYSTEM:
    returnStatus = StatusCode::FAILURE_SYSTEM_ERROR;
    break;
  case QNN_COMMON_ERROR_NOT_SUPPORTED:
    returnStatus = StatusCode::QNN_FEATURE_UNSUPPORTED;
    break;
  default:
    break;
  }
  return returnStatus;
}

bool QnnModel::isDevicePropertySupported()
{
  if (nullptr == m_qnnFunctionPointers.qnnInterface.propertyHasCapability)
  {
    QNN_WARN("Function pointers not loaded: unable to determine if device property supported");
    return false;
  }

  auto supportedStatus = m_qnnFunctionPointers.qnnInterface.propertyHasCapability(QNN_PROPERTY_GROUP_DEVICE);
  if (QNN_PROPERTY_SUPPORTED == supportedStatus)
  {
    return true;
  }
  else
  {
    QNN_WARN("Device proprety is not supported");
    return false;
  }
}

StatusCode QnnModel::createDevice()
{
  if (nullptr != m_qnnFunctionPointers.qnnInterface.deviceCreate)
  {
    auto qnnStatus =
        m_qnnFunctionPointers.qnnInterface.deviceCreate(m_logHandle, nullptr, &m_deviceHandle);
    if (QNN_SUCCESS != qnnStatus && QNN_DEVICE_ERROR_UNSUPPORTED_FEATURE != qnnStatus)
    {
      QNN_ERROR("Failed to create device");
      return verifyFailReturnStatus(qnnStatus);
    }
  }
  return StatusCode::SUCCESS;
}

StatusCode QnnModel::freeDevice()
{
  if (nullptr != m_qnnFunctionPointers.qnnInterface.deviceFree)
  {
    auto qnnStatus = m_qnnFunctionPointers.qnnInterface.deviceFree(m_deviceHandle);
    if (QNN_SUCCESS != qnnStatus && QNN_DEVICE_ERROR_UNSUPPORTED_FEATURE != qnnStatus)
    {
      QNN_ERROR("Failed to free device");
      return verifyFailReturnStatus(qnnStatus);
    }
  }
  return StatusCode::SUCCESS;
}

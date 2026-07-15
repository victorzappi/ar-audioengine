/*
 * Copyright 2026 Victor Zappi, Avery Huang
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

// Original clean-room implementation written against the public QNN API only
// (QnnInterface / QnnSystemInterface, QnnSystemDlc, QnnContext, QnnGraph,
// QnnTensor, QnnTypes, QnnLog) plus <dlfcn.h>. It does not use or derive from
// Qualcomm's SDK sample sources (IOTensor, SampleApp, qnn_wrapper_api, PAL, etc.).

#include "DlcModel.h"

#include <dlfcn.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "QnnInterface.h"
#include "QnnTypes.h"
#include "QnnLog.h"
#include "System/QnnSystemInterface.h"
#include "System/QnnSystemContext.h"
#include "System/QnnSystemDlc.h"

namespace ar
{
    namespace qnn
    {
        namespace
        {
            // ----- logging -----------------------------------------------------

            void logMsg(const char *fmt, ...)
            {
                va_list ap;
                va_start(ap, fmt);
                fprintf(stderr, "[DlcModel] ");
                vfprintf(stderr, fmt, ap);
                fprintf(stderr, "\n");
                va_end(ap);
            }

            // callback handed to QnnLog_create: the backend logs through this
            void qnnLogCallback(const char *fmt, QnnLog_Level_t level, uint64_t /*timestamp*/, va_list argp)
            {
                const char *tag = "UNKNOWN";
                switch (level)
                {
                case QNN_LOG_LEVEL_ERROR:   tag = "ERROR";   break;
                case QNN_LOG_LEVEL_WARN:    tag = "WARN";    break;
                case QNN_LOG_LEVEL_INFO:    tag = "INFO";    break;
                case QNN_LOG_LEVEL_VERBOSE: tag = "VERBOSE"; break;
                case QNN_LOG_LEVEL_DEBUG:   tag = "DEBUG";   break;
                default: break;
                }
                fprintf(stderr, "[QNN %s] ", tag);
                vfprintf(stderr, fmt, argp);
                fprintf(stderr, "\n");
            }

            // ----- version-safe Qnn_Tensor_t accessors -------------------------
            // Qnn_Tensor_t is a {version, union{v1, v2}}; v1 and v2 share these
            // leading fields, so we read/write them through the active version.

            uint32_t tensorRank(const Qnn_Tensor_t &t)
            {
                return t.version == QNN_TENSOR_VERSION_1 ? t.v1.rank : t.v2.rank;
            }
            const uint32_t *tensorDims(const Qnn_Tensor_t &t)
            {
                return t.version == QNN_TENSOR_VERSION_1 ? t.v1.dimensions : t.v2.dimensions;
            }
            Qnn_DataType_t tensorDataType(const Qnn_Tensor_t &t)
            {
                return t.version == QNN_TENSOR_VERSION_1 ? t.v1.dataType : t.v2.dataType;
            }
            const char *tensorName(const Qnn_Tensor_t &t)
            {
                const char *n = t.version == QNN_TENSOR_VERSION_1 ? t.v1.name : t.v2.name;
                return n ? n : "";
            }
            void tensorSetRawBuffer(Qnn_Tensor_t &t, void *data, uint32_t dataSize)
            {
                if (t.version == QNN_TENSOR_VERSION_1)
                {
                    t.v1.memType = QNN_TENSORMEMTYPE_RAW;
                    t.v1.clientBuf.data = data;
                    t.v1.clientBuf.dataSize = dataSize;
                }
                else
                {
                    t.v2.memType = QNN_TENSORMEMTYPE_RAW;
                    t.v2.clientBuf.data = data;
                    t.v2.clientBuf.dataSize = dataSize;
                }
            }

            // ----- version-safe QnnSystemContext_GraphInfo_t accessors ---------

            const char *graphName(const QnnSystemContext_GraphInfo_t &g)
            {
                switch (g.version)
                {
                case QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_1: return g.graphInfoV1.graphName;
                case QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_2: return g.graphInfoV2.graphName;
                default:                                      return g.graphInfoV3.graphName;
                }
            }
            void graphTensors(const QnnSystemContext_GraphInfo_t &g,
                              Qnn_Tensor_t **inputs, uint32_t *numInputs,
                              Qnn_Tensor_t **outputs, uint32_t *numOutputs)
            {
                switch (g.version)
                {
                case QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_1:
                    *inputs = g.graphInfoV1.graphInputs;   *numInputs = g.graphInfoV1.numGraphInputs;
                    *outputs = g.graphInfoV1.graphOutputs; *numOutputs = g.graphInfoV1.numGraphOutputs;
                    break;
                case QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_2:
                    *inputs = g.graphInfoV2.graphInputs;   *numInputs = g.graphInfoV2.numGraphInputs;
                    *outputs = g.graphInfoV2.graphOutputs; *numOutputs = g.graphInfoV2.numGraphOutputs;
                    break;
                default:
                    *inputs = g.graphInfoV3.graphInputs;   *numInputs = g.graphInfoV3.numGraphInputs;
                    *outputs = g.graphInfoV3.graphOutputs; *numOutputs = g.graphInfoV3.numGraphOutputs;
                    break;
                }
            }

            size_t productDims(const uint32_t *dims, uint32_t rank)
            {
                size_t n = (rank == 0) ? 0 : 1;
                for (uint32_t i = 0; i < rank; ++i)
                    n *= dims[i];
                return n;
            }

        } // namespace

        // =====================================================================
        // Impl
        // =====================================================================

        struct DlcModel::Impl
        {
            std::string backendLibPath;
            std::string dlcPath;
            std::string systemLibPath;

            void *backendLib = nullptr;
            void *systemLib = nullptr;
            const QnnInterface_t *iface = nullptr;
            const QnnSystemInterface_t *sysIface = nullptr;

            Qnn_LogHandle_t logHandle = nullptr;
            Qnn_BackendHandle_t backend = nullptr;
            Qnn_ContextHandle_t context = nullptr;
            QnnSystemDlc_Handle_t dlc = nullptr;

            QnnSystemContext_GraphInfo_t *graphs = nullptr; // owned by us; free() at teardown
            uint32_t numGraphs = 0;

            // per-graph runtime state
            std::vector<Qnn_GraphHandle_t> graphHandles;
            std::vector<std::vector<TensorInfo>> inInfos;
            std::vector<std::vector<TensorInfo>> outInfos;
            std::vector<std::vector<Qnn_Tensor_t>> inTensors;  // client copies for execute
            std::vector<std::vector<Qnn_Tensor_t>> outTensors;

            bool loaded = false;

            ~Impl() { teardown(); }
            void teardown();
        };

        void DlcModel::Impl::teardown()
        {
            const QnnInterface_t *i = iface;
            if (i)
            {
                auto &core = i->QNN_INTERFACE_VER_NAME;
                if (context && core.contextFree)
                    core.contextFree(context, nullptr);
                if (backend && core.backendFree)
                    core.backendFree(backend);
                if (logHandle && core.logFree)
                    core.logFree(logHandle);
            }
            context = nullptr;
            backend = nullptr;
            logHandle = nullptr;

            if (dlc && sysIface)
            {
                auto &sys = sysIface->QNN_SYSTEM_INTERFACE_VER_NAME;
                if (sys.systemDlcFree)
                    sys.systemDlcFree(dlc);
            }
            dlc = nullptr;

            if (graphs)
            {
                free(graphs); // composeGraphs: "Memory allocated in graphs is owned by clients"
                graphs = nullptr;
            }
            numGraphs = 0;

            graphHandles.clear();
            inInfos.clear();
            outInfos.clear();
            inTensors.clear();
            outTensors.clear();

            if (systemLib)
            {
                dlclose(systemLib);
                systemLib = nullptr;
            }
            if (backendLib)
            {
                dlclose(backendLib);
                backendLib = nullptr;
            }
            iface = nullptr;
            sysIface = nullptr;
            loaded = false;
        }

        // =====================================================================
        // DlcModel
        // =====================================================================

        DlcModel::DlcModel(std::string backendLibPath, std::string dlcPath, std::string systemLibPath)
            : p_(new Impl())
        {
            p_->backendLibPath = std::move(backendLibPath);
            p_->dlcPath = std::move(dlcPath);
            p_->systemLibPath = std::move(systemLibPath);
        }

        DlcModel::~DlcModel() = default;

        bool DlcModel::load(int logLevel)
        {
            if (p_->loaded)
                return true;

            // --- resolve the backend interface -------------------------------
            p_->backendLib = dlopen(p_->backendLibPath.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!p_->backendLib)
            {
                logMsg("dlopen backend '%s' failed: %s", p_->backendLibPath.c_str(), dlerror());
                return false;
            }
            {
                typedef Qnn_ErrorHandle_t (*GetProvidersFn)(const QnnInterface_t ***, uint32_t *);
                auto getProviders = (GetProvidersFn)dlsym(p_->backendLib, "QnnInterface_getProviders");
                if (!getProviders)
                {
                    logMsg("backend missing QnnInterface_getProviders: %s", dlerror());
                    return false;
                }
                const QnnInterface_t **providers = nullptr;
                uint32_t numProviders = 0;
                if (getProviders(&providers, &numProviders) != QNN_SUCCESS || numProviders == 0 || !providers)
                {
                    logMsg("QnnInterface_getProviders returned no providers");
                    return false;
                }
                p_->iface = providers[0]; // one provider per backend library
            }

            // --- resolve the system interface --------------------------------
            p_->systemLib = dlopen(p_->systemLibPath.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!p_->systemLib)
            {
                logMsg("dlopen system lib '%s' failed: %s", p_->systemLibPath.c_str(), dlerror());
                return false;
            }
            {
                typedef Qnn_ErrorHandle_t (*GetSysProvidersFn)(const QnnSystemInterface_t ***, uint32_t *);
                auto getProviders = (GetSysProvidersFn)dlsym(p_->systemLib, "QnnSystemInterface_getProviders");
                if (!getProviders)
                {
                    logMsg("system lib missing QnnSystemInterface_getProviders: %s", dlerror());
                    return false;
                }
                const QnnSystemInterface_t **providers = nullptr;
                uint32_t numProviders = 0;
                if (getProviders(&providers, &numProviders) != QNN_SUCCESS || numProviders == 0 || !providers)
                {
                    logMsg("QnnSystemInterface_getProviders returned no providers");
                    return false;
                }
                p_->sysIface = providers[0];
            }

            auto &core = p_->iface->QNN_INTERFACE_VER_NAME;
            auto &sys = p_->sysIface->QNN_SYSTEM_INTERFACE_VER_NAME;

            // --- log, backend, context ---------------------------------------
            QnnLog_Level_t level = (QnnLog_Level_t)(logLevel < QNN_LOG_LEVEL_ERROR ? QNN_LOG_LEVEL_ERROR
                                                    : logLevel > QNN_LOG_LEVEL_DEBUG ? QNN_LOG_LEVEL_DEBUG
                                                                                     : logLevel);
            if (core.logCreate)
                core.logCreate(qnnLogCallback, level, &p_->logHandle); // non-fatal if it fails

            if (core.backendCreate(p_->logHandle, nullptr, &p_->backend) != QNN_SUCCESS)
            {
                logMsg("backendCreate failed");
                return false;
            }
            // device is optional for CPU/GPU; pass nullptr
            if (core.contextCreate(p_->backend, nullptr, nullptr, &p_->context) != QNN_SUCCESS)
            {
                logMsg("contextCreate failed");
                return false;
            }

            // --- open the DLC and compose its graphs into the context --------
            if (sys.systemDlcCreateFromFile(nullptr, p_->dlcPath.c_str(), &p_->dlc) != QNN_SUCCESS)
            {
                logMsg("systemDlcCreateFromFile('%s') failed", p_->dlcPath.c_str());
                return false;
            }
            if (sys.systemDlcComposeGraphs(p_->dlc,
                                           nullptr, 0,
                                           p_->backend,
                                           p_->context,
                                           *p_->iface, // backendInterface (by value)
                                           QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_1,
                                           &p_->graphs,
                                           &p_->numGraphs) != QNN_SUCCESS ||
                p_->graphs == nullptr || p_->numGraphs == 0)
            {
                logMsg("systemDlcComposeGraphs failed");
                return false;
            }

            // --- per graph: retrieve handle, finalize, prepare IO tensors -----
            p_->graphHandles.assign(p_->numGraphs, nullptr);
            p_->inInfos.resize(p_->numGraphs);
            p_->outInfos.resize(p_->numGraphs);
            p_->inTensors.resize(p_->numGraphs);
            p_->outTensors.resize(p_->numGraphs);

            for (uint32_t g = 0; g < p_->numGraphs; ++g)
            {
                const char *name = graphName(p_->graphs[g]);
                if (core.graphRetrieve(p_->context, name, &p_->graphHandles[g]) != QNN_SUCCESS)
                {
                    logMsg("graphRetrieve('%s') failed", name ? name : "");
                    return false;
                }
                if (core.graphFinalize(p_->graphHandles[g], nullptr, nullptr) != QNN_SUCCESS)
                {
                    logMsg("graphFinalize('%s') failed", name ? name : "");
                    return false;
                }

                Qnn_Tensor_t *ins = nullptr, *outs = nullptr;
                uint32_t nIn = 0, nOut = 0;
                graphTensors(p_->graphs[g], &ins, &nIn, &outs, &nOut);

                auto prepare = [&](Qnn_Tensor_t *src, uint32_t n,
                                   std::vector<TensorInfo> &infos,
                                   std::vector<Qnn_Tensor_t> &clients) -> bool
                {
                    infos.resize(n);
                    clients.resize(n);
                    for (uint32_t i = 0; i < n; ++i)
                    {
                        if (tensorDataType(src[i]) != QNN_DATATYPE_FLOAT_32)
                        {
                            logMsg("tensor '%s' has dtype %d; only FLOAT_32 is wired currently "
                                   "(add a conversion path in DlcModel.cpp)",
                                   tensorName(src[i]), (int)tensorDataType(src[i]));
                            return false;
                        }
                        const uint32_t rank = tensorRank(src[i]);
                        const uint32_t *dims = tensorDims(src[i]);
                        infos[i].name = tensorName(src[i]);
                        infos[i].dims.assign(dims, dims + rank);
                        infos[i].numElements = productDims(dims, rank);

                        // client tensor: shallow copy of the descriptor (shares the
                        // name/dimensions owned by p_->graphs, which outlives us),
                        // switched to a RAW client buffer we point at execute time.
                        clients[i] = src[i];
                        tensorSetRawBuffer(clients[i], nullptr, 0);
                    }
                    return true;
                };

                if (!prepare(ins, nIn, p_->inInfos[g], p_->inTensors[g]))
                    return false;
                if (!prepare(outs, nOut, p_->outInfos[g], p_->outTensors[g]))
                    return false;
            }

            p_->loaded = true;
            return true;
        }

        uint32_t DlcModel::numGraphs() const
        {
            return p_->numGraphs;
        }

        const std::vector<TensorInfo> &DlcModel::inputs(uint32_t graphIdx) const
        {
            return p_->inInfos.at(graphIdx);
        }

        const std::vector<TensorInfo> &DlcModel::outputs(uint32_t graphIdx) const
        {
            return p_->outInfos.at(graphIdx);
        }

        bool DlcModel::execute(uint32_t graphIdx,
                               const float *const *inputBuffers,
                               float *const *outputBuffers)
        {
            if (!p_->loaded || graphIdx >= p_->numGraphs)
                return false;

            auto &core = p_->iface->QNN_INTERFACE_VER_NAME;
            auto &inT = p_->inTensors[graphIdx];
            auto &outT = p_->outTensors[graphIdx];
            auto &inI = p_->inInfos[graphIdx];
            auto &outI = p_->outInfos[graphIdx];

            // fp32 zero-copy: point the client buffers straight at the caller's
            // float arrays (QNN only reads inputs, so the const_cast is safe).
            for (size_t i = 0; i < inT.size(); ++i)
                tensorSetRawBuffer(inT[i], const_cast<float *>(inputBuffers[i]),
                                   (uint32_t)(inI[i].numElements * sizeof(float)));
            for (size_t i = 0; i < outT.size(); ++i)
                tensorSetRawBuffer(outT[i], outputBuffers[i],
                                   (uint32_t)(outI[i].numElements * sizeof(float)));

            Qnn_ErrorHandle_t err = core.graphExecute(p_->graphHandles[graphIdx],
                                                      inT.data(), (uint32_t)inT.size(),
                                                      outT.data(), (uint32_t)outT.size(),
                                                      nullptr, nullptr);
            if (err != QNN_SUCCESS)
            {
                logMsg("graphExecute failed (err=%lld)", (long long)err);
                return false;
            }
            return true;
        }

    } // namespace qnn
} // namespace ar

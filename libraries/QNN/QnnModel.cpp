/*
 * Copyright 2026 Victor Zappi, Avery Huang
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

// Original clean-room implementation written against the public QNN API only
// (QnnInterface / QnnSystemInterface, QnnSystemDlc, QnnSystemContext, QnnContext,
// QnnGraph, QnnTensor, QnnLog) plus <dlfcn.h>. It does not use or derive from
// Qualcomm's SDK sample sources (IOTensor, SampleApp, qnn_wrapper_api, PAL, etc.).
//
// Supports two model sources, chosen by file extension:
//   .dlc -> composed at load via QnnSystemDlc_composeGraphs (+ graph finalize)
//   .bin -> precompiled context binary via QnnContext_createFromBinary (no finalize)
// Both paths converge on the same graph-retrieve / tensor-setup / execute code.

#include "QnnModel.h"

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
                fprintf(stderr, "[QnnModel] ");
                vfprintf(stderr, fmt, ap);
                fprintf(stderr, "\n");
                va_end(ap);
            }

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

            // extract the graph array from a context binary's BinaryInfo (version-safe)
            void binaryInfoGraphs(const QnnSystemContext_BinaryInfo_t *b,
                                  QnnSystemContext_GraphInfo_t **graphs, uint32_t *numGraphs)
            {
                switch (b->version)
                {
                case QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_1:
                    *graphs = b->contextBinaryInfoV1.graphs; *numGraphs = b->contextBinaryInfoV1.numGraphs;
                    break;
                case QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_2:
                    *graphs = b->contextBinaryInfoV2.graphs; *numGraphs = b->contextBinaryInfoV2.numGraphs;
                    break;
                default:
                    *graphs = b->contextBinaryInfoV3.graphs; *numGraphs = b->contextBinaryInfoV3.numGraphs;
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

            bool endsWith(const std::string &s, const char *suffix)
            {
                size_t n = strlen(suffix);
                return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
            }

            bool readFile(const std::string &path, std::vector<uint8_t> &out)
            {
                FILE *f = fopen(path.c_str(), "rb");
                if (!f)
                    return false;
                fseek(f, 0, SEEK_END);
                long n = ftell(f);
                fseek(f, 0, SEEK_SET);
                if (n <= 0)
                {
                    fclose(f);
                    return false;
                }
                out.resize((size_t)n);
                size_t rd = fread(out.data(), 1, (size_t)n, f);
                fclose(f);
                return rd == (size_t)n;
            }

        } // namespace

        // =====================================================================
        // Impl
        // =====================================================================

        struct QnnModel::Impl
        {
            std::string backendLibPath;
            std::string modelPath;
            std::string systemLibPath;

            void *backendLib = nullptr;
            void *systemLib = nullptr;
            const QnnInterface_t *iface = nullptr;
            const QnnSystemInterface_t *sysIface = nullptr;

            Qnn_LogHandle_t logHandle = nullptr;
            Qnn_BackendHandle_t backend = nullptr;
            Qnn_ContextHandle_t context = nullptr;

            // .dlc source
            QnnSystemDlc_Handle_t dlc = nullptr;
            QnnSystemContext_GraphInfo_t *dlcGraphs = nullptr; // owned by us; free() at teardown

            // .bin source
            QnnSystemContext_Handle_t sysCtx = nullptr; // owns the BinaryInfo (+ its tensors/dims)
            std::vector<uint8_t> binaryBuffer;

            uint32_t numGraphs = 0;

            std::vector<Qnn_GraphHandle_t> graphHandles;
            std::vector<std::vector<TensorInfo>> inInfos;
            std::vector<std::vector<TensorInfo>> outInfos;
            std::vector<std::vector<Qnn_Tensor_t>> inTensors;
            std::vector<std::vector<Qnn_Tensor_t>> outTensors;

            bool loaded = false;

            ~Impl() { teardown(); }
            void teardown();
        };

        void QnnModel::Impl::teardown()
        {
            if (iface)
            {
                auto &core = iface->QNN_INTERFACE_VER_NAME;
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

            if (sysIface)
            {
                auto &sys = sysIface->QNN_SYSTEM_INTERFACE_VER_NAME;
                if (dlc && sys.systemDlcFree)
                    sys.systemDlcFree(dlc);
                if (sysCtx && sys.systemContextFree)
                    sys.systemContextFree(sysCtx); // frees the BinaryInfo it owns
            }
            dlc = nullptr;
            sysCtx = nullptr;

            if (dlcGraphs)
            {
                free(dlcGraphs); // composeGraphs: "Memory allocated in graphs is owned by clients"
                dlcGraphs = nullptr;
            }
            numGraphs = 0;

            graphHandles.clear();
            inInfos.clear();
            outInfos.clear();
            inTensors.clear();
            outTensors.clear();
            binaryBuffer.clear();

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
        // QnnModel
        // =====================================================================

        QnnModel::QnnModel(std::string backendLibPath, std::string modelPath, std::string systemLibPath)
            : p_(new Impl())
        {
            p_->backendLibPath = std::move(backendLibPath);
            p_->modelPath = std::move(modelPath);
            p_->systemLibPath = std::move(systemLibPath);
        }

        QnnModel::~QnnModel() = default;

        bool QnnModel::load(int logLevel)
        {
            if (p_->loaded)
                return true;

            // choose the source format from the file extension
            bool isBinary;
            if (endsWith(p_->modelPath, ".bin"))
                isBinary = true;
            else if (endsWith(p_->modelPath, ".dlc"))
                isBinary = false;
            else
            {
                logMsg("unknown model format for '%s' (expected .dlc or .bin)", p_->modelPath.c_str());
                return false;
            }

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
                p_->iface = providers[0];
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

            // --- log + backend -----------------------------------------------
            QnnLog_Level_t level = (QnnLog_Level_t)(logLevel < QNN_LOG_LEVEL_ERROR ? QNN_LOG_LEVEL_ERROR
                                                    : logLevel > QNN_LOG_LEVEL_DEBUG ? QNN_LOG_LEVEL_DEBUG
                                                                                     : logLevel);
            if (core.logCreate)
                core.logCreate(qnnLogCallback, level, &p_->logHandle);

            if (core.backendCreate(p_->logHandle, nullptr, &p_->backend) != QNN_SUCCESS)
            {
                logMsg("backendCreate failed");
                return false;
            }

            // --- create the context + obtain graph metadata (source-specific) -
            QnnSystemContext_GraphInfo_t *graphs = nullptr;
            uint32_t numGraphs = 0;
            bool needFinalize = false;

            if (!isBinary)
            {
                // .dlc: create an empty context, then compose the DLC's graphs into it
                if (core.contextCreate(p_->backend, nullptr, nullptr, &p_->context) != QNN_SUCCESS)
                {
                    logMsg("contextCreate failed");
                    return false;
                }
                if (sys.systemDlcCreateFromFile(nullptr, p_->modelPath.c_str(), &p_->dlc) != QNN_SUCCESS)
                {
                    logMsg("systemDlcCreateFromFile('%s') failed", p_->modelPath.c_str());
                    return false;
                }
                if (sys.systemDlcComposeGraphs(p_->dlc, nullptr, 0,
                                               p_->backend, p_->context, *p_->iface,
                                               QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_1,
                                               &p_->dlcGraphs, &numGraphs) != QNN_SUCCESS ||
                    p_->dlcGraphs == nullptr || numGraphs == 0)
                {
                    logMsg("systemDlcComposeGraphs failed");
                    return false;
                }
                graphs = p_->dlcGraphs;
                needFinalize = true; // composed graphs must be finalized before execute
            }
            else
            {
                // .bin: read graph metadata from the binary, then create the context from it
                if (!readFile(p_->modelPath, p_->binaryBuffer))
                {
                    logMsg("failed to read context binary '%s'", p_->modelPath.c_str());
                    return false;
                }
                if (sys.systemContextCreate(&p_->sysCtx) != QNN_SUCCESS)
                {
                    logMsg("systemContextCreate failed");
                    return false;
                }
                const QnnSystemContext_BinaryInfo_t *binInfo = nullptr;
                Qnn_ContextBinarySize_t binInfoSize = 0;
                if (sys.systemContextGetBinaryInfo(p_->sysCtx,
                                                   p_->binaryBuffer.data(),
                                                   (uint64_t)p_->binaryBuffer.size(),
                                                   &binInfo, &binInfoSize) != QNN_SUCCESS ||
                    binInfo == nullptr)
                {
                    logMsg("systemContextGetBinaryInfo failed");
                    return false;
                }
                binaryInfoGraphs(binInfo, &graphs, &numGraphs); // owned by sysCtx (kept alive)
                if (graphs == nullptr || numGraphs == 0)
                {
                    logMsg("context binary reports no graphs");
                    return false;
                }
                if (core.contextCreateFromBinary(p_->backend, nullptr, nullptr,
                                                 p_->binaryBuffer.data(),
                                                 (Qnn_ContextBinarySize_t)p_->binaryBuffer.size(),
                                                 &p_->context, nullptr) != QNN_SUCCESS)
                {
                    logMsg("contextCreateFromBinary failed");
                    return false;
                }
                needFinalize = false; // a context binary is already finalized
            }

            // --- per graph: retrieve handle, (finalize for DLC), prep IO tensors
            p_->numGraphs = numGraphs;
            p_->graphHandles.assign(numGraphs, nullptr);
            p_->inInfos.resize(numGraphs);
            p_->outInfos.resize(numGraphs);
            p_->inTensors.resize(numGraphs);
            p_->outTensors.resize(numGraphs);

            for (uint32_t g = 0; g < numGraphs; ++g)
            {
                const char *name = graphName(graphs[g]);
                if (core.graphRetrieve(p_->context, name, &p_->graphHandles[g]) != QNN_SUCCESS)
                {
                    logMsg("graphRetrieve('%s') failed", name ? name : "");
                    return false;
                }
                if (needFinalize &&
                    core.graphFinalize(p_->graphHandles[g], nullptr, nullptr) != QNN_SUCCESS)
                {
                    logMsg("graphFinalize('%s') failed", name ? name : "");
                    return false;
                }

                Qnn_Tensor_t *ins = nullptr, *outs = nullptr;
                uint32_t nIn = 0, nOut = 0;
                graphTensors(graphs[g], &ins, &nIn, &outs, &nOut);

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
                                   "(add a conversion path in QnnModel.cpp)",
                                   tensorName(src[i]), (int)tensorDataType(src[i]));
                            return false;
                        }
                        const uint32_t rank = tensorRank(src[i]);
                        const uint32_t *dims = tensorDims(src[i]);
                        infos[i].name = tensorName(src[i]);
                        infos[i].dims.assign(dims, dims + rank);
                        infos[i].numElements = productDims(dims, rank);

                        // client tensor: shallow copy of the descriptor (shares the
                        // name/dimensions owned by the DLC graphs array or the system
                        // context, both kept alive for our lifetime), switched to a
                        // RAW client buffer we point at execute time.
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

        uint32_t QnnModel::numGraphs() const
        {
            return p_->numGraphs;
        }

        const std::vector<TensorInfo> &QnnModel::inputs(uint32_t graphIdx) const
        {
            return p_->inInfos.at(graphIdx);
        }

        const std::vector<TensorInfo> &QnnModel::outputs(uint32_t graphIdx) const
        {
            return p_->outInfos.at(graphIdx);
        }

        bool QnnModel::execute(uint32_t graphIdx,
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

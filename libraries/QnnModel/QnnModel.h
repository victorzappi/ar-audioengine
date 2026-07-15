/*
 * Copyright 2026 Victor Zappi, Avery Huang
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

// QnnModel: load a Qualcomm model -- either a Deep Learning Container (.dlc) or a
// precompiled context binary (.bin) -- and run its graph(s) through the public QNN API.
//
// This is an original clean-room implementation written against the public QNN
// interface only (QnnInterface / QnnSystemInterface, QnnSystemDlc,
// QnnSystemContext, QnnContext, QnnGraph, QnnTensor, QnnLog) plus <dlfcn.h>. It
// does not use or derive from Qualcomm's SDK sample sources.
//
// The public header intentionally exposes no QNN types (it is pimpl'd), so a
// project can include it without the QNN SDK headers on its include path.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ar
{
    namespace qnn
    {
        // Shape/identity of one graph input or output tensor.
        struct TensorInfo
        {
            std::string name;
            std::vector<uint32_t> dims; // size of each dimension, outermost first
            size_t numElements = 0;     // product of dims
        };

        // Loads a model and runs its graphs. One instance owns one backend +
        // context; not copyable. All heavy resources are released in the destructor.
        class QnnModel
        {
        public:
            // backendLibPath: QNN backend library (e.g. libQnnCpu.so / libQnnGpu.so).
            // modelPath:      a ".dlc" (composed at load) or a ".bin" context binary
            //                 (precompiled); the format is chosen from the extension.
            // systemLibPath:  the QNN System library (libQnnSystem.so).
            QnnModel(std::string backendLibPath,
                     std::string modelPath,
                     std::string systemLibPath);
            ~QnnModel();

            QnnModel(const QnnModel &) = delete;
            QnnModel &operator=(const QnnModel &) = delete;

            // Load both libraries and the model, prepare graphs and I/O tensors.
            // logLevel is the QNN log level: 1=ERROR, 2=WARN, 3=INFO, 4=VERBOSE,
            // 5=DEBUG. Returns false on any failure (details are logged). Call once
            // before execute().
            bool load(int logLevel = 1);

            uint32_t numGraphs() const;

            // I/O tensor descriptors for a graph (valid after a successful load()).
            const std::vector<TensorInfo> &inputs(uint32_t graphIdx = 0) const;
            const std::vector<TensorInfo> &outputs(uint32_t graphIdx = 0) const;

            // Run one graph: inputBuffers[i] feeds input i (sized to
            // inputs(graphIdx)[i].numElements floats), outputBuffers[i] receives
            // output i (sized to outputs(graphIdx)[i].numElements floats). Buffers
            // are float32. Returns false on failure.
            bool execute(uint32_t graphIdx,
                         const float *const *inputBuffers,
                         float *const *outputBuffers);

        private:
            struct Impl;
            std::unique_ptr<Impl> p_;
        };

    } // namespace qnn
} // namespace ar

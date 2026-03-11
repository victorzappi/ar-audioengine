#pragma once

#include <vector>

#include "QnnTypeMacros.hpp"
#include "QnnTypes.h"
#include "QnnTensor.h"
#include "IOTensor.hpp"

namespace qnn
{
    namespace tools
    {
        /**
         * Some functionality in the original sample app's IOTensor files are private or serve slightly different purposes.
         * TensorTools contains modified versions of the functions (plus some additional helpful utilities).
         */
        namespace iotensor
        {
            // Struct with helpful user-facing info for an input/output tensor
            struct TensorWrapper {
                Qnn_Tensor_t *tensor;
                std::vector<size_t> dimensions;
                size_t numElements;
                // Not all models take float32 as inputs - this flag determines if conversion to/from f32 is required.
                // Conversions are handled under the hood when using QnnModel.
                bool convertNative; 
            };

            // Populate a vector with the tensor's dimensions
            void fillDims(std::vector<size_t> &dims, Qnn_Tensor_t *tensor);

            // Create a TensorWrapper with the info from the given tensor
            TensorWrapper createTensorWrapper(Qnn_Tensor_t *tensor);

            // Allocates a 2D float array to store data for a set of input/output tensors
            StatusCode allocateIOFloatBuffer(const std::vector<TensorWrapper> &tensors, float **&buffer);

            // Copies data from an input float array into a tensor's underlying buffer, 
            // converting from float32 if necessary
            StatusCode copyBufferToInputTensor(float *source, TensorWrapper *dest);

            // Copies data from an a tensor's underlying buffer into an output float array, 
            // converting to float32 if necessary
            StatusCode copyOutputTensorToBuffer(TensorWrapper *source, float *dest);
        }
    }
}
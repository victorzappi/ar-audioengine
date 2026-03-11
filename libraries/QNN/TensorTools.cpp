#include "TensorTools.h"
#include "QnnTypes.h"
#include "QnnTypeMacros.hpp"
#include "Logger.hpp"
#include "PAL/StringOp.hpp"
#include "DataUtil.hpp"

namespace qnn
{
    namespace tools
    {
        namespace iotensor
        {
            void fillDims(std::vector<size_t> &dims, Qnn_Tensor_t *tensor)
            {
                uint32_t *inDimensions = QNN_TENSOR_GET_DIMENSIONS(tensor);
                for (size_t r = 0; r < QNN_TENSOR_GET_RANK(tensor); r++)
                {
                    dims.push_back(inDimensions[r]);
                }
            }

            TensorWrapper createTensorWrapper(Qnn_Tensor_t *tensor)
            {
                std::vector<size_t> dims;
                fillDims(dims, tensor);
                bool convertNative = (QNN_TENSOR_GET_DATA_TYPE(tensor) != QNN_DATATYPE_FLOAT_32);
                size_t numElements = datautil::calculateElementCount(dims);

                return {tensor, dims, numElements, convertNative};
            }

            StatusCode allocateIOFloatBuffer(const std::vector<TensorWrapper> &tensors, float **&buffer)
            {
                if (tensors.empty())
                {
                    QNN_ERROR("allocateIOFloatBuffer: given empty tensor vector");
                    return StatusCode::FAILURE;
                }

                buffer = static_cast<float **>(calloc(tensors.size(), sizeof(float *)));
                if (buffer == nullptr)
                {
                    QNN_ERROR("allocateIOFloatBuffer: failed to allocate buffer array");
                    return StatusCode::FAILURE;
                }

                for (size_t i = 0; i < tensors.size(); i++)
                {
                    buffer[i] = static_cast<float *>(calloc(tensors[i].numElements, sizeof(float)));
                    if (buffer[i] == nullptr)
                    {
                        QNN_ERROR("allocateIOFloatBuffer: failed to allocate buffer %d", i);

                        // Cleanup: Free all previously allocated buffers, then free `buffer`
                        for (size_t j = 0; j < i; j++)
                        {
                            free(buffer[j]);
                        }
                        free(buffer);
                        buffer = nullptr;

                        return StatusCode::FAILURE;
                    }
                }

                return StatusCode::SUCCESS;
            }

            // forward-defs for private helpers
            StatusCode copyFloatToNative(float *source, TensorWrapper *dest);
            StatusCode copyNativeToFloat(TensorWrapper *source, float *dest);

            StatusCode copyBufferToInputTensor(float *source, TensorWrapper *dest)
            {
                if (source == nullptr)
                {
                    QNN_ERROR("copyBufferToInputTensor: given null source buffer");
                    return StatusCode::FAILURE;
                }
                if (dest == nullptr)
                {
                    QNN_ERROR("copyBufferToInputTensor: given null destination tensor");
                    return StatusCode::FAILURE;
                }

                if (dest->convertNative)
                {
                    return copyFloatToNative(source, dest);
                }
                else
                {
                    size_t length = dest->numElements * datautil::g_dataTypeToSize.find(QNN_DATATYPE_FLOAT_32)->second;
                    if (QNN_TENSOR_GET_CLIENT_BUF(dest->tensor).dataSize != length)
                    {
                        QNN_ERROR("copyBufferToInputTensor: computed byte length doesn't match tensor datasize");
                        return StatusCode::FAILURE;
                    }

                    pal::StringOp::memscpy(QNN_TENSOR_GET_CLIENT_BUF(dest->tensor).data, length, source, length);
                    return StatusCode::SUCCESS;
                }
            }

            StatusCode copyOutputTensorToBuffer(TensorWrapper *source, float *dest)
            {
                if (source == nullptr)
                {
                    QNN_ERROR("copyOutputTensorToBuffer: given null source tensor");
                    return StatusCode::FAILURE;
                }
                if (dest == nullptr)
                {
                    QNN_ERROR("copyOutputTensorToBuffer: given null destination buffer");
                    return StatusCode::FAILURE;
                }

                if (source->convertNative)
                {
                    return copyNativeToFloat(source, dest);
                }
                else
                {
                    size_t length = source->numElements * datautil::g_dataTypeToSize.find(QNN_DATATYPE_FLOAT_32)->second;
                    if (QNN_TENSOR_GET_CLIENT_BUF(source->tensor).dataSize != length)
                    {
                        QNN_ERROR("copyOutputTensorToBuffer: computed byte length doesn't match tensor datasize");
                        return StatusCode::FAILURE;
                    }

                    pal::StringOp::memscpy(dest, length, QNN_TENSOR_GET_CLIENT_BUF(source->tensor).data, length);
                    return StatusCode::SUCCESS;
                }
            }

            /**
             * ======================================
             * PRIVATE
             * Private template helpers to convert between float and various other datatypes
             * ======================================
             */

            template <typename T>
            static StatusCode copyFromFloatToTfN(Qnn_Tensor_t *qnnTensor, float *source, size_t numElements)
            {
                auto copyStatus = datautil::floatToTfN<T>(
                    static_cast<T *>(QNN_TENSOR_GET_CLIENT_BUF(qnnTensor).data),
                    source,
                    QNN_TENSOR_GET_QUANT_PARAMS(qnnTensor).scaleOffsetEncoding.offset,
                    QNN_TENSOR_GET_QUANT_PARAMS(qnnTensor).scaleOffsetEncoding.scale,
                    numElements);

                if (datautil::StatusCode::SUCCESS != copyStatus)
                {
                    QNN_ERROR("copyFloatToNative: failed to copy to tensor");
                    return StatusCode::FAILURE;
                }

                return StatusCode::SUCCESS;
            }

            template <typename T>
            static StatusCode copyFromFloatWithCast(Qnn_Tensor_t *qnnTensor, float *source, size_t numElements)
            {
                auto copyStatus = datautil::castFromFloat<T>(
                    static_cast<T *>(QNN_TENSOR_GET_CLIENT_BUF(qnnTensor).data),
                    source,
                    numElements);

                if (datautil::StatusCode::SUCCESS != copyStatus)
                {
                    QNN_ERROR("copyFloatToNative: failed to copy to tensor");
                    return StatusCode::FAILURE;
                }

                return StatusCode::SUCCESS;
            }

            template <typename T>
            static StatusCode copyFromTfNToFloat(Qnn_Tensor_t *qnnTensor, float *dest, size_t numElements)
            {
                auto copyStatus = datautil::tfNToFloat<T>(
                    dest,
                    reinterpret_cast<T *>(QNN_TENSOR_GET_CLIENT_BUF(qnnTensor).data),
                    QNN_TENSOR_GET_QUANT_PARAMS(qnnTensor).scaleOffsetEncoding.offset,
                    QNN_TENSOR_GET_QUANT_PARAMS(qnnTensor).scaleOffsetEncoding.scale,
                    numElements);

                if (datautil::StatusCode::SUCCESS != copyStatus)
                {
                    QNN_ERROR("copyNativeToFloat: failed to copy from tensor");
                    return StatusCode::FAILURE;
                }

                return StatusCode::SUCCESS;
            }

            template <typename T>
            static StatusCode copyToFloatWithCast(Qnn_Tensor_t *qnnTensor, float *dest, size_t numElements)
            {
                auto copyStatus = datautil::castToFloat<T>(
                    dest,
                    static_cast<T *>(QNN_TENSOR_GET_CLIENT_BUF(qnnTensor).data),
                    numElements);

                if (datautil::StatusCode::SUCCESS != copyStatus)
                {
                    QNN_ERROR("copyNativeToFloat: failed to copy from tensor");
                    return StatusCode::FAILURE;
                }

                return StatusCode::SUCCESS;
            }

            StatusCode copyFloatToNative(float *source, TensorWrapper *dest)
            {
                if (source == nullptr)
                {
                    QNN_ERROR("copyFloatToNative: given null source buffer");
                    return StatusCode::FAILURE;
                }
                if (dest == nullptr)
                {
                    QNN_ERROR("copyFloatToNative: given null destination tensor");
                    return StatusCode::FAILURE;
                }

                switch (QNN_TENSOR_GET_DATA_TYPE(dest->tensor))
                {
                case QNN_DATATYPE_UFIXED_POINT_8:
                    return copyFromFloatToTfN<uint8_t>(dest->tensor, source, dest->numElements);
                case QNN_DATATYPE_UFIXED_POINT_16:
                    return copyFromFloatToTfN<uint16_t>(dest->tensor, source, dest->numElements);
                case QNN_DATATYPE_UINT_8:
                    return copyFromFloatWithCast<uint8_t>(dest->tensor, source, dest->numElements);
                case QNN_DATATYPE_UINT_16:
                    return copyFromFloatWithCast<uint16_t>(dest->tensor, source, dest->numElements);
                case QNN_DATATYPE_UINT_32:
                    return copyFromFloatWithCast<uint32_t>(dest->tensor, source, dest->numElements);
                case QNN_DATATYPE_UINT_64:
                    return copyFromFloatWithCast<uint64_t>(dest->tensor, source, dest->numElements);
                case QNN_DATATYPE_INT_8:
                    return copyFromFloatWithCast<int8_t>(dest->tensor, source, dest->numElements);
                case QNN_DATATYPE_INT_16:
                    return copyFromFloatWithCast<int16_t>(dest->tensor, source, dest->numElements);
                case QNN_DATATYPE_INT_32:
                    return copyFromFloatWithCast<int32_t>(dest->tensor, source, dest->numElements);
                case QNN_DATATYPE_INT_64:
                    return copyFromFloatWithCast<int64_t>(dest->tensor, source, dest->numElements);
                case QNN_DATATYPE_BOOL_8:
                    return copyFromFloatWithCast<uint8_t>(dest->tensor, source, dest->numElements);
                default:
                    QNN_ERROR("copyFloatToNative: datatype not supported");
                    return StatusCode::FAILURE;
                }
            }

            StatusCode copyNativeToFloat(TensorWrapper *source, float *dest)
            {
                if (source == nullptr)
                {
                    QNN_ERROR("copyNativeToFloat: given null source tensor");
                    return StatusCode::FAILURE;
                }
                if (dest == nullptr)
                {
                    QNN_ERROR("copyNativeToFloat: given null destination buffer");
                    return StatusCode::FAILURE;
                }

                switch (QNN_TENSOR_GET_DATA_TYPE(source->tensor))
                {
                case QNN_DATATYPE_UFIXED_POINT_8:
                    return copyFromTfNToFloat<uint8_t>(source->tensor, dest, source->numElements);
                case QNN_DATATYPE_UFIXED_POINT_16:
                    return copyFromTfNToFloat<uint16_t>(source->tensor, dest, source->numElements);
                case QNN_DATATYPE_UINT_8:
                    return copyToFloatWithCast<uint8_t>(source->tensor, dest, source->numElements);
                case QNN_DATATYPE_UINT_16:
                    return copyToFloatWithCast<uint16_t>(source->tensor, dest, source->numElements);
                case QNN_DATATYPE_UINT_32:
                    return copyToFloatWithCast<uint32_t>(source->tensor, dest, source->numElements);
                case QNN_DATATYPE_UINT_64:
                    return copyToFloatWithCast<uint64_t>(source->tensor, dest, source->numElements);
                case QNN_DATATYPE_INT_8:
                    return copyToFloatWithCast<int8_t>(source->tensor, dest, source->numElements);
                case QNN_DATATYPE_INT_16:
                    return copyToFloatWithCast<int16_t>(source->tensor, dest, source->numElements);
                case QNN_DATATYPE_INT_32:
                    return copyToFloatWithCast<int32_t>(source->tensor, dest, source->numElements);
                case QNN_DATATYPE_INT_64:
                    return copyToFloatWithCast<int64_t>(source->tensor, dest, source->numElements);
                case QNN_DATATYPE_BOOL_8:
                    return copyToFloatWithCast<uint8_t>(source->tensor, dest, source->numElements);
                default:
                    QNN_ERROR("copyNativeToFloat: datatype not supported");
                    return StatusCode::FAILURE;
                }
            }
        }
    }
}
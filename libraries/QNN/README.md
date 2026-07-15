# QNN Integration

This folder provides a small wrapper around the QNN SDK to load DLC models and execute them inside the audio engine. The primary entry point is `QnnModel` in `QnnModel.h`.

## SDK Setup
To use the `QnnModel`, you first need some files from the QNN SDK.

1. Download the [Qualcomm Neural Processing SDK for AI](https://www.qualcomm.com/developer/software/neural-processing-sdk-for-ai)
> You do *not* need to run any setup steps to use the SDK! However, this documentation will reference a $QNN_SDK_ROOT variable, which refers to `<download location>/qairt/<qairt version>` in the SDK (e.g. `~/qairt/2.42.0.251225`)
2. Copy the QNN SDK headers from `$QNN_SDK_ROOT/include/QNN` into `dependencies/QNN/include`.
> 
3. Copy the QNN sample app utility files from `$QNN_SDK_ROOT/examples/QNN/SampleApp/SampleApp/src/` into `dependencies/QNN/tools`, excluding `main.cpp`, `CMakeLists.txt`, and `QnnSampleApp.c/hpp`.

On Linux systems, you can run these commands to complete the setup:
```
cp -r $QNN_SDK_ROOT/include/QNN/* dependencies/QNN/include
rsync -a --exclude='main.cpp' --exclude='CMakeLists.txt' --exclude='QnnSampleApp.*' $QNN_SDK_ROOT/examples/QNN/SampleApp/SampleApp/src/ dependencies/QNN/tools/
```

## Typical flow

1. Initialize QNN logging so the QNN macros work and the backend can reuse the logger.
2. Construct `QnnModel` with paths to the backend, DLC, and system library.
3. Call `setup()` once during initialization.
`getOutputTensors()`.
5. Allocate float input/output buffers with `iotensor::allocateIOFloatBuffer()`.
6. For each audio block:
   - Fill input buffers (row-major order).
   - Call `executeGraph()` to run the model.
   - Consume output buffer data.
7. On shutdown, free the float buffers and call `cleanup()`.

> A DLC (Deep Learning Container) is a Qualcomm file format containing a model that can be loaded and run using the QNN SDK. More information is available on the [Qualcomm AI Hub documentation](https://workbench.aihub.qualcomm.com/docs/hub/faq.html#which-qnn-model-format-should-i-use).

> Backend and system libraries can be found under `$QNN_SDK_ROOT/lib/<target architecture>`. For example, to run a project on an OE Linux device targeting the CPU, use the backend library `$QNN_SDK_ROOT/lib/aarch64-oe-linux-gcc11.2/libQnnCpu.so` and the system library `$QNN_SDK_ROOT/lib/aarch64-oe-linux-gcc11.2/libQnnSystem.so`.

## Sample QNN Project

`projects/qnn_sample_project` is an example project using the QNN integration. This project generates audio using a neural network designed to recreate a sine wave. Two models, `full_oscillator_256x2.dlc` and `full_oscillator_512x2.dlc` are provided under `projects/qnn_sample_project/run` with batch sizes of 256 and 512 respectively. This project takes the model, backend, and system library paths as arguments and expects the audio engine's period size to match the model's batch size, e.g.:

```
./ar_audioengine -p 512 --model ./run/full_oscillator_512x2.dlc --backend <path to backend library> --system_library <path to system library>
```
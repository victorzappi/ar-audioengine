# QNN Integration

This folder provides a small, self-contained wrapper for loading Qualcomm Deep
Learning Container (`.dlc`) models and running them inside the audio engine. The
entry point is `ar::qnn::DlcModel` in [`DlcModel.h`](DlcModel.h).

`DlcModel` is an **original implementation written against the public QNN API
only** (`QnnInterface` / `QnnSystemInterface`, `QnnSystemDlc`, `QnnContext`,
`QnnGraph`, `QnnTensor`, `QnnLog`) plus `dlopen`. It does **not** use or derive
from Qualcomm's SDK sample sources, so this wrapper can live in the repository.
The QNN SDK headers it compiles against are proprietary and are **not**
redistributed here — you supply them from your own licensed SDK (see below).

## SDK setup

To build the QNN integration you only need the QNN **headers** — no sample-app
source is required.

1. Download the [Qualcomm AI Runtime SDK / Neural Processing SDK](https://www.qualcomm.com/developer/software/neural-processing-sdk-for-ai).
   This documentation refers to `$QNN_SDK_ROOT`, i.e. `<download location>/qairt/<version>`
   (e.g. `~/qairt/2.45.0.260326`).
2. Copy the QNN SDK headers into `dependencies/QNN/include`:
   ```
   cp -r $QNN_SDK_ROOT/include/QNN/* dependencies/QNN/include
   ```

That's it — the headers are enough. (The `dependencies/QNN/include` and
`dependencies/QNN/tools` folders are kept in the repo but their contents are
gitignored, since the SDK is not redistributable.)

> Backend and system libraries are found under `$QNN_SDK_ROOT/lib/<target arch>`.
> For an OE Linux device targeting the CPU, that is
> `.../libQnnCpu.so` (backend) and `.../libQnnSystem.so` (system library). Swap
> the backend for `libQnnGpu.so`, `libQnnHtp.so`, etc. to run on other hardware.

## Typical flow

```cpp
#include "DlcModel.h"

ar::qnn::DlcModel model(backendPath, dlcPath, systemLibPath);
if (!model.load(/*logLevel=*/1))          // 1=ERROR .. 5=DEBUG
    return;                               // load logs the reason on failure

const auto& in  = model.inputs();         // per-tensor {name, dims, numElements}
const auto& out = model.outputs();

// allocate one float buffer per input/output tensor (size = numElements)
// ... per audio block:
model.execute(0, inputBuffers, outputBuffers);   // float in -> float out
```

The destructor releases the backend, context, DLC handle and libraries; there is
no separate teardown call. `execute()` is fp32 zero-copy (the tensors point
directly at your buffers) — suitable for the real-time audio thread.

> A DLC (Deep Learning Container) is a Qualcomm file format containing a model
> that can be loaded and run with the QNN SDK. See the
> [Qualcomm AI Hub docs](https://workbench.aihub.qualcomm.com/docs/hub/faq.html#which-qnn-model-format-should-i-use).

## Sample QNN project

`projects/qnn_sine` is an example project using this integration: it generates
audio with a small neural network trained to recreate a sine wave. Two models,
`full_oscillator_256x2.dlc` and `full_oscillator_512x2.dlc`, are provided with
batch sizes of 256 and 512. Run it with the audio engine period size matching the
model batch size, e.g.:

```
./ar_audioengine -p 512 \
  --dlc-model  /path/to/full_oscillator_512x2.dlc \
  --qnn-backend /path/to/libQnnCpu.so \
  --qnn-system  /path/to/libQnnSystem.so
```

# QnnModel

This library provides a small, self-contained wrapper for loading a Qualcomm
model and running it inside the audio engine. The entry point is
`ar::qnn::QnnModel` in [`QnnModel.h`](QnnModel.h). It loads either format, chosen
by file extension:

- **`.dlc`** — a Deep Learning Container, composed into a context at load time.
- **`.bin`** — a precompiled context binary (faster init; e.g. for HTP), loaded
  directly.

`QnnModel` is an **original implementation written against the public QNN API
only** (`QnnInterface` / `QnnSystemInterface`, `QnnSystemDlc`, `QnnSystemContext`,
`QnnContext`, `QnnGraph`, `QnnTensor`, `QnnLog`) plus `dlopen`. It does **not**
use or derive from Qualcomm's SDK sample sources, so this wrapper can live in the
repository. The QNN SDK headers it compiles against are proprietary and are
**not** redistributed here — you supply them from your own licensed SDK (see below).

## SDK setup

To build the QNN integration you only need the QNN **headers** — no sample-app
source is required.

1. Download the [Qualcomm AI Runtime SDK / Neural Processing SDK](https://www.qualcomm.com/developer/software/neural-processing-sdk-for-ai).
   This documentation refers to `$QNN_SDK_ROOT`, i.e. `<download location>/qairt/<version>`
   (e.g. `~/qairt/2.48.0.260626`).
2. Copy the QNN SDK headers into `dependencies/QNN/include`:
   ```
   cp -r $QNN_SDK_ROOT/include/QNN/* dependencies/QNN/include
   ```

That's it — the headers are enough. (The `dependencies/QNN/include` folder is
kept in the repo but its contents are gitignored, since the SDK is not
redistributable. No QNN sample-app source is needed.)

> Backend and system libraries are found under `$QNN_SDK_ROOT/lib/<target arch>`.
> For an OE Linux device targeting the CPU, that is
> `.../libQnnCpu.so` (backend) and `.../libQnnSystem.so` (system library). Swap
> the backend for `libQnnGpu.so`, `libQnnHtp.so`, etc. to run on other hardware.

## Typical flow

```cpp
#include "QnnModel.h"

ar::qnn::QnnModel model(backendPath, modelPath, systemLibPath);  // modelPath: .dlc or .bin
if (!model.load(/*logLevel=*/1))          // 1=ERROR .. 5=DEBUG
    return;                               // load logs the reason on failure

const auto& in  = model.inputs();         // per-tensor {name, dims, numElements}
const auto& out = model.outputs();

// allocate one float buffer per input/output tensor (size = numElements)
// ... per audio block:
model.execute(0, inputBuffers, outputBuffers);   // float in -> float out
```

The destructor releases the backend, context, model and libraries; there is no
separate teardown call. Buffers are always float32, and `execute()` is suitable for
the real-time audio thread:

- **float32 tensors** are zero-copy (the tensors point directly at your buffers).
- **Quantized inputs** (8/16-bit fixed point with a per-tensor scale/offset encoding)
  are quantized inside `execute()` into a staging buffer allocated by `load()`, so
  no allocation happens on the audio thread. Other encodings (per-axis, block, ...)
  are rejected at `load()`.
- **Outputs must be float32**; a model with fixed-point outputs fails `load()`. On
  HTP v68 (QCS6490) this means quantizing with `--preserve_io_datatype output_0`, so
  the graph keeps a fixed-point input and a float32 output.

> A DLC (Deep Learning Container) is a Qualcomm file format containing a model
> that can be loaded and run with the QNN SDK. See the
> [Qualcomm AI Hub docs](https://workbench.aihub.qualcomm.com/docs/hub/faq.html#which-qnn-model-format-should-i-use).

## Sample QNN project

`projects/qnn_osc` is an example project using this integration: it generates
audio with a small neural network trained to recreate a sine wave. Two models,
`full_oscillator_256x2.dlc` and `full_oscillator_512x2.dlc`, are provided with
batch sizes of 256 and 512. Run it with the audio engine period size matching the
model batch size, e.g.:

```
./ar_audioengine -p 512 \
  --qnn-model   /path/to/full_oscillator_512x2.dlc \
  --qnn-backend /path/to/libQnnCpu.so \
  --qnn-system  /path/to/libQnnSystem.so
```

## Running on the HTP (NPU)

The HTP backend is split between the CPU and the DSP, so it needs more files on
the device than the CPU backend. Copy them into a single directory (e.g.
`/root/dev/qnn`), all from the **same SDK release**: the CPU-side stub and the
DSP-side skel must match exactly. For the QCS6490 (Hexagon v68):

| File | From `$QNN_SDK_ROOT/lib/...` | Runs on |
|---|---|---|
| `libQnnHtp.so`, `libQnnSystem.so` | `aarch64-oe-linux-gcc11.2/` | CPU |
| `libQnnHtpV68Stub.so` | `aarch64-oe-linux-gcc11.2/` | CPU |
| `libQnnHtpPrepare.so` (only for `.dlc`) | `aarch64-oe-linux-gcc11.2/` | CPU |
| `libQnnHtpV68Skel.so`, `libQnnHtpV68.so` | `hexagon-v68/unsigned/` | DSP |

Then set two environment variables in the shell you run from.
[`projects/qnn_osc/htp_env.sh`](../../projects/qnn_osc/htp_env.sh) does this for
you:

```sh
export LD_LIBRARY_PATH=/root/dev/qnn
export ADSP_LIBRARY_PATH="/root/dev/qnn;/usr/lib/dsp/cdsp;/usr/lib/rfsa/adsp;/dsp"
```

Copy it to the device once, then **source** it in each new shell before
running:

```sh
adb push projects/qnn_osc/htp_env.sh /root/dev/   # once, from the host
adb shell
cd /root/dev
source htp_env.sh                                 # or: . htp_env.sh
```

`QnnModel` opens the backend and system libraries by the full paths you pass, but
those libraries then load their helpers **by name only**, and each variable
tells a loader where to search:

- **`LD_LIBRARY_PATH`** (colon-separated) is read by the Linux dynamic loader.
  `libQnnHtp.so` loads `libQnnHtpV68Stub.so` and `libQnnHtpPrepare.so` by name,
  and these aren't in the system library directories.
- **`ADSP_LIBRARY_PATH`** (semicolon-separated) is read by FastRPC, the library
  that loads code onto the DSP. The stub asks the DSP to load the skel, and
  FastRPC, running in your process, finds that file in these directories and
  sends it to the DSP. Setting the variable **replaces** FastRPC's default
  search list, so the system DSP directories are listed again after yours.

Both variables only affect programs started from that shell after the `export`,
and they don't persist across logins, which is why `htp_env.sh` has to be
sourced again in every new `adb shell`. If the skel can't be found, the failure shows up at
`load()` as a FastRPC or device-creation error that doesn't mention the missing
file. Check `ADSP_LIBRARY_PATH` first, including its spelling.

```
./ar_audioengine -p 256 \
  --qnn-model   /path/to/model.bin \
  --qnn-backend /root/dev/qnn/libQnnHtp.so \
  --qnn-system  /root/dev/qnn/libQnnSystem.so
```

Notes:

- A context binary (`.bin`) loads faster than a `.dlc` and doesn't need
  `libQnnHtpPrepare.so`. Generate it for the target SoC
  (`qnn-context-binary-generator` with a config naming the `soc_model` and `dsp_arch`).
- HTP v68 has no FP16, so a model with a **float32 input fails** on it.
  Quantize with a fixed-point input and a float32 output (see above).
- `<E> unexpected GraphHtpSettings option 66` is logged on every HTP run and is
  harmless.

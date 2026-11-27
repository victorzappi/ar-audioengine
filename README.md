# ar-audioengine

A C++ framework for building real-time audio applications on Linux embedded boards powered by Qualcomm chipsets. 

The developer writes a simple audio callback (`render()`), and the engine takes care of the rest — setting up the [AudioReach](https://audioreach.github.io/index.html) pipeline that streams audio from the CPU through the Qualcomm Hexagon DSP to the hardware codec.

## Branches

Each branch builds on the one above it, and needs more outside this repository. This is
the **`cpu-buffer`** branch.

| Branch | What it adds | QLI image | ADSP firmware | Also needs |
|---|---|---|---|---|
| `main` | The engine: `render()` once per period, audio read and written through AGM (RW mode) | QLI 1.8 or 2.1 (on 2.1: `-s 0 -e 0 -E 1`) | Stock | |
| `mmap` | MMAP mode (`-M`): the engine reads and writes the DSP's shared ring buffers. Blocks shorter than 1 ms. EVL route (`--evl`) | QLI 2.1; the Xenomai (EVL) image for `--evl` | Custom: frame duration | An ACDB with Shared Memory Pull/Push Mode graphs (the QLI 2.1 ACDB has none). For periods under 48 frames, an AGM PCM plugin with a smaller minimum period |
| `sync-start` | `mmap`, plus playback and capture Codec DMAs that start together | As `mmap` | Custom: frame duration, sync start | As `mmap`, plus the AGM branch [`codec-dma-early-device-prepare`](https://github.com/victorzappi/audioreach-graphmgr/tree/codec-dma-early-device-prepare) |
| `cpu-buffer` | `sync-start`, plus the CPU-buffer route (`--cpu-buffer`): the engine reads and writes the Codec DMAs' own buffers, and the DSP does nothing per block | QLI 2.1 Xenomai (EVL) image | Custom: frame duration, sync start, CPU buffer | As `sync-start`, plus the `ar_lpass_map` kernel module (`kernel/ar_lpass_map`) |

**Custom ADSP firmware:** built from Qualcomm's proprietary ADSP sources
(ADSP.HT.5.5.c8-00256.2, `kodiak.adsp.prod`, for the QCS6490), with our changes to the
Codec DMA module. It is not published. A firmware with more changes also runs the
branches above it.

## How it works

AudioReach is Qualcomm's signal processing framework for audio on Linux. It manages graphs of processing modules running on the Hexagon DSP and routes audio between the application processor (CPU) and the codec.

ar-audioengine abstracts away the complexity of AudioReach's mixer controls, graph key values, and backend configuration. You write three functions:

- **`setup()`** — called once before audio starts; initialize your state here
- **`render()`** — called once per audio period; fill the audio buffer with samples
- **`cleanup()`** — called when audio stops; free your resources here

The engine handles the full AudioReach signal chain:

```
CPU (your render callback) → AudioReach → Hexagon DSP → Codec → Audio Output
```

DSP use cases are described by graphs — directed arrangements of processing modules. These graphs are selected at runtime by passing graph key values (GKVs) that identify the stream type, device, device post-processing, and instance.

For more details on AudioReach internals, see the [AudioReach documentation](https://audioreach.github.io/index.html) and the [AudioReach GitHub](https://github.com/Audioreach).

## Target platforms

ar-audioengine targets any Linux board or SoC that supports AudioReach and has a Qualcomm-released toolchain. This includes boards like the Qualcomm RB3 Gen2, with broader platform support expected as AudioReach adoption grows.

## Project structure

```
ar-audioengine/
├── core/                   # Engine source files
│   ├── main.cpp            # Entry point, command-line parsing, audio loop
│   ├── agm_mixer.cpp       # AudioReach graph and mixer control setup
│   ├── hw_mixer.cpp        # Hardware mixer path configuration
│   ├── pcm_utils.cpp       # PCM format utilities
│   ├── cpu_perf.cpp        # CPU affinity and performance-governor control
│   └── default_render.cpp  # Default sine wave renderer
├── include/                # Header files
│   ├── agm_mixer.h
│   ├── hw_mixer.h
│   ├── pcm_utils.h
│   ├── cpu_perf.h
│   ├── render.h            # The render API your project implements
│   ├── audioreach_mappings.h
│   └── optparse.h
├── projects/               # User audio projects
│   ├── sine/
│   │   └── render.cpp      # Example: sine wave generator
│   └── passthrough/
│       └── render.cpp      # Example: first capture channel -> all playback channels
├── CMakeLists.txt
├── LICENSE
└── README.md
```

## Building

ar-audioengine uses CMake. Build directly on the target board or cross-compile with the appropriate Qualcomm toolchain.

Some projects pull in dependencies tracked as git submodules (e.g. libsndfile, RTNeural). Clone the repo with submodules included:

```bash
git clone --recurse-submodules https://github.com/victorzappi/ar-audioengine.git
```

If you already cloned without that flag, fetch them into the existing checkout instead:

```bash
git submodule update --init --recursive
```

### Default project (built-in sine wave)

```bash
cmake -B build
cmake --build build
```

### Custom project

```bash
cmake -B build -DPROJECT_PATH=projects/sine
cmake --build build
```

The `PROJECT_PATH` can be relative to the source directory or absolute. The specified folder must contain a `render.cpp` file. All `.cpp` files in that folder are compiled, and the folder is added to the include path.

### Switching projects

CMake caches the project path. To switch, either delete the build directory:

```bash
rm -rf build
cmake -B build -DPROJECT_PATH=projects/another_project
cmake --build build
```

Or explicitly unset the variable:

```bash
cmake -B build -UPROJECT_PATH
cmake --build build
```

### Build options
```bash
# Debug build (default is Release with -O2)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Custom board-specific config file paths
cmake -B build -DMIXER_PATHS=/path/to/mixer_paths.xml -DBACKEND_CONF_FILE=/path/to/backend_conf.xml -DCARDS_CONF_FILE=/path/to/card-defs.xml
```

If not passed, the default configuration XML files will target the [Qualcomm RB3 Gen 2](https://www.qualcomm.com/developer/hardware/rb3-gen-2-development-kit) board.\
All options can be combined in a single configure command.

### Clean

```bash
cmake --build build --target clean
```

### Cross-compilation

Source the Qualcomm SDK environment, then pass the CMake toolchain file:
```bash
source /opt/qcom-wayland/1.6/environment-setup-armv8-2a-qcom-linux
cmake -B build -DCMAKE_TOOLCHAIN_FILE="$OE_CMAKE_TOOLCHAIN_FILE" -DPROJECT_PATH=projects/sine
cmake --build build
```
If the path to the CMake toolchain file is not exported automatically, look for it in the SDK sysroot (_$OECORE_NATIVE_SYSROOT_). The path should look like this:
```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE="$OECORE_NATIVE_SYSROOT/usr/share/cmake/OEToolchainConfig.cmake" -DPROJECT_PATH=projects/sine
```

## Running

```bash
./build/ar_audioengine [options]
```

**Sound servers:** if the image runs PipeWire or PulseAudio, ar-audioengine works only
while the sound server is stopped. Stop it before running the engine, including its
socket, or the server starts again as soon as a client connects. With systemd user
units, for example:

```bash
# PipeWire
systemctl --user stop pipewire.socket pipewire-pulse.socket pipewire.service pipewire-pulse.service wireplumber.service
# PulseAudio
systemctl --user stop pulseaudio.socket pulseaudio.service
```

### Options

The engine runs full duplex (playback + capture) by default. Pass `-u` /
`--no-capture` for playback only. Most options come in playback and capture
variants, using a case-symmetric convention: playback is the lowercase letter,
capture is the same letter upper-cased (e.g. `-b`/`-B` for bits). The rate/period
options are set once and copied to both streams (per-stream overrides exist as
long options).

#### Shared

| Option | Description | Default |
|---|---|---|
| `-c`, `--virtual-card` | Virtual card number | `100` |
| `-s`, `--physical-card` | Physical card number | `0` |
| `-p`, `--period-size` | Period size (both streams) | `960` |
| `-q`, `--period-count` | Period count (both streams) | `4` |
| `-r`, `--rate` | Sample rate (both streams) | `48000` |
| `-u`, `--no-capture` | Disable capture (playback only) | full duplex on |
| `-a`, `--echo-reference` | Enable the capture←playback echo reference path (only applied when capture is active) | `off` |
| `-m`, `--cpu-affinity` | Pin the audio thread to this 0-based CPU index | unset (no affinity) |
| `-g`, `--performance-governor` | Force every online CPU's scaling governor to `performance` for the run, restored on exit | `off` |
| `-M`, `--mmap` | MMAP mode: read/write the DSP's shared ring buffers directly (needs Shared Memory Pull/Push Mode graphs) | `off` |
| `--evl` | Run the audio thread out-of-band under EVL (Xenomai 4); needs `-M` and a build with `-DEVL=ON` | `off` |
| `--cpu-buffer` | CPU-buffer route: read/write the Codec DMA buffers directly, the DSP does nothing per block; needs `-M`, full duplex, the custom firmware and `ar_lpass_map` (see [CPU-buffer route](#cpu-buffer-route)) | `off` |
| `-h`, `--help` | Print help and exit | |
| `--verbose` | Print engine diagnostics (device/graph setup, mixer ctls, CPU pinning...) | `off` |

`--performance-governor` needs write access to
`/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor` (root, or matching
udev/permission setup) to actually take effect. If a CPU's governor isn't
writable (or has no cpufreq driver), it's skipped with a warning — the flag
never fails the run, it just silently has no effect on that CPU.

By default the engine only prints the startup banner and a message on
successful exit; errors and warnings always print regardless. Pass
`--verbose` to also see per-stream diagnostics (frontend/backend resolution,
PCM config, mixer ctl and graph setup, CPU pinning/governor changes).

#### Playback

| Option | Description | Default |
|---|---|---|
| `-d`, `--playback-virtual-device` | Virtual device number | `100` |
| `-e`, `--playback-physical-device` | Physical device number | `2` |
| `-t`, `--playback-frontend-name` | Frontend device name | auto |
| `-k`, `--playback-backend-name` | Backend device name | auto |
| `-n`, `--playback-channels` | Number of channels | `2` |
| `-b`, `--playback-bits` | Bits per sample | `16` |
| `-f`, `--playback-float` | Use floating-point PCM | `off` |
| `-o`, `--playback-path` | Hardware mixer playback path | `speaker` |
| `-w`, `--streamrx` | Stream graph key value | `PCM_LL_PLAYBACK` |
| `-x`, `--streampp-rx` | Stream PP graph key value | `0` |
| `-y`, `--devicepp-rx` | Device PP graph key value | `DEVICEPP_RX_AUDIO_MBDRC` |
| `-z`, `--devicerx` | Device graph key value | `SPEAKER` |
| `-i`, `--instancerx` | Instance graph key value | `INSTANCE_1` |
| `--playback-period-size` / `-count` / `--playback-rate` | Per-stream overrides of the shared values | shared |

#### Capture

| Option | Description | Default |
|---|---|---|
| `-D`, `--capture-virtual-device` | Virtual device number | `101` |
| `-E`, `--capture-physical-device` | Physical device number | `3` |
| `-T`, `--capture-frontend-name` | Frontend device name | auto |
| `-K`, `--capture-backend-name` | Backend device name | auto |
| `-N`, `--capture-channels` | Number of channels | `2` |
| `-B`, `--capture-bits` | Bits per sample | `16` |
| `-F`, `--capture-float` | Use floating-point PCM | `off` |
| `-O`, `--capture-path` | Hardware mixer capture path | `speaker-mic` |
| `-W`, `--streamtx` | Stream graph key value | `PCM_RECORD` |
| `-X`, `--streampp-tx` | Stream PP graph key value | `0` |
| `-Y`, `--devicepp-tx` | Device PP graph key value | `DEVICEPP_TX_AUDIO_FLUENCE_SMECNS` |
| `-Z`, `--devicetx` | Device graph key value | `SPEAKER_MIC` |
| `-I`, `--instancetx` | Instance graph key value | `INSTANCE_1` |
| `--capture-period-size` / `-count` / `--capture-rate` | Per-stream overrides of the shared values | shared |

Graph key values can be passed as strings (e.g., `SPEAKER` for playback,
`PCM_RECORD` for capture) or hex numbers.

### Example

```bash
./build/ar_audioengine -c 100 -d 100 -k CODEC_DMA-LPAIF_WSA-RX-0 -s 0 -e 0
```

### Synchronized full-duplex start

On this branch, `AE_SYNC_START=1` makes the DSP enable the playback and the capture
Codec DMA interfaces together (within 1 us), when the second stream starts.
`AE_START_STATUS=1` prints, at exit, when each interface was enabled and when its first
DMA interrupt arrived.

```bash
AE_SYNC_START=1 AE_START_STATUS=1 ./build/ar_audioengine -s 0 -e 0 -E 1
```

This needs two changes outside this repository:

- AGM that prepares the Codec DMA backends at `pcm_prepare()`: branch
  [`codec-dma-early-device-prepare`](https://github.com/victorzappi/audioreach-graphmgr/tree/codec-dma-early-device-prepare)
  of audioreach-graphmgr.
- An ADSP firmware whose Codec DMA module supports the sync-start parameter. It is
  built from Qualcomm's proprietary ADSP sources and is not published.

Without that firmware the engine reports that sync start is off and starts the two
streams one after the other. Without that AGM, the interfaces still start together, but
the capture codec powers up after the start, which shifts capture by about 0.5 ms.
Tested in RW mode and in MMAP mode (48-frame blocks).

### CPU-buffer route

With `--cpu-buffer`, the engine works on the Codec DMAs' own double buffers, as Bela does.
When the capture DMA finishes a period, the engine reads it, calls `render()`, and writes
the playback period after the one the playback DMA is reading. The DSP does nothing per
block. From capture to playback takes 2 periods. Each block has one period, less one frame
(the playback DMA's read-ahead), from the end of its capture period until the playback DMA
reads it.

Besides the custom firmware with the CPU-buffer change, this needs:

- **The `ar_lpass_map` kernel module.** It maps the DMA buffers and registers into the
  engine, since the kernel has no `/dev/mem`. Build it against the board's kernel (a
  build tree prepared with `make modules_prepare`), then load it:
  ```bash
  make -C kernel/ar_lpass_map KDIR=<kernel build tree> ARCH=arm64 CROSS_COMPILE=aarch64-qcom-linux-
  ```
  ```bash
  insmod ar_lpass_map.ko
  ```
- **`-M`, full duplex, and the DMAs' channel counts** for `-n` and `-N`. The engine checks
  them against what the firmware reports, and says what to pass if they differ.
- **`--evl`**, with the audio thread on an isolated CPU (`-m`). Without EVL the loop polls
  in a busy wait, and Linux still interrupts it now and then.

Example with 4-frame blocks (83 us at 48 kHz):

```bash
./build/ar_audioengine -s 0 -e 0 -E 1 -M --cpu-buffer --evl -m 7 -p 4 -q 2 -N 1 -w PCM_ULL_PLAYBACK -x 0 -y 0 -z SPEAKER -i INSTANCE_1 -W PCM_ULL_RECORD -X 0 -Y 0 -Z SPEAKER_MIC -I 0
```

At exit the engine reports any late or skipped playback periods. On the RB3 Gen 2 (QLI
2.1 Xenomai image), 4-frame blocks for 10 s: no late or skipped periods with `--evl`, 3
skipped without it.

## Writing a project

Create a folder under `projects/` with at least a `render.cpp` file that implements the three callback functions declared in `render.h`:

```c
#include "render.h"

int setup(struct audio_ctx *ctx, void *user_data)
{
    // Initialize your state
    // ctx->sample_rate, ctx->channels, ctx->period_size are available
    return 0; // return non-zero on failure
}

void render(struct audio_ctx *ctx, void *user_data)
{
    // Fill ctx->audio_out with samples in [-1.0, 1.0]
    // Buffer layout: interleaved channels
    // Buffer size: ctx->period_size * ctx->channels
    for (unsigned int n = 0; n < ctx->period_size; n++) {
        float sample = /* your synthesis here */;
        for (unsigned int c = 0; c < ctx->channels; c++)
            ctx->audio_out[n * ctx->channels + c] = sample;
    }
}

void cleanup(struct audio_ctx *ctx, void *user_data)
{
    // Free any resources
}
```

You can add additional `.cpp` and `.h` files in your project folder — they will be compiled and the folder will be in the include path.

## Dependencies

- [TinyALSA](https://github.com/tinyalsa/tinyalsa) — PCM and mixer interface
- [Expat](https://github.com/libexpat/libexpat) — XML parsing for backend configuration
- [AudioReach/AGM](https://github.com/Audioreach) — Audio Graph Manager libraries and headers
- POSIX threads (pthread)

## License

BSD-3-Clause-Clear. See [LICENSE](LICENSE).

Portions of `agm_mixer.cpp` and `agm_mixer.h` are derived from AudioReach source code, copyright The Linux Foundation and Qualcomm Innovation Center, Inc. See the file headers for details.

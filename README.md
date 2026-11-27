# ar-audioengine

A C++ framework for building real-time audio applications on Linux embedded boards powered by Qualcomm chipsets. 

The developer writes a simple audio callback (`render()`), and the engine takes care of the rest — setting up the [AudioReach](https://audioreach.github.io/index.html) pipeline that streams audio from the CPU through the Qualcomm Hexagon DSP to the hardware codec.

## Branches

Each branch builds on the one above it. This is the **`cpu-buffer`** branch.

| Branch | What it adds | QLI image | ADSP firmware | Also needs |
|---|---|---|---|---|
| `main` | The engine: `render()` once per period, audio read and written through AGM (RW mode) | QLI 1.8 or 2.1 (on 2.1: `-s 0 -e 0 -E 1`) | Stock | |
| `mmap` | MMAP mode (`-M`): the engine reads and writes the DSP's shared ring buffers. Blocks shorter than 1 ms. EVL route (`--evl`) | QLI 2.1; the Xenomai (EVL) image for `--evl` | Custom: frame duration | An ACDB with Shared Memory Pull/Push Mode graphs (the QLI 2.1 ACDB has none). For periods under 48 frames, an AGM PCM plugin with a smaller minimum period |
| `sync-start` | `mmap`, plus playback and capture Codec DMAs that start together | As `mmap` | Custom: frame duration, sync start | As `mmap`, plus the AGM branch [`codec-dma-early-device-prepare`](https://github.com/victorzappi/audioreach-graphmgr/tree/codec-dma-early-device-prepare) |
| `cpu-buffer` | Only the CPU-buffer route: the engine reads and writes the Codec DMAs' own buffers, and the DSP does nothing per block. `sync-start` without its MMAP and read/write routes | QLI 2.1 Xenomai (EVL) image | Custom: frame duration, sync start, CPU buffer, sync align | The AGM branch [`codec-dma-early-device-prepare`](https://github.com/victorzappi/audioreach-graphmgr/tree/codec-dma-early-device-prepare), and the `ar_lpass_map` kernel module (`kernel/ar_lpass_map`). The stock ACDB and AGM PCM plugin are fine |

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

On this branch, AudioReach only sets the path up. The audio itself goes straight between
`render()` and the Codec DMA buffers, and the DSP does nothing per block (see
[CPU-buffer route](#cpu-buffer-route)):

```
mic → Codec → capture DMA buffer → render() → playback DMA buffer → Codec → speaker
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

The engine always runs full duplex: the capture DMA paces the loop. Most options come in
playback and capture variants, using a case-symmetric convention: playback is the
lowercase letter, capture is the same letter upper-cased (e.g. `-w`/`-W` for the stream
graph key). The block (`-p`) is the same for both streams. The defaults are for the RB3
Gen 2 on QLI 2.1 with the stock ACDB.

Each stream's channel count and rate are not options: they come from the backend config
(`/etc/backend_conf.xml`, or `-DBACKEND_CONF_FILE` at build time), which is the format
the Codec DMAs run in. Both backends must have the same rate.

#### Shared

| Option | Description | Default |
|---|---|---|
| `-c`, `--virtual-card` | Virtual card number | `100` |
| `-s`, `--physical-card` | Physical card number | `0` |
| `-p`, `--period-size` | The block: frames per Codec DMA period, both streams | `960` |
| `-m`, `--cpu-affinity` | Pin the audio thread to this 0-based CPU index | unset (no affinity) |
| `-g`, `--performance-governor` | Force every online CPU's scaling governor to `performance` for the run, restored on exit | `off` |
| `--evl` | Run the audio thread out-of-band under EVL (Xenomai 4); needs a build with `-DEVL=ON` | `off` |
| `-h`, `--help` | Print help and exit | |
| `--verbose` | Print engine diagnostics (device/graph setup, mixer ctls, CPU pinning, DMA addresses...) | `off` |

`--performance-governor` needs write access to
`/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor` (root, or matching
udev/permission setup) to actually take effect. If a CPU's governor isn't
writable (or has no cpufreq driver), it's skipped with a warning — the flag
never fails the run, it just silently has no effect on that CPU.

By default the engine only prints the startup banner and a message on
successful exit; errors and warnings always print regardless. Pass
`--verbose` to also see per-stream diagnostics (frontend/backend resolution,
PCM config, mixer ctl and graph setup, CPU pinning/governor changes, the DMAs).

#### Playback

| Option | Description | Default |
|---|---|---|
| `-d`, `--playback-virtual-device` | Virtual device number | `100` |
| `-e`, `--playback-physical-device` | Physical device number | `0` (`CODEC_DMA-LPAIF_WSA-RX-0`) |
| `-t`, `--playback-frontend-name` | Frontend device name | auto |
| `-k`, `--playback-backend-name` | Backend device name | auto |
| `-o`, `--playback-path` | Hardware mixer playback path | `speaker` |
| `-w`, `--streamrx` | Stream graph key value | `PCM_LL_PLAYBACK` |
| `-x`, `--streampp-rx` | Stream PP graph key value | `0` |
| `-y`, `--devicepp-rx` | Device PP graph key value | `DEVICEPP_RX_AUDIO_MBDRC` |
| `-z`, `--devicerx` | Device graph key value | `SPEAKER` |
| `-i`, `--instancerx` | Instance graph key value | `INSTANCE_1` |
| `--playback-watermark` | The playback Codec DMA's FIFO watermark, 1 to 8 32-bit words | one frame |

#### Capture

| Option | Description | Default |
|---|---|---|
| `-D`, `--capture-virtual-device` | Virtual device number | `101` |
| `-E`, `--capture-physical-device` | Physical device number | `1` (`CODEC_DMA-LPAIF_VA-TX-0`) |
| `-T`, `--capture-frontend-name` | Frontend device name | auto |
| `-K`, `--capture-backend-name` | Backend device name | auto |
| `-O`, `--capture-path` | Hardware mixer capture path | `speaker-mic` |
| `-W`, `--streamtx` | Stream graph key value | `RAW_RECORD` |
| `-X`, `--streampp-tx` | Stream PP graph key value | `0` |
| `-Y`, `--devicepp-tx` | Device PP graph key value | `0` |
| `-Z`, `--devicetx` | Device graph key value | `SPEAKER_MIC` |
| `-I`, `--instancetx` | Instance graph key value | `0` |
| `--capture-watermark` | The capture Codec DMA's FIFO watermark, 1 to 8 32-bit words | one frame |

Graph key values can be passed as strings (e.g., `SPEAKER` for playback,
`RAW_RECORD` for capture) or hex numbers; `0` leaves the key out. The stock QLI 2.1
`PCM_RECORD` graph includes Fluence, which the custom firmware does not open:
`RAW_RECORD` reaches the same capture Codec DMA.

### Example

4-frame blocks (83 us at 48 kHz), the audio thread on the isolated CPU 7 under EVL, every
CPU at its highest clock:

```bash
./build/ar_audioengine --evl -m 7 -g -p 4
```

### Synchronized full-duplex start

When the second stream starts, the DSP enables the capture Codec DMA interface, waits
for the capture DMA's first word, and enables the playback interface 10 us later. So the
playback DMA reads the same distance ahead of the capture DMA on every start. Enabled
together, the two interfaces start at a random point of the codecs' 20.8 us frame, and
the playback DMA ends up one frame closer to or further from the capture DMA from one start
to the next: that moved the round trip by up to one block, and at 3-frame blocks it left
some runs with late periods.

`AE_START_STATUS=1` prints, at exit, when each interface was enabled and how many FIFO and
bus errors each DMA had.

```bash
AE_START_STATUS=1 ./build/ar_audioengine --evl -m 7 -g -p 4
```

Without the AGM branch
[`codec-dma-early-device-prepare`](https://github.com/victorzappi/audioreach-graphmgr/tree/codec-dma-early-device-prepare),
the interfaces still start together, but the capture codec powers up after the start,
which shifts capture by about 0.5 ms.

### CPU-buffer route

The engine works on the Codec DMAs' own double buffers, as Bela does. When the capture DMA
finishes a period, the engine reads it, calls `render()`, and writes the first playback
period the playback DMA has not started reading. The DSP does nothing per block. From
capture to playback takes 2 periods, at every block size. Each block has one period less
one frame, give or take 5 us, from the end of its capture period until the playback DMA
reads it: 16 to 24 us at 2-frame blocks. The 5 us is where the two codecs' frame ticks fall
relative to each other, which changes each time the kernel powers them up.

The audio goes only through the DMA buffers. The graphs are still opened and started,
because the Codec DMA modules in them set up the DMAs, but no module runs per block. So
the graphs can be any that reach the speaker and the mic Codec DMAs, and the PCMs are
never read or written: the DMA block is the period given with `-p`, whatever period the
PCM opens with.

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
- **The backend config** for the channels and rate. The engine also checks the channel
  counts against what the firmware reports.
- **`--evl`**, with the audio thread on an isolated CPU (`-m`). Without EVL the loop polls
  in a busy wait, and Linux still interrupts it now and then.
- **`-g`**, the `performance` governor. The out-of-band audio thread may not count as load
  for the default `schedutil` governor, which can then leave its CPU at a low clock.

At exit the engine reports any late or skipped playback periods. On the RB3 Gen 2 (QLI
2.1 Xenomai image, stock ACDB and AGM PCM plugin), `-m 7 -g`, 10 s per run:

| Block | With `--evl` | Without EVL |
|---|---|---|
| 4 frames (83 us) | no late or skipped periods (3 runs) | no late or skipped periods (3 runs) |
| 3 frames (63 us) | no late or skipped periods (3 runs) | |
| 2 frames (42 us) | no late or skipped periods (3 runs, and 3 more of 30 s) | |

Before the aligned start (both interfaces enabled together), without `-g`: 3, 2 and
960-frame blocks had no late or skipped periods with `--evl`; at 4 frames, 8 of 9 runs had
none, and one had 51,392 late and 7,346 skipped of 118,972 (cause not found). Without
either `-g` or EVL, runs at 4 frames had up to 250 late periods in 10 s.

#### Round-trip latency

Measured with the `latency_tester` project (`projects/latency_tester`): a pulse every
second on the speakers, and the samples until the mic hears it. The RB3 Gen 2 has no
line in or out, so a speaker faces the board's microphone (a TDK T5818 on the mainboard;
the speaker outputs are on the interposer). Built with
`-DPROJECT_PATH=projects/latency_tester`:

```bash
./build/ar_audioengine --evl -m 7 -g -p 2
```

With the speaker 1 cm from the mic, one run of 10 s per block size (8 pulses each), the
same for every pulse:

| Block | Round trip | Engine: 2 blocks | The rest |
|---|---|---|---|
| 2 frames | 34 samples (0.71 ms) | 4 | 30 |
| 3 frames | 36 samples (0.75 ms) | 6 | 30 |
| 4 frames | 38 samples (0.79 ms) | 8 | 30 |

The first pulse of each run measured one sample less. With the speaker a few cm away
(3 runs of 10 s per block size) every round trip was 3 samples longer: 37, 39 and 41
samples. Sound travels 0.71 cm in one sample at 48 kHz, so that was about 3 cm.

So the round trip is 2 blocks plus 30 samples: the codecs, the amplifier, the speaker, the
mic, 1.4 samples of air, and how far the playback DMA reads ahead of the capture DMA (the
same on every start with the aligned start). The microphone itself takes under 30 us
(TDK's T5818 datasheet): it only sends its 1-bit PDM stream, and the filtering is in the
QCS6490. An earlier run at 4-frame blocks, with the speaker about 9 cm from the mic,
measured 51 samples: 8 for the engine, and 13 more of air than at 1 cm.

The DMA FIFOs don't add to it. With 16-frame blocks the round trip was 62 samples
(2 x 16 + 30) with the default watermarks (one frame), with a playback watermark of 4 or
8 words (2 or 4 stereo frames) and with a capture watermark of 4 or 8 words, within one
sample. A deeper playback FIFO also removes the FIFO error that the playback DMA reports
at every start.

The tester prints the pulse's peak at the mic and the noise every second. Set
`--threshold` (default 0.1) between the two: at 1 cm the peak was -10.5 dBFS and the noise
about -60 dBFS; with the speakers about 10 cm away the peak was only -25 dBFS, below the
default threshold, and `--threshold 0.02` worked.

## Writing a project

Create a folder under `projects/` with at least a `render.cpp` file that implements the three callback functions declared in `render.h`:

```c
#include "render.h"

int setup(struct audio_ctx *ctx, void *user_data)
{
    // Initialize your state
    // ctx->sample_rate, ctx->period_size, ctx->channels (playback) and
    // ctx->in_channels (capture) are available
    return 0; // return non-zero on failure
}

void render(struct audio_ctx *ctx, void *user_data)
{
    // ctx->audio_in holds the captured samples: ctx->period_size * ctx->in_channels
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

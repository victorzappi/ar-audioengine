# ar-audioengine

A C++ framework for building real-time audio applications on Linux embedded boards powered by Qualcomm chipsets. 

The developer writes a simple audio callback (`render()`), and the engine takes care of the rest — setting up the [AudioReach](https://audioreach.github.io/index.html) pipeline that streams audio from the CPU through the Qualcomm Hexagon DSP to the hardware codec.

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
│   └── default_render.cpp  # Default sine wave renderer
├── include/                # Header files
│   ├── agm_mixer.h
│   ├── hw_mixer.h
│   ├── pcm_utils.h
│   ├── render.h            # The render API your project implements
│   ├── audioreach_mappings.h
│   └── optparse.h
├── projects/               # User audio projects
│   └── sine/
│       └── render.cpp      # Example: sine wave generator
├── CMakeLists.txt
├── LICENSE
└── README.md
```

## Building

ar-audioengine uses CMake. Build directly on the target board or cross-compile with the appropriate Qualcomm toolchain.

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
cmake -B build -DMIXER_PATHS=/etc/mixer_paths_custom.xml -DBACKEND_CONF_FILE=/etc/backend_conf_custom.xml
```

If not passed, the default configuration XML files will target the [Qualcomm RB3 Gen 2](https://www.qualcomm.com/developer/hardware/rb3-gen-2-development-kit) board.\
All options can be combined in a single configure command.

### Clean

```bash
cmake --build build --target clean
```

### Cross-compilation

Source the Qualcomm SDK environment, then pass the toolchain file:
```bash
source /opt/qcom-wayland/1.6/environment-setup-armv8-2a-qcom-linux
cmake -B build -DCMAKE_TOOLCHAIN_FILE="$OE_CMAKE_TOOLCHAIN_FILE" -DPROJECT_PATH=projects/sine
cmake --build build
```

## Running

```bash
./build/ar_audioengine [options]
```

### Options

| Option | Description | Default |
|---|---|---|
| `-c` | Virtual card number | `100` |
| `-d` | Frontend device number | `100` |
| `-B` | Backend device name | `CODEC_DMA-LPAIF_WSA-RX-0` |
| `-C` | Physical card number (Alsa Sink) | `0` |
| `-D` | Physical device number (Alsa Sink) | `0` |
| `-p` | Period size | `960` |
| `-q` | Period count | `4` |
| `-n` | Number of channels | `2` |
| `-r` | Sample rate | `48000` |
| `-b` | Bits per sample | `16` |
| `-f` | Use floating-point PCM | off |
| `-x` | Stream graph key value | `PCM_LL_PLAYBACK` |
| `-y` | Stream PP graph key value | `0` |
| `-w` | Device PP graph key value | `DEVICEPP_RX_AUDIO_MBDRC` |
| `-z` | Device graph key value | `SPEAKER` |
| `-i` | Instance graph key value | `INSTANCE_1` |

Graph key values can be passed as strings (e.g., `SPEAKER`) or hex numbers.

### Example

```bash
./build/ar_audioengine -c 100 -d 100 -B CODEC_DMA-LPAIF_WSA-RX-0 -C 0 -D 0
```

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
    // Fill ctx->audio_buffer with samples in [-1.0, 1.0]
    // Buffer layout: interleaved channels
    // Buffer size: ctx->period_size * ctx->channels
    for (unsigned int n = 0; n < ctx->period_size; n++) {
        float sample = /* your synthesis here */;
        for (unsigned int c = 0; c < ctx->channels; c++)
            ctx->audio_buffer[n * ctx->channels + c] = sample;
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

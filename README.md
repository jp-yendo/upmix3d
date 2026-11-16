# upmix3d

English | [日本語](README-ja.md)

A Windows spatial audio output module for VLC 3.0.x x64. Upmix mono, stereo and multichannel audio to layouts with up to 7.1.4 channels and an additional back-center position. Supports matrix and STFT upmixing, height generation, downmixing and bitstream passthrough.

See the [requirements](Documents/requirements.md), [technical rationale](Documents/requirements-review.md), [implementation and validation coverage](Documents/implementation-status.md), and [internal specification](Documents/システム仕様.md). These documents are in Japanese.

## Build

Requires Visual Studio 2026, the v145 toolset, C++20, the Windows SDK and CMake. The DLL targets x64 only. Python, pip and virtual environments are not used.

### Prepare the VLC SDK

Download the VLC SDK into `external/`, which is excluded from Git. Open PowerShell in the repository root and run:

```powershell
.\scripts\fetch-vlc.ps1
.\scripts\build.ps1 -Configuration Release
```

`fetch-vlc.ps1` downloads the official VLC 3.0.24 x64 archive, verifies its SHA256 checksum and extracts it into `external/`. It requires an internet connection, `curl.exe` and a version of `tar.exe` that supports 7z archives. The main dependency paths are:

```text
external/
  vlc-3.0.24/
    libvlc.dll
    libvlccore.dll
    plugins/
    sdk/
      include/vlc/plugins/vlc_aout.h
      lib/libvlccore.lib
```

For an offline environment, place a downloaded VLC distribution in `external/vlc-3.0.24/`. If `tar.exe` cannot extract 7z archives, use 7-Zip or another compatible tool to extract the downloaded and verified `external/vlc-3.0.24-win64.7z` into `external/`, preserving the directory structure above. Building requires `sdk/`; the integration tests also require the VLC binaries and `plugins/`.

### SDK path and build outputs

The default SDK comes from the official VLC 3.0.24 portable distribution. To use an existing SDK, pass `-VlcRoot C:\path\to\vlc` to the build script. When opening `upmix3d.vcxproj` in Visual Studio, set the SDK path through the MSBuild `VlcRoot` property or the `VLC_ROOT` environment variable.

| Build method | Output DLL |
|---|---|
| CMake / build.ps1 | `build/Release/libupmix3d_plugin.dll` |
| Visual Studio project | `build/vs/Release/libupmix3d_plugin.dll` |

The MSVC runtime is linked statically into the DLL. VLC's `libvlccore.dll` is linked dynamically.

## Validation

```powershell
# Core tests and performance measurement
.\scripts\verify.ps1

# Silent device tests using portable VLC
.\scripts\verify.ps1 -DeviceTests

# Process two hours of audio faster than real time for DSP endurance testing
.\scripts\verify.ps1 -LongDsp
```

Device tests require an output device that supports spatial audio, exclusive PCM and encoded audio. Test settings and logs are stored in `build/integration/`. The tests do not change the Windows default audio device or files in an installed VLC distribution.

The silent integration tests exercise pause, resume, seeking, volume, mute, draining, settings persistence and device selection. Included silent fixtures cover AC-3, DTS, E-AC-3 and TrueHD. The DTS-HD transport test wraps a DTS core stream in the HDMI high-bit-rate format.

To check physical speaker positions, explicitly enable test playback:

```powershell
.\build\Release\upmix3d_speaker_check.exe --list
.\build\Release\upmix3d_speaker_check.exe --play
.\build\Release\upmix3d_speaker_check.exe --play --back-center
```

The tool displays each position and plays its channel for approximately one second. It uses pink noise for regular channels and a 60 Hz tone for LFE, with a reference gain of -30 dB. Use `--device` with an enumerated device ID and `--layout` to choose a layout.

## Use with VLC

Close VLC, copy the DLL into the target VLC distribution's `plugins/audio_output/` directory, then restart VLC. Select `upmix3d` as the audio output module.

```powershell
vlc.exe --aout=upmix3d --upmix3d-layout=7.1.4 movie.mkv
```

Enable Windows spatial sound to use height channels. The output format and speaker rendering depend on Windows and the connected device. Configure the module in VLC's preferences with the settings view set to **All**. DSP and layout settings take effect when a stream starts; volume, mute and device selection also work during playback.

| Setting | Behavior |
|---|---|
| `upmix3d-layout` | 3.1 / 4.0 / 5.1 / 7.1 / 2.1.2 / 3.1.2 / 5.1.2 / 5.1.4 / 7.1.2 / 7.1.4. Default: 7.1.4 |
| `upmix3d-downmix` | OFF by default. Retains input positions; logs and folds down positions when device capabilities require it |
| `upmix3d-method` | `matrix` / `frequency`. Default: STFT with a 2048-sample window and 512-sample hop |
| `upmix3d-preset` | `movie` / `music` / `wide` / `custom`. Editing individual values selects a custom preset |
| `upmix3d-preset-5-1` and related options | Assign presets to input layouts from 1.0 through 8.1. `inherit` uses the overall setting |
| `upmix3d-route` | Output route without heights: `spatial` / `shared` / `exclusive` |
| `upmix3d-passthrough` | `0`: disabled / `1`: AC-3 and DTS core / `2`: all formats. Default: 2. Bypasses DSP and volume |
| `upmix3d-top-middle` | Places two height channels directly overhead using dynamic objects |
| `upmix3d-back-center` | `dynamic` / `fold` when a native static back-center position is unavailable |
| `upmix3d-latency` | `low` / `standard` / `quality`. Low latency uses matrix processing and shorter buffers |
| `upmix3d-protection` | `fixed` / `normalize` / `limiter`. Default: limiter |
| `upmix3d-true-peak` | ON by default. Uses a conservative bound from a 4x interpolation FIR with 23 samples of lookahead |
| `upmix3d-threshold-db` | Default: -1. Uses dBTP with true-peak protection enabled, dBFS otherwise |
| `upmix3d-transition-ms` | Smooths playback transitions. Default: 5 ms |
| `upmix3d-spatial-offset-ms` and related options | Saves separate corrections from -500 to +500 ms for spatial, shared and exclusive output |

Normalization targets instantaneous peaks, not programme loudness. When input and output layouts match, gain is 0 dB and the limiter is inactive, PCM sample values are preserved apart from the reported delay.

Check the receiver's Atmos indication, physical speaker positions, audiovisual synchronization and audible quality with the actual equipment. Silent automated tests and accelerated processing of two hours of audio do not establish synchronization during two hours of real-time video playback.

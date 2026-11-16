# Developer Reference

English | [日本語](developer-reference-ja.md)

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
| --- | --- |
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

The silent integration tests exercise pause, resume, seeking, volume, mute, draining, settings persistence, device selection and the speaker test. Included silent fixtures cover AC-3, DTS, E-AC-3 and TrueHD. The DTS-HD transport test wraps a DTS core stream in the HDMI high-bit-rate format.

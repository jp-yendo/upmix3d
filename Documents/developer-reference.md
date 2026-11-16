# Developer Reference

English | [日本語](developer-reference-ja.md)

## Prerequisites

- Visual Studio 2026 (Desktop development with C++, including the CMake tools)
- An internet connection (to fetch the VLC SDK the first time)

## Building

Open PowerShell in the repository root and run:

```powershell
.\scripts\fetch-vlc.ps1
.\scripts\build.ps1 -Configuration Release
```

The first line fetches the VLC 3.0.24 SDK into `external/` (first time only). The second builds the module, runs the core tests and creates `build/Release/libupmix3d_plugin.dll`.

To use Visual Studio, open `upmix3d.vcxproj`. Its output folder is `build/vs/Release/`.

### Using an existing VLC SDK

Pass `-VlcRoot C:\path\to\vlc` to `build.ps1`. In Visual Studio, set the `VLC_ROOT` environment variable to the SDK folder.

### Without an internet connection

Extract the portable VLC 3.0.24 x64 distribution into `external/vlc-3.0.24/`.

## Testing

```powershell
# Core tests and performance measurement
.\scripts\verify.ps1

# Integration tests with real VLC and the output device (no sound is played)
.\scripts\verify.ps1 -DeviceTests

# Endurance test that processes two hours of audio
.\scripts\verify.ps1 -LongDsp
```

The integration tests need an output device that supports spatial sound and exclusive mode and can send bitstream audio to an AV receiver. Logs are saved in `build/integration/`.

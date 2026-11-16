# upmix3d

English | [日本語](README-ja.md)

An audio output module for VLC on 64-bit Windows. It spreads stereo, 5.1 and other audio across speaker setups of up to 7.1.4, including height speakers (speakers placed on the ceiling or high on the walls).

## Requirements

- Windows 10 (version 1703 or later) or Windows 11, 64-bit
- VLC 3.0.x, 64-bit
- For height speakers: an output device that supports Windows spatial sound, such as Dolby Atmos for home theater
- For bitstream output: an AV receiver connected through HDMI or similar

## Installation

1. Close VLC.
2. Copy `libupmix3d_plugin.dll` into the `plugins\audio_output\` folder of your VLC installation.
3. Start VLC, open **Tools > Preferences > Audio**, select **upmix3d spatial upmix output** as the **Output module** and click **Save**. When VLC is in Japanese, it appears as 「upmix3d 空間アップミックス出力」.
4. To use height speakers, turn on spatial sound for your output device in the Windows sound settings.

## Settings

In VLC, open **Tools > Preferences**, set **Show settings** at the bottom left to **All**, then go to **Audio > Output modules > upmix3d**. Changes take effect the next time playback starts.

| Setting | Description |
| --- | --- |
| Output layout | Your speaker layout. Default: 5.1.2 |
| Preset | How the sound is spread: Movie, Music or Music (wide) |
| Bitstream passthrough | Whether Dolby Digital, DTS and similar audio is sent to your AV receiver unchanged. VLC's volume does not apply while it is, so adjust the volume on your AV receiver |
| Center extraction, Center width | How much of the sound in the middle, such as dialogue, goes to the center speaker |
| Generated side / back level, Height level | Level of the spread sound |
| Level protection | How clipping on loud sounds is prevented. The default, Limiter, is fine for most uses |
| Latency correction for spatial sound / shared output / exclusive output | Adjust these if audio and video are out of sync |

To return every setting to its default, turn on **Reset all settings at next playback**, save, and start playback.

## Troubleshooting

- **Not listed under Output module:** Check that VLC is the 64-bit version (installed under `Program Files`, not `Program Files (x86)`) and that the DLL is in `plugins\audio_output\`, then restart VLC.
- **No sound from the height speakers:** Check that spatial sound is turned on for your output device in the Windows sound settings.
- **Bitstream output does not work:** Make sure no other app is playing sound. Also open **Sound settings > More sound settings**, open the properties of the output device and, on the **Advanced** tab, turn on **Allow applications to take exclusive control of this device**.

When the output cannot follow your settings, VLC shows the cause and what to do.

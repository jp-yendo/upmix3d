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
3. Start VLC, open **Tools > Preferences > Audio** and select `upmix3d` as the **Output module**.
4. To use height speakers, turn on spatial sound for your output device in the Windows sound settings.

## Settings

In VLC, open **Tools > Preferences**, set **Show settings** at the bottom left to **All**, then go to **Audio > Output modules > upmix3d**. Changes take effect the next time playback starts.

| Setting | Description |
| --- | --- |
| Output layout | Your speaker layout. Default: 7.1.4 |
| Preset | How the sound is spread: Movie, Music or Music (wide) |
| Bitstream passthrough | Whether Dolby Digital, DTS and similar audio is sent to your AV receiver unchanged. VLC's volume does not apply while it is, so adjust the volume on your AV receiver |
| Center extraction, Center width | How much of the sound in the middle, such as dialogue, goes to the center speaker |
| Generated side / back level, Height level | Level of the spread sound |
| Level protection | How clipping on loud sounds is prevented. The default, Limiter, is fine for most uses |
| Latency correction for spatial sound / shared output / exclusive output | Adjust these if audio and video are out of sync |

## Checking speaker positions

1. In the **upmix3d** settings, turn on **Speaker test at next playback** and save.
2. Play any video or music.
3. Each speaker plays a sound in turn, and the name of the speaker that is playing appears on screen. Click **Stop** to end the test early.

## Troubleshooting

- **No sound from the height speakers:** Check that spatial sound is turned on for your output device in the Windows sound settings.
- **Bitstream output does not work:** Make sure no other app is playing sound. Also open **Sound settings > More sound settings**, open the properties of the output device and, on the **Advanced** tab, turn on **Allow applications to take exclusive control of this device**.

When the output cannot follow your settings, VLC shows the cause and what to do.

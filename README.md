# upmix3d

English | [日本語](README-ja.md)

An audio output module for VLC on 64-bit Windows. It spreads stereo, 5.1 and other audio across speaker setups of up to 7.1.4, including height speakers (speakers placed on the ceiling or high on the walls).

- Creates center, surround and height sound from stereo or mono audio.
- Keeps the original channels of 5.1 and 7.1 audio and fills in the missing positions.
- Sends Dolby Digital, DTS, Dolby Digital Plus, Dolby TrueHD and DTS-HD audio to your AV receiver without decoding it (bitstream output).

## Requirements

- Windows 10 (version 1703 or later) or Windows 11, 64-bit
- VLC 3.0.x, 64-bit
- For height speakers: an output device that supports Windows spatial sound, such as Dolby Atmos for home theater
- For bitstream output: an AV receiver connected through HDMI or similar that can play the format

## Installation

1. Close VLC.
2. Copy `libupmix3d_plugin.dll` into the `plugins\audio_output\` folder of your VLC installation.
3. Start VLC, open **Tools > Preferences > Audio** and select `upmix3d` as the **Output module**.

To use height speakers, turn on spatial sound for your output device in the Windows sound settings.

## Settings

In VLC, open **Tools > Preferences**, set **Show settings** at the bottom left to **All**, then go to **Audio > Output modules > upmix3d**. Volume, mute and the output device change during playback. Other settings take effect the next time playback starts.

### Main settings

| Setting | Description |
| --- | --- |
| Output layout | Your speaker layout. Default: 7.1.4. The value in parentheses is the channel count including height speakers; for example, 5.1.2 has the same 8 channels as 7.1 |
| Preset | How the sound is spread: Movie, Music or Music (wide). Changing an individual value switches to Custom |
| Preset for ... input | Use a different preset for stereo, 5.1 or other input |
| Downmix to the selected layout | When on, sound for positions missing from the output layout is moved to nearby speakers. When off, the original channels are kept |
| Bitstream passthrough | Disabled, AC-3 and DTS core, or All supported formats (default). VLC's volume does not apply to bitstream output, so adjust the volume on your AV receiver |
| Output route (without heights) | Output method for layouts without height speakers: Windows spatial sound, WASAPI shared (default) or WASAPI exclusive. Layouts with height speakers always use spatial sound |
| Latency mode | Low latency is lighter and delays the sound less. High quality separates sounds better but adds more delay |

### Sound adjustments

| Setting | Description |
| --- | --- |
| Center extraction | How much of the sound in the middle of a stereo image (mostly dialogue) moves to the center speaker |
| Center width | At 0 that sound comes only from the center speaker; higher values spread it to the left and right |
| Front/back dimension | Positive values move the sound field toward the rear |
| Generated side / back level, Height level | Level of the created sound |
| Attenuate dialogue band | Keeps dialogue from being heard from the surround and height speakers |
| Overall gain, LFE level | Overall volume and the amount of bass sent to the subwoofer |

### Clipping protection

| Setting | Description |
| --- | --- |
| Level protection | Limiter (default) lowers the volume only at loud moments and then restores it. Normalization lowers the volume to fit the loudest moment and keeps it until playback restarts or you seek. Fixed always lowers the volume by 6 dB |
| Measure true peaks | When on, also prevents clipping from peaks that fall between digital samples |
| Limiter threshold | Level at which the volume starts to be lowered. Default: -1 dB |

The limiter and normalization do nothing to sound that is not loud enough to clip. When the audio already matches the output layout and the overall gain and LFE level are 0 dB, it plays unchanged.

### Audio and video sync

If your AV receiver or other equipment delays the sound, adjust the latency correction for each output method (**Spatial / Shared WASAPI / Exclusive WASAPI latency correction**) between -500 and +500 ms.

## Checking speaker positions

1. In the **upmix3d** settings, turn on **Speaker test at next playback** and save.
2. Play any video or music.
3. Each speaker plays noise for about one second in turn (a low tone for the subwoofer), and the name of the speaker that is playing appears on screen. Click **Stop** to end the test early.

During the test, the test sound replaces the audio being played; the audio returns when the test ends. The setting turns itself off when the test starts, so later playback is normal. If the media plays through bitstream output, the test does not run and the setting stays on.

## Notes

- When the output cannot follow your settings (spatial sound is off, the device lacks some speakers, bitstream output is unavailable and so on), VLC shows a message with the cause and what to do. Each message appears once while VLC is running.
- Whether your AV receiver shows Atmos and which speakers actually play depend on Windows and AV receiver settings.
- The sound for the height speakers is created from the reverberation in the original audio. It does not carry per-sound position information like Atmos does.

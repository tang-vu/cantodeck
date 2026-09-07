# CantoDeck

Experimental Windows desktop karaoke console, C++20 / JUCE 8.0.15, AGPL-3.0-only.

[![Windows CI](https://github.com/tang-vu/cantodeck/actions/workflows/windows.yml/badge.svg)](https://github.com/tang-vu/cantodeck/actions/workflows/windows.yml) [![License: AGPL v3](https://img.shields.io/badge/License-AGPL_v3-blue.svg)](LICENSE)

**Development preview: not ready for reliable live karaoke through external speakers.** The owner reports objectionable monitoring delay and vocal quality. Successful builds, WAV tests and short silent device probes do not establish acceptable singing performance. Binaries are unsigned; release 0.1 acceptance is incomplete.

[Hướng dẫn tiếng Việt](README.vi.md) · [Status/evidence](docs/STATUS.md) · [Roadmap](docs/ROADMAP.md) · [Specification](docs/SPECIFICATION.md) · [Contributing](CONTRIBUTING.md)

## Preview downloads

Successful [Windows CI runs](https://github.com/tang-vu/cantodeck/actions/workflows/windows.yml) attach an unsigned portable artifact with the executable, matching CantoDeck/JUCE source and license notices. A GitHub login may be required to download workflow artifacts. These are development builds, not a stable release. You can also build locally below.

## Build and run

Windows 11 x64, Visual Studio 2022 Desktop development with C++, Windows SDK, CMake 3.22+, Git. Initial build downloads the pinned JUCE source; the application needs no network or account.

```powershell
./scripts/build.ps1
./scripts/verify.ps1
& ./build/CantoDeck_artefacts/Release/CantoDeck.exe
./scripts/package.ps1
```

Select a microphone and speakers/headphones, Connect, check the real input meter, then deliberately enable monitoring. Start with wired headphones and low hardware volume. Open a local mono/stereo WAV, Play, then Record to a new WAV. Output test is opt-in and quiet. Monitoring is off at startup and after device changes.

The main view has mic/music/master gain, meters, six editable vocal presets, echo/room controls, WAV transport/seek/session queue, lyrics and recording. Advanced exposes channel mapping, sample rate/buffer requests, effect bypass, tone EQ/compressor, JSON import/export and diagnostics. F11 in the separate lyrics window toggles fullscreen.

## Audio and recording semantics

One mono mic bus selects channel 1, channel 2, or an equal average of the first two input channels. Stereo music and mono processed voice feed stereo output. Two separate USB microphones are not implemented.

Signal path: input mapping → clock bridge → smoothed software input boost (0–24 dB, default 0) → fixed 75 Hz high-pass → optional gentle expander → optional broad tone EQ → optional compressor → smoothed mic gain (0–8x) → parallel echo and four damped room combs → monitored voice plus stereo WAV → smoothed master → sample-peak clamp at 0.95. Input meters and dry recording remain pre-boost. The broad tone control is not a full parametric EQ. The room effect is a basic algorithmic reverberator. No de-esser or automatic anti-feedback is included.

Recording includes the internal output mix (including master/mute and any user-triggered test tone), not Windows/browser audio. Optional dry and wet mono WAVs use the same frame clock; these taps are independent of the monitoring toggle/master fader. MUTE ALL also silences stems. While RECORD is active, microphone stems continue even if monitoring is disabled. Recording stops on queue overflow, write failure or the conservative 3-hour RIFF limit. Stop to finalize; an interrupted/crashed process may leave unfinalized WAV headers. No acoustic or backing-track latency compensation is applied.

WAV files are decoded on a worker into RAM before playback, capped at 129,600,000 samples (45 minutes at 48 kHz), maximum two channels. This can use approximately 1 GiB, temporarily more while replacing a track. Track/device/record changes are disabled while loading; emergency mute remains available. Do not change tracks during recording. End of track stops; choose the next queue item explicitly. Queue and lyric files are not persisted yet. MP3, FLAC and video are disabled.

The default compatibility resampler prebuffers three actual blocks (minimum 512 samples). Optional Advanced → Low latency uses WASAPI shared low-latency and requests 128 samples; press Connect to apply. Its FIFO target is one output block converted to input frames plus two capture blocks and two interpolation guard frames (minimum 128). Actual driver periods may be larger than requested. It uses linear interpolation, not a mastering-quality bandlimited converter. Driver latency, FIFO occupancy, callback load and error counters are reported separately; none is a measured round-trip latency. Disable low-latency mode if unsupported or unstable; it does not use exclusive mode.

## Offline commands

GUI-subsystem executables should be run with `Start-Process -Wait -PassThru` when checking their exit status in PowerShell.

```powershell
Start-Process ./build/CantoDeck_artefacts/Release/CantoDeck.exe -Wait -ArgumentList '--diagnostics diagnostics.txt'
Start-Process ./build/CantoDeck_artefacts/Release/CantoDeck.exe -Wait -ArgumentList '--render input.wav output.wav'
Start-Process ./build/CantoDeck_artefacts/Release/CantoDeck.exe -Wait -ArgumentList '--probe probe.txt'
Start-Process ./build/CantoDeck_artefacts/Release/CantoDeck.exe -Wait -ArgumentList '--probe-low probe-low.txt'
```

`--render` processes the input as mic audio through the actual engine callback graph and writes mix/dry/wet 24-bit WAVs including two seconds of tail. It reopens the mix to verify frame count and channels. `--probe` opens the first available mic/output for three seconds with monitoring off, without recording or tones. It reports actual backend behavior, not audibility. Diagnostics from the UI omit endpoint names; command-line diagnostics include endpoint display names for local troubleshooting. Check before sharing. Commands refuse an existing destination.

License and dependency details: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Hardware recipes and troubleshooting: [README.vi.md](README.vi.md). Architecture: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

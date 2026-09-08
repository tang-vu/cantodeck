# CantoDeck

Experimental Windows desktop karaoke console, C++20 / JUCE 8.0.15, AGPL-3.0-only.

[![Windows CI](https://github.com/tang-vu/cantodeck/actions/workflows/windows.yml/badge.svg)](https://github.com/tang-vu/cantodeck/actions/workflows/windows.yml) [![License: AGPL v3](https://img.shields.io/badge/License-AGPL_v3-blue.svg)](LICENSE)

**Development preview: not ready for reliable live karaoke through external speakers.** The owner reports objectionable monitoring delay and vocal quality. Successful builds, WAV tests and short silent device probes do not establish acceptable singing performance. Binaries are unsigned; release 0.1 acceptance is incomplete.

[Hướng dẫn tiếng Việt](README.vi.md) · [Status/evidence](docs/STATUS.md) · [Roadmap](docs/ROADMAP.md) · [Specification](docs/SPECIFICATION.md) · [Contributing](CONTRIBUTING.md)

## Preview downloads

To identify the executable being tested, check its Advanced diagnostics or title tooltip. Portable packages include `BUILD_INFO.json` with the embedded revision, configuration time, JUCE pin, architecture and executable SHA256. A `-dirty` revision denotes local uncommitted changes; keep the matching source bundle when reporting results. These identifiers do not mean a build is signed or has passed hardware acceptance.

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

The low-latency bridge (also used by the native backend) starts with its existing small FIFO target. After an actual FIFO
underrun it adds a bounded jitter reserve: one quarter of the summed capture quantum
and output quantum converted to input frames (minimum 32 frames), at most twice,
with the total target capped at 8192 frames. The current target is shown in diagnostics.
It resets on reconnect and never shrinks automatically during a connection. This
trades extra buffering for recovery when delivery is irregular; the triggering
underrun can still be audible. Compatibility mode retains its fixed target.
It is not a guarantee against scheduling gaps or a measured latency claim.

LRC `[offset:+500]` adjusts lyric lookup by milliseconds: positive values show lyrics
earlier, negative values later. It adds to the manual lyric offset and does not alter
audio timing. Signed integers with at most six digits and magnitude at most 600000 ms
are accepted; invalid tags are ignored and the last valid tag wins globally. A newly
loaded lyric file resets the file offset; TXT retains literal tags. Convention reference:
[paroles LRC parser documentation](https://github.com/Clarkkkk/paroles). No parser dependency
or third-party code was added.

One mono mic bus selects channel 1, channel 2, or an equal average of the first two input channels. Stereo music and mono processed voice feed stereo output. Two separate USB microphones are not implemented.

Signal path: input mapping → clock bridge → smoothed software input boost (0–24 dB, default 0) → vocal processing and mic gain → monitored voice plus stereo WAV → smoothed master → stereo-linked sample-peak limiter at 0.95. Transparent voice (default in the UI) bypasses coloration and effects while retaining gain. The processed path offers a 75 Hz high-pass, optional expander, legacy broad tone control plus three parametric bell EQ bands, compressor, echo and four damped room combs. Input meters and dry recording remain pre-boost. The room effect is basic. No de-esser, true-peak limiter or automatic anti-feedback is included.

Advanced → EQ 3 bands edits each band's frequency, gain and Q; Vocal EQ bypasses both tone and parametric EQ. Transparent voice bypasses them regardless of that toggle. All bands start at 0 dB; named presets reset their gains. Settings persist in JSON presets/sessions. Parameter smoothing changes filter response over time but adds no lookahead audio queue. Physical sound quality remains unverified.

Room now diffuses its four comb returns through four allpass sections. This spreads the wet reflections without placing those delays on the dry vocal bus. It remains a basic mono room effect with fixed decay, not a stereo or convolution reverb. Mathematical stability/response checks are separate from listening acceptance.

Recording includes the internal output mix (including master/mute and any user-triggered test tone), not Windows/browser audio. Optional dry and wet mono WAVs use the same frame clock; these taps are independent of the monitoring toggle/master fader. MUTE ALL also silences stems. While RECORD is active, microphone stems continue even if monitoring is disabled. Recording stops on queue overflow, write failure or the conservative 3-hour RIFF limit. Stop to finalize; an interrupted/crashed process may leave unfinalized WAV headers. No acoustic or backing-track latency compensation is applied.

WAV playback now reads ahead on a worker into eight 1024-frame stereo blocks instead of decoding the entire song into RAM. Sample storage is bounded (about 64 KiB queued, plus current/worker scratch blocks; excluding codec metadata and OS caching). Mono/stereo, 8–192 kHz, up to 24 hours of declared audio are accepted; long-file/storage-failure hardware tests remain open. File reads and retired-reader destruction stay outside callbacks. Seeking uses source-position-tagged blocks and bounded consumer work. While data is unavailable, only music is silent and its timeline waits; mic processing/recording continue. Diagnostics count music wait blocks (including seeks), not necessarily audible disk underruns. Read errors stop music. Music rate conversion remains linear interpolation, not the vocal sinc converter.

Track/device/record changes are disabled while opening a track; emergency mute remains available. Do not change tracks during recording. End of track stops; choose the next queue item explicitly. The local session persists up to 128 queued WAV paths and lyric offset. Startup restores the list without opening/selecting/playing media; select a song explicitly, and missing files report a load error then. Clear queue removes only list entries, not files or the currently loaded song. Playlist paths and lyric offset are excluded from exported vocal presets. Lyric-file selection is not persisted yet. MP3, FLAC and video are disabled.

The default compatibility bridge prebuffers three actual blocks (minimum 512 samples). Advanced → Low latency requests smaller shared-mode periods; actual driver periods may be larger. The bridge uses a 32-tap windowed-sinc interpolator with 16 input-frame lookahead. Low-latency FIFO targets include an output quantum converted to input frames, one capture quantum and 16 guard frames (minimum 128). Experimental Advanced → Native selects a direct WASAPI backend with separate period/capacity/queue diagnostics. Press Connect to apply backend changes. Neither route uses exclusive mode. Driver latency and queue counters are not measured round-trip latency. See [audio rework evidence and limitations](docs/AUDIO_REWORK.md).

Advanced also offers an explicit loop-measurement button that emits a quiet test signal. Pause external audio first. Monitoring stays off afterward; invalid returns are rejected. The measurement implementation has synthetic regression coverage but has not been physically validated.

## Offline commands

`scripts/verify.ps1` also runs `--test-playback input.wav report.txt` with a generated short fixture (0.5–10 seconds). This verifies streaming playback/pause/seek/end behavior and a backing-only stereo recording at `report.wav`, without opening audio devices. Use synthetic non-silent input for this diagnostic; existing report/recording destinations are refused.

GUI-subsystem executables should be run with `Start-Process -Wait -PassThru` when checking their exit status in PowerShell.

```powershell
Start-Process ./build/CantoDeck_artefacts/Release/CantoDeck.exe -Wait -ArgumentList '--diagnostics diagnostics.txt'
Start-Process ./build/CantoDeck_artefacts/Release/CantoDeck.exe -Wait -ArgumentList '--render input.wav output.wav'
Start-Process ./build/CantoDeck_artefacts/Release/CantoDeck.exe -Wait -ArgumentList '--probe probe.txt'
Start-Process ./build/CantoDeck_artefacts/Release/CantoDeck.exe -Wait -ArgumentList '--probe-low probe-low.txt'
```

`--render` processes the input as mic audio through the actual engine callback graph and writes mix/dry/wet 24-bit WAVs including two seconds of tail. It reopens the mix to verify frame count and channels. `--probe` opens the first available mic/output for three seconds with monitoring off, without recording or tones. It reports actual backend behavior, not audibility. Diagnostics from the UI omit endpoint names; command-line diagnostics include endpoint display names for local troubleshooting. Check before sharing. Commands refuse an existing destination.

License and dependency details: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Hardware recipes and troubleshooting: [README.vi.md](README.vi.md). Architecture: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

Fault handling: a latched device error silences mix and dry/wet recording taps. The GUI stops/finalizes an active take on its control timer, after any pending track-load operation. Physical device-loss recovery still requires validation. `--render` additionally writes `output-fault.wav`, `output-fault-dry.wav` and `output-fault-wet.wav` (using the chosen output basename), injecting a software fault with nonzero synthetic input and verifying all 4096 recorded frames are silent. It never opens audio streams for this test and refuses existing recording destinations.

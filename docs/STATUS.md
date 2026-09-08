# Implementation and evidence — 2026-09-07

**Active work resumed 2026-09-08:** see [AUDIO_REWORK.md](AUDIO_REWORK.md) for the native backend, transparent path, improved resampling/limiting, measurement tool and current verification state. The older checkpoint details below remain historical evidence; they are not a claim that the current goal is complete.

## Outcome

A native Windows x64 development-preview executable has been built and launched. The owner reports unacceptable delay and unnatural vocal sound when singing through external speakers. The real-time software path exists but the primary karaoke experience has not passed acceptance. Physical round-trip/30-minute soak gates are open, so release 0.2 has not started. No system audio defaults or drivers were changed. Repository publication/CI are separate from audio acceptance; see GitHub Actions for hosted build results.

## Implemented

Latency follow-up: added opt-in shared WASAPI low-latency mode, supported-buffer negotiation (including a 128-sample request), and a smaller capture/output-aware FIFO target. Deterministic drift tests include both compatibility/low-latency modes and asymmetric 128-input/480-output periods. One smaller buffer candidate failed this test and was not shipped.

Silent native probe at 48 kHz on 2026-09-07: DGM20 input + DGM20 headphones negotiated 128/128 samples, driver-reported latency 128/128 samples, FIFO target 386, end-of-three-second occupancy 241, under/overruns 0/0. Report: `build/low-latency-dgm.txt`. DGM20 + Realtek negotiated 128/480 samples. No tones, monitoring, recording, or round-trip measurement was used. These short callback checks do not establish audible latency or sustained reliability. User must restart the old executable and enable Advanced → Low latency → Connect to use this path.

Low microphone level follow-up: the previous mic fader was limited to 2x (+6 dB), with default master 0.5 (-6 dB). Added explicit smoothed input boost 0–24 dB before the expander/compressor and expanded the post-compressor mic fader to 8x. Defaults/saved levels are not automatically increased. A weak-signal RMS regression checks +12 dB boost at 44.1/48 kHz. CLI diagnostics now read Windows endpoint input level/mute without changing them. Actual audible loudness still needs user verification.

Read-only Windows endpoint inspection on 2026-09-07 confirmed DGM20 USB Microphone input level 100.0%, muted=no. Its low audible level cannot be attributed to a low Windows endpoint slider based on this observation. This does not measure the microphone's acoustic sensitivity or physical gain control.

Hot-plug follow-up: Windows enumerated `Microphone (DGM20 USB Microphone)` while the running application's menus were stale. Fixed device-list notification propagation to refresh both UI menus without closing streams, changing the current selection, or enabling monitoring. Native re-enumeration can be checked without recording; physical unplug/replug of this fix remains to be verified by the owner.

- Separate real WASAPI shared capture/output with channel 1/2/average mapping, negotiated formats, explicit monitoring, device list fault mute and manual reconnect.
- Output-driven bounded clock bridge with linear resampling and drift servo; real peak meters, callback load, FIFO/error diagnostics.
- High-pass, gentle expander, broad tone EQ, compressor, smoothed gains, bounded echo, basic room reverberation, six editable presets, effect bypass and sample-peak output clamp.
- Worker-decoded local WAV, stereo playback/pause/seek, session queue with explicit next selection, UTF-8 LRC/TXT, offset, separate resizable/fullscreen lyrics window.
- Explicit local mix recording, optional aligned dry/wet WAVs, worker writes, bounded overflow/error handling and conservative RIFF size/time limit.
- VI/EN primary controls, advanced view, emergency mute, JSON import/export, atomic session replacement with last-good backup, local diagnostic export.
- Build, deterministic tests, fixture/render verification, portable source-inclusive packaging and Windows CI workflow. Inno Setup recipe is supplied but no installer compiler was found.

## Verified locally

Platform: Windows 11, Windows build reported by SDK selection as 10.0.26200; MSVC 19.38.33144.0 / VS 2022 17.8, Windows SDK 10.0.22621.0. JUCE 8.0.15 exact source revision `91ad83ae34a81e0833b1a2b0866f54846370ae53`.

- Release x64 configure/build succeeds; `scripts/build.ps1` runs CTest.
- Core executable passes bounded ring/FIFO ordering/overflow/underflow, 44.1/48 kHz impulse stability, NaN protection, muted gain, echo onset timing, sustained-signal compression, positive/negative 1,000 ppm clock drift, mixed-rate conversion in both directions with arbitrary blocks, and source loss tests.
- `scripts/verify.ps1` generates a reproducible three-second WAV fixture, renders the actual engine graph and reopens finalized mix/dry/wet files. Checks nonzero fixture output, output peak bounds and emergency mute. Latest evidence is in `build/evidence-*` (generated, not committed).
- Actual silent WASAPI open/callback probe on built-in Realtek microphone array + Realtek headphone endpoint: 48,000 Hz, actual input/output buffers 480 samples; input/output driver latency reported 480/480 samples. At the end of the three-second probe: FIFO 963 samples, ratio 0.999978, under/overruns 0/0, callback load snapshot 0.4%, input peak approximately 0.000007. Local report: `build/probe-local.txt`. These are one short run's software/driver observations, not a round-trip measurement or audibility test.
- Main/advanced native UI rendered and snapshots visually inspected at 1080×820 logical pixels, including Vietnamese characters; `build/ui-main.png` and `build/ui-main-advanced.png`. No claim of all-DPI or full interaction coverage.
- Portable ZIP build and SHA256 are produced by `scripts/package.ps1`, including exact JUCE source and CantoDeck source. Binaries are unsigned.
- Hosted Windows CI passed for code checkpoint `d43e5d65cd2516f3d0dff8df02664f3e11356eb8`: [run 34133873321](https://github.com/tang-vu/cantodeck/actions/runs/34133873321). A fresh `windows-2022` runner used MSVC 19.44.35228.0 and Windows SDK 10.0.26100.0; build, CTest, offline mix/dry/wet WAV verification, packaging and artifact upload all succeeded. The `CantoDeck-windows-x64-unsigned` artifact contains the portable package and synthetic test evidence. This establishes clean-machine compilation/offline execution, not physical audio acceptance.

## Not verified / specific limits

- No mic-to-speaker listening, USB mic/interface/BT test, unplug/reconnect physical test, acoustic feedback evaluation, RTT measurement or 30-minute hardware soak. See HARDWARE_TESTS.md. A human with the target setup must run these; the agent cannot hear output or manipulate cables.
- This bridge deliberately adds buffering; no sub-20 ms claim. Linear interpolation has limited high-frequency rejection. JUCE's own WASAPI dispatch contains a lock; strict end-to-end no-lock certification is not met.
- Stable endpoint IDs are not persisted; display names are saved and manually reselected. EQ is broad tone shaping, not full parametric EQ. De-esser absent. Two channels feed one mono vocal bus, not separate mic strips.
- WAV is predecoded into RAM, not streamed. Large files may need approximately 1 GiB plus replacement-buffer memory. No MP3/FLAC/video, automatic queue advancement, persisted queue/lyrics, or complete localization/accessibility test suite. Plain-text lyrics do not scroll.
- No latency compensation, RF64/segmentation or crash-recovery WAV repair. Record writes/overflow have explicit failure branches; actual disk-full/device-loss fault injection remains untested. Three-hour/3-GB stop is conservative, not segmentation.
- No process capture, key/tempo, virtual microphone driver, separate stream bus, two independent USB mics, phone remote or anti-feedback system. Browser music is outside the recording mix.
- No Inno Setup compiler or signing credentials were available. Installer recipe is uncompiled; portable ZIP is the runnable deliverable. Clean-machine runtime dependency compatibility remains untested.

## Next executable tasks

1. Use README.vi.md with wired headphones to check audible monitored voice, echo/room, stereo backing and recorded take; fill the hardware matrix with measured results and run the 30-minute soak.
2. Validate disconnect/reconnect, denied mic permissions, disk-full/queue overflow, high DPI, Unicode paths, and large-file loading. Measure real RTT before tuning prebuffer/servo.
3. Implement stable endpoint IDs, bandlimited rate conversion and backend callback audit; add full parametric EQ, streaming decoder and complete state/localization coverage.
4. Only after core gates pass: implement Windows process capture in recording-only mode first, with digital feedback/duplicate-route regression tests, then follow master specification order.

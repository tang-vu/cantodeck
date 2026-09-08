# Audio rework — development checkpoint, 2026-09-08

The goal remains the full project specification and comfortable live singing through the owner's external speakers. Neither native compilation nor a synthetic latency result is acceptance of that goal. The DGM20 is currently absent from Windows enumeration; the exact external-speaker connection is awaiting the owner's answer.

## Changes made

- Added an `AudioBackend` boundary and a direct Windows WASAPI implementation. Capture and render are serviced by one MMCSS `Pro Audio` thread. Application processing calls have no owned mutex, allocation or file I/O. Device service calls and OS scheduling are not claimed to be hard real-time.
- Negotiates `IAudioClient3` periods where supported; retries without RAW properties and then standard shared mode if necessary. Reports accepted RAW requests, actual periods and allocation capacities separately.
- Maintains one output period plus a 1 ms scheduling margin instead of filling the entire WASAPI allocation. The driver allocation itself is not presented as the queued-audio target.
- Replaced the slow, underdamped queue servo with direct proportional occupancy control. Low-latency capture target is one output quantum converted into input frames plus one capture quantum and 16 guard samples.
- Replaced linear interpolation with a precomputed 32-tap windowed-sinc phase table. It uses 16 input-frame lookahead (about 0.33 ms at 48 kHz) and a lower cutoff for downsampling. This is not a mastering-quality/universal rate-conversion claim.
- Added a transparent vocal path. Natural/Dry presets bypass coloration and effects; software gain still applies. Added a stereo-linked, zero-lookahead sample-peak limiter with 80 ms release in place of hard clipping. No true-peak or acoustic-feedback protection is claimed.
- Added explicit user-triggered speaker-to-microphone loop measurement. It plays a short quiet shaped pseudorandom signal, temporarily pauses local music/monitoring, keeps returned samples in RAM and analyses correlation off the audio thread. It rejects silence, clipping and ambiguous returns. Its result includes acoustic travel and is not inferred from buffer sizes. It has NOT been physically validated yet.
- Saves native endpoint IDs in addition to display names, with name fallback for old sessions, plus backend/rate/buffer/channel choices. Monitoring remains off on start/reconnect.

## Evidence so far

- Local Windows/MSVC Release builds and CTest pass.
- Pre-publication verification on 2026-09-08: Release rebuild and CTest passed (1/1); `scripts/verify.ps1` passed actual offline graph rendering and mix/dry/wet WAV finalization (`build/evidence-20260908-204943`). Portable packaging with matching application/dependency sources and SHA256 also passed. These checks do not validate audible monitoring or physical round-trip latency.
- Deterministic checks cover transparent-waveform preservation, stereo-linked limiting/release, previous DSP/FIFO tests, asymmetric 128/480 callback drift, 10 kHz amplitude preservation under fractional drift, a known delayed calibration signal and rejection of a silent return.
- Follow-up verification: measurement tests at 44.1/48 kHz also cover inverted/DC-offset returns, two equal separated returns, clipping, muted output, unprepared/duplicate start, cancellation before/during capture and restart. Cancelled capture transitions use compare/exchange so completion cannot overwrite a concurrent cancellation. Release/CTest and offline WAV verification passed again (`build/evidence-20260908-205219`). These are deterministic checks, not a stress-test proof of all thread interleavings.
- A native probe using deliberately nonexistent input/output names exited with code 12 and `HRESULT 0x80070490`, without silently choosing another endpoint. The contemporaneous Windows list contained Realtek and Steam endpoints, not DGM20. This tests missing selections, not physical unplug during streaming.
- A native silent Realtek microphone-array/headphones probe ran for 30 seconds: 48 kHz, 480-frame input/output periods, 1056-frame WASAPI capacities, 528-frame output queue target, 3000 capture packets and 3000 render callbacks. One capture discontinuity flag occurred; the initial packet may carry this flag, but its cause was not separately established. Application FIFO under/overruns and observed empty output paddings were zero. This run preceded the final sinc/calibration additions.
- A 30-minute silent native run of the newer sinc/calibration build was started at local time 20:40:06. Its separate executable is `build/native-soak-20260908-204006/CantoDeck.exe`; report target `result.txt` in that directory; process ID at launch 14164. Revalidate the live process/path or the final report before treating it as running/completed. Do not count it as an audible singing/recording soak or as DGM20 validation.

## Remaining acceptance work

Verify native device loss/reconnect, stop/start/pause, missing endpoints, RAW/fallback behavior, physical loop measurements, vocal listening quality and the real external-speaker route. Complete the remaining specification features and the hardware matrix. In particular, a successful Realtek silent stream does not establish sub-20 ms measured RTT or acceptable singing performance on DGM20/other speakers.

Native mode is currently selectable in Advanced, alongside the JUCE compatibility route. The loop measurement plays no sound until the user explicitly presses its button. Do not invoke that button from an automated hardware smoke test.

Primary API references consulted: [IAudioClient3 shared initialization](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient3-initializesharedaudiostream), [client stream properties](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient2-setclientproperties).

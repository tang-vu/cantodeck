# Architecture decisions

## Current amendments — 2026-09-08

The original ADRs below describe the initial implementation. The following amendments supersede their obsolete backend, clock bridge and device-identity details; [AUDIO_REWORK.md](AUDIO_REWORK.md) tracks verification and remaining limitations.

- An opt-in direct WASAPI adapter services capture/render on one MMCSS thread, alongside the JUCE shared-mode compatibility adapter. It distinguishes engine period, allocated capacity and queued-output target. Neither provides a hard real-time scheduling guarantee.
- The bridge now uses a 32-tap windowed-sinc phase table with 16-frame lookahead and proportional occupancy correction. Low-latency target is one output quantum converted to input frames plus one capture quantum plus 16 guard samples. Compatibility still targets three blocks.
- Capture overflow rejects new samples and counts them. If queued input exceeds the recovery threshold, the output-side consumer discards stale queued frames back to target; both resynchronizations and discarded frames are visible. The threshold margin is four negotiated quanta or 1024 frames, whichever is larger. Startup/starvation/recovery have 5 ms amplitude transitions. This loses audio during recovery and is not gapless recording or latency compensation.
- Windows endpoint IDs are persisted with display-name fallback for older sessions. Monitoring remains explicitly off after connection changes.
- A transparent vocal path retains gain and bypasses coloration/effects. Switching crossfades correlated dry/processed paths linearly over 5 ms without adding a steady-state delay. Both paths keep advancing to avoid frozen tails. Fixed filter coefficients are calculated during preparation; dB conversions are cached until their parameters change.
- Output protection is a stereo-linked, zero-lookahead sample-peak limiter, not the earlier clamp, not true-peak limiting, and not acoustic-feedback prevention.
- The explicit loop-measurement tool emits only on user request and rejects unreliable returns. Synthetic correlation tests do not establish physical round-trip latency.
- Backing WAV now uses a dedicated reader worker and eight 1024-frame stereo blocks, plus fixed current/scratch blocks. Four blocks are prefetched by the file loader before publication. Source indices identify queued data across seeks; the audio consumer discards stale blocks without resetting live indices and pops at most 16 blocks per output callback. Linear source interpolation preserves the previous music-conversion semantics. A source wait silences only music and pauses its position while mic/recorder continue; diagnostics count wait blocks, including seek waits. Reader errors stop music. Reader replacement/retirement occurs off the audio callback, and retired-reader joins happen after output resumes. Sample storage is bounded independently of track duration; codec metadata and OS caching are not covered by that bound. Physical slow-disk and long-file tests remain open.

## ADR 001 — Native Windows shared-mode audio

C++20, JUCE 8.0.15 pinned at `91ad83ae34a81e0833b1a2b0866f54846370ae53`, CMake, native JUCE UI. The repository initially contained only the master prompt. JUCE's pinned source confirms independent endpoint names and shared WASAPI availability. Two input/output-only devices are opened; no system defaults are changed. ASIO, exclusive mode, loopback and extra codecs are disabled. Core DSP/rings are standard C++; orchestration, WAV codec and UI use JUCE.

## ADR 002 — Independent capture clocks

Output callback is the clock. Input callback produces mapped mono samples to a 32,768-frame SPSC ring; output interpolates with a smoothed FIFO-occupancy servo. Nominal ratio is actual input rate / actual output rate, correction bounded ±2%. Startup and underflow rebuffer to three blocks, clamped 512–8,192 frames. Overflow drops newest samples and counts; underflow emits silence and counts once before reprime. Linear interpolation has limited high-frequency rejection. Positive/negative 1,000 ppm simulated drift is tested. USB/interface/BT physical behavior remains unverified. Multi-mic/multi-output timing is not claimed.

Low-latency follow-up: optional `WASAPIDeviceMode::sharedLowLatency` uses JUCE's IAudioClient3 path. Device buffers are negotiated to supported sizes nearest the request. Low-latency FIFO target is `ceil(outputBlock * inputRate/outputRate) + 2*inputBlock + 2`, clamped 128–8192. One capture-block margin was rejected after an asymmetric 128/480 drift regression failed; the two-block margin passes that regression. The default compatibility path is unchanged. Device mode switching closes/recreates endpoints and never resumes monitoring automatically.

## ADR 003 — Callback ownership

Application callbacks have no file I/O, UI calls, locks, allocation or ordinary logging. Track/DSP preparation and destruction occur while output is stopped. Parameters/meters are compile-time-checked lock-free atomics; only callback-owned DSP state is mutable in processing. Echo retiming uses a smoothed fractional delay (pitch bends during retiming); gain, tone and wet changes ramp. JUCE's own WASAPI callback dispatch uses `startStopLock` in its pinned source: the strict end-to-end no-lock contract is therefore not certified. This is a known backend constraint, not hidden by the application callback audit.

Device-list changes latch a fault and turn monitoring off. Rate changes during backend restart latch a fault. Reconnecting explicitly rebuilds DSP and clears the fault. A capture source loss also causes FIFO underflow/silence. The application never re-enables monitoring after an endpoint change. Device selections currently persist JUCE display names, not stable Windows endpoint IDs; duplicate/renamed devices require manual reselection.

## ADR 004 — Recording

A 262,144-frame SPSC queue contains stereo mix + mono dry/wet. Writer thread batches up to 1,024 frames, polls every 4 ms and finalizes 24-bit WAVs on stop. On overflow it latches an error and stops accepting frames; disk write error stops the worker. A three-hour limit remains below ordinary RIFF size at the supported live rates; no RF64/segmentation yet. All taps share frame counts; no acoustic or input-vs-music latency compensation. Start/stop briefly quiesce output to safely own the writer. Disk files use new names; application does not intentionally overwrite existing takes.

## ADR 005 — Licensing and external applications

AGPL-3.0-only application; JUCE's AGPL option. See pinned `vendor/JUCE/LICENSE.md`, https://juce.com/get-juce/ and the checked-in notices. No commercial SDK or paid API. Core operation offline. No telemetry or background recording. Only explicit Record writes mic data; probe captures transient levels but saves no samples.

Browser playback remains external. No virtual microphone is created by selecting a physical output. Future application capture needs runtime capability checks and explicit record-only versus fully routed modes; avoiding self-capture alone does not prevent duplicate playback. Primary Windows reference: https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/ (requires build 20348+). Endpoint loopback: https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording. Bluetooth behavior: https://learn.microsoft.com/en-us/windows-hardware/drivers/bluetooth/bluetooth-classic-audio. Future Linux planning must not imply direct PipeWire support from JUCE: https://docs.pipewire.org/devel/page_overview.html.

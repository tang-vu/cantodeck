# CantoDeck project specification

Repo: `cantodeck`

GitHub description: Open-source desktop audio console for karaoke, vocal effects, recording, and flexible mic-to-speaker routing.

Suggested first target: Windows 11 x64. Product languages: Vietnamese and English.

This preserves the original project requirements and delivery priorities. It describes intended scope, not shipped capabilities. See [STATUS.md](STATUS.md) for implementation evidence and [ROADMAP.md](ROADMAP.md) for current priorities.

---

You are the lead engineer, real-time audio engineer, DSP developer, product designer, and release engineer for CantoDeck. Build the application, not just a proposal. Work autonomously on reversible implementation choices, inspect the repository before changing it, preserve existing work, and obey its applicable instructions. Respond to the owner in Vietnamese; use English for code and technical documentation, with a Vietnamese quick-start guide.

## 1. Product and priorities

CantoDeck turns a Windows laptop or PC into an approachable audio console. The owner's primary goal is to sing karaoke through the computer using a microphone, backing track, and speakers. Additional modes are voice recording, podcasting, streaming, presentations, and everyday audio mixing.

The intended user is not an audio engineer. The first-run path must be: choose microphone, choose output, test input at a safe level, load a backing track, enable monitoring, sing. Make the simplest supported setup understandable within two minutes.

The product must actually capture, process, mix, play, and record audio. A polished UI with simulated meters does not satisfy the task. The defining experience is clear setup, useful vocal presets, visible routing, honest diagnostics, and reliable recovery from device changes.

Deliver a genuinely usable Windows alpha first, then expand it. Do not describe unfinished roadmap features as shipped. Never invent hardware tests or latency measurements. Do not stop after scaffolding when the environment permits more implementation.

## 2. Hard boundaries and hardware reality

- Work with audio endpoints exposed by the OS: built-in input/output, USB microphones, USB audio interfaces, compatible analog inputs, HDMI audio, and Bluetooth endpoints where available.
- A passive XLR microphone still needs an appropriate preamp/interface. Phantom power, input impedance, radio receivers, speaker amplification, and proprietary device controls cannot be created by software. Distinguish app-level gain from hardware gain.
- Prefer wired/USB setups for live vocal monitoring. Bluetooth transport latency and profile changes are outside our full control. Detect what is available, label what is unknown, and never advertise universal zero-latency karaoke.
- A Bluetooth speaker must already be paired through supported OS mechanisms. Do not promise direct control of all speaker firmware, EQ, battery state, or radio pairing.
- CantoDeck is initially a user-space audio app, not a replacement OS audio driver. Routing a mix into Discord/OBS/meeting apps may require a user-installed virtual audio device or an application-specific integration. Do not claim selecting a physical output creates a virtual microphone.
- No paid API, account, cloud inference, or internet connection is required for the core experience. Microphone recordings stay local. No background recording or telemetry by default.

## 3. Default technical architecture

Use C++20, CMake, JUCE 8, and native JUCE UI unless repository constraints provide a compelling reason otherwise. Select a stable JUCE revision after checking current primary documentation and pin it. Use the open-source AGPLv3 route for the application and include the appropriate license and notices. Verify each extra dependency's actual license before bundling it; do not label a JUCE-based application MIT by assumption.

Keep dependencies minimal. Use JUCE's available audio/DSP primitives, supplemented by tested custom DSP and a small native Windows adapter where needed. Do not start with an Electron renderer or browser Web Audio as the real-time audio engine. Do not introduce a second GUI framework without a concrete requirement.

Windows is the acceptance platform. Start with WASAPI shared mode for compatibility. Expose other backend modes only when genuinely supported and tested. Exclusive mode can prevent other applications from playing backing audio, so explain that tradeoff in the device UI. ASIO is optional, capability-gated, and subject to verification of SDK/driver distribution requirements; never make it the only working path.

Separate a headless DSP/routing core, audio backend interfaces, session/preset state, recorder workers, platform integrations, and UI. Keep future CoreAudio and Linux backend work possible without claiming parity in the first release. Do not assume JUCE automatically exposes PipeWire directly.

Suggested structure, adjusted to the repository when sensible:

    app/
    engine/audio/
    engine/dsp/
    engine/routing/
    engine/recording/
    platform/windows/
    ui/
    resources/presets/
    tests/
    docs/
    scripts/

Write short architecture decisions for backend choice, licensing, cross-device clock handling, and external-app capture.

## 4. Real-time audio contract

The output device is the processing clock. Capture sources with independent clocks enter through bounded ring buffers with explicitly designed resampling and drift correction where required. First prove the common full-duplex/interface path and the common USB mic plus separate speaker path. Do not silently assume independently clocked devices run at exactly the same effective rate.

- No allocation/deallocation, locks, file or network I/O, unbounded work, UI calls, or ordinary logging in audio callbacks.
- Preallocate buffers and DSP state. Transfer parameter changes through bounded, real-time-safe mechanisms. Smooth audible parameter changes.
- Construct and retire graphs off the audio thread; avoid object destruction on that thread during swaps.
- Support 44.1 and 48 kHz where devices allow, with 48 kHz preferred. Negotiate actual formats and show actual values. Handle arbitrary callback sizes.
- Expose underruns/overruns, callback load, buffer sizes, resampler state, and connection health. Bound all queues and document overflow behavior.
- Handle NaN/Inf, denormals, silence, clipping, and malformed input without runaway output.
- On device loss, enter a safe muted state, notify the user, and require deliberate re-enabling before audible microphone monitoring resumes on a changed output.
- Treat reported device latency, algorithmic delay, buffered delay, and measured round-trip latency as separate quantities. Never present buffer duration alone as end-to-end latency.

Target a measured round-trip latency below 20 ms on a suitable wired reference setup, preferably lower. This is a hardware-dependent target, not a universal promise or a prerequisite for publishing honest software test results. Record backend, devices, sample rate, buffers, measurement method, and date for any published measurement.

## 5. Release 0.1: required working vertical slice

### Device setup

Implement live device enumeration, independent input/output selection where supported, channel mapping, actual sample-rate/buffer reporting, mic level test, and explicit monitoring activation. Save device identifiers with a graceful missing-device flow. Offer a low-level output test only after the user starts it; never auto-play tones.

Start with one mono microphone and stereo output. Support two microphone channels from a single multichannel interface when available. Multiple independent USB microphones are later work unless the clocking/resampling architecture is already tested for them.

Make “USB microphone + laptop headphone output or powered speakers” and “mic + USB audio interface + speakers/headphones” explicit setup recipes. Explain direct monitoring versus processed monitoring and how enabling both can produce double voice.

### Mixer and vocal DSP

Use clearly named microphone, music, and master channels. Provide gain/fader, mute, clipping indication, and responsive real signal meters. Keep dry input and processed vocal taps separate.

Implement a documented chain along these lines:

Input mapping -> trim -> DC removal/high-pass -> gentle expander -> parametric EQ -> compressor -> optional de-esser -> vocal fader -> dry path plus echo/reverb sends -> mix -> output protection.

Provide usable echo and reverb with bounded feedback/decay and wet/dry controls. Keep singing presets conservative: heavy speech denoising and aggressive gates can damage sustained notes. Every implemented effect needs bypass and understandable controls. Add an actual de-esser only when its DSP and tests exist, otherwise defer it visibly.

Include Natural, Warm Karaoke, Bright Karaoke, Small Room, Speech, and Dry Recording presets with editable values and JSON import/export. Preset names describe intended sound; do not invent celebrity voices or claim automatic voice improvement.

### Backing music and lyrics

Ship a local backing-track player with play/pause, seek, position, gain, queue, and end-of-track behavior. WAV is mandatory. Add MP3/FLAC only through codecs that are really enabled and distributable. Decode and read files outside the audio callback.

Support UTF-8 LRC lyrics, Vietnamese text, configurable lyric offset, and a readable resizable/fullscreen lyrics view. Handle plain-text lyrics gracefully. Basic audio plus LRC is the first-release scope; local video and a separate TV lyrics window can follow.

Allow the user to sing while a browser plays YouTube externally. In this simple mode CantoDeck monitors the mic; browser music is mixed by the OS. Clearly state that CantoDeck's local music fader, key shift, and recording mix do not automatically control/include that browser audio.

### Recording and state

Record the internal stereo mix to WAV, with optional separate dry and wet vocal files. Clearly identify which sources the recording includes. Keep file writes off the audio thread with bounded queues and explicit disk-full/write-error handling. Finalize WAV headers correctly; plan file segmentation or RF64 before exceeding ordinary WAV size limits.

Keep track timing aligned, document latency compensation where implemented, and never overwrite recordings silently. Persist validated, versioned presets/session settings atomically; retain a last-known-good configuration.

### Output protection

Provide master limiting, headroom, gain ramps, and a large always-accessible MUTE ALL control that bypasses ordinary queued UI work. Label the limiter accurately as sample-peak or true-peak according to implementation. Digital limiting does not guarantee safe acoustic sound pressure or prevent all feedback.

Monitoring starts off. Do not automatically unmute because a preset loads or a device reconnects. Handle acoustic feedback with a practical setup guide first. Any later automatic anti-feedback system must distinguish sustained singing from feedback and state its limitations.

## 6. Interface quality

Build a desktop audio console with a focused Karaoke view and an Advanced view. Use a coherent dark neutral theme, clear typography, restrained color accents, large faders, accessible contrast, and meaningful units. Avoid decorative controls that do nothing.

The main view shows selected mic/output, signal status, mic/music/master faders, vocal preset, echo/reverb, playback, lyrics, recording state, and emergency mute. Advanced view exposes routing, EQ/compressor parameters, channel mapping, and diagnostics.

Use Vietnamese and English localization, robust Unicode, keyboard focus, tooltips, scalable DPI, and useful empty/error states. A user should understand why there is no sound, which source is being recorded, and how to recover without opening a log file.

Test the UI with no devices, unavailable devices, active playback, muted monitoring, clipping, long Vietnamese filenames, and high DPI. Demo mode may use generated fixtures but must be explicitly labelled and isolated from actual device discovery and metrics.

## 7. Release 0.2: connected audio workstation

Implement these after the 0.1 path is stable; keep a truthful capability matrix and prioritize functional slices over a sprawling settings UI.

1. Windows application-audio capture. Use documented native APIs with runtime OS capability checks. JUCE loopback support must be verified rather than assumed. Allow selection of a process tree where supported. Microsoft reference: https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/
2. Prevent both recursive digital feedback and duplicate playback. Process exclusion alone does not prevent a browser's direct output plus the captured/replayed copy from being heard twice. Define two explicit modes: capture for recording only while original app playback remains audible, or full routing through a separate existing endpoint/virtual device with only the routed copy audible. Reject unsafe configurations. Do not mute a source process blindly and assume capture remains unaffected.
3. Key and tempo adjustment for local backing tracks using a verified license-compatible DSP dependency. Keep this processing off the live vocal path where possible and report its delay. Do not claim it affects uncaptured YouTube audio.
4. Separate monitor and stream/record buses, with clear routing matrix and per-route gains. Route into an already-installed virtual endpoint where available; document external dependencies. Do not download/install drivers or change system defaults silently.
5. Two independent microphones only with validated rate adaptation and buffer behavior. Multiple physical output devices need their own synchronization strategy; do not promise phase-aligned playback across arbitrary Bluetooth/USB speakers.
6. Opt-in LAN phone remote for gain, mute, preset, transport, and queue. This controls the native engine; it does not carry live vocal audio. Use explicit pairing, expiring credentials, origin/host validation, bounded/rate-limited messages, and an appropriate protected transport. Show who is connected, allow revocation, and disable external access by default. No automatic port forwarding. Phone-as-microphone is a separate later feature with honest added latency.
7. Speech/podcast and presentation modes built on the same working mixer. Denoising, mix-minus, hotkeys, and soundboard can be added when their routing and performance are verified.

## 8. Later research, not first-release promises

Optional offline stem separation, pitch analysis/training, plugin hosting, MIDI control, network audio, virtual-driver development, automatic feedback suppression, macOS/Linux parity, and advanced video playback belong on the roadmap. Do not ship fake AI controls or create a cloud dependency for these.

Feedback suppression is not equivalent to acoustic echo cancellation, noise reduction, or a limiter. If implemented, use bounded narrow notches and conservative detection, evaluate false positives on held notes, and never market it as guaranteed howl prevention.

## 9. Validation and release evidence

Add meaningful deterministic tests for DSP stability, EQ response, compressor/limiter behavior, delay timing and feedback bounds, parameter smoothing, mixing/routing, bypass, stereo/channel mapping, and recording integrity. Generate test tones, impulses, sweeps, and fixtures reproducibly.

Exercise independent-clock simulation with positive and negative drift, resampling, ring-buffer limits, and source loss. Include regression tests against digital self-capture and duplicate routing where those features exist.

Provide an offline render command that runs fixture audio through the actual DSP graph and writes results. Include a diagnostics command or export with dependency/build versions, OS/backend, device configuration, graph delays, and error counts; redact personal paths/device identifiers as appropriate before sharing.

Build/test on Windows CI without assuming audio hardware. If local execution is on Linux, validate portable components there and distinguish that from native Windows compilation and physical audio testing. Never say karaoke was heard or tested merely because CI passed.

Create a manual hardware matrix covering built-in audio, USB mic with separate output, an audio interface, Bluetooth, different sample rates, and disconnect/reconnect. Mark untested cells untested. A 30-minute hardware soak test is a release gate for claims about sustained playback; automated headless endurance tests provide different evidence.

Package a Windows portable artifact and a repeatable installer build when tooling supports it. Avoid admin requirements unless an identified component needs them. Do not claim binaries are signed without a signing process. Provide source/build instructions, checksums for produced artifacts, dependency notices, and exact validated commands.

## 10. Execution plan and handoff

1. Inspect the repo and environment. Identify relevant instructions, existing implementation, compilers, test/build tools, and constraints. Write a concise plan and assumptions; start work without asking the owner to choose routine frameworks.
2. Prove the headless DSP pipeline and real mic-to-output path, then independent USB mic/output behavior. If hardware is absent, implement backend handling and testable adapters while stating the limitation.
3. Add the core mixer, safe monitoring, effects, local music, lyrics, recording, and session persistence. Keep the app buildable through each milestone.
4. Integrate the finished UI with real state and meter data. Validate visible controls and failure states.
5. Complete Windows build/package automation, tests, Vietnamese setup guide, troubleshooting, and capability matrix.
6. Continue with 0.2 priorities when core gates pass and the environment permits. Do not substitute a roadmap for authorized implementable work. When a tool or hardware limitation blocks a specific item, complete independent work and document exact remaining steps.

Maintain docs/STATUS.md with implemented/verified/untested/blocked distinctions and the next executable tasks so another Codex session can resume. Use sensible commits if permitted by the repository workflow. Do not force-push, change repository visibility, publish releases, or alter system-wide audio settings without authorization. Local implementation, builds, and tests are expected.

Deliver the working source, build artifacts actually produced, test evidence, architecture notes, README, README.vi.md, LICENSE, dependency notices, contributing instructions, release notes, and the exact launch command. Tell the owner what now works, what hardware was actually tested, what remains, and the minimum physical steps needed to sing.

Begin now by inspecting the repository and implementing the first real audio vertical slice. Do not return only this plan.

---

## Primary technical references

Verify current documentation and pinned versions at implementation time.

- JUCE licensing: https://juce.com/get-juce/
- JUCE source/license: https://github.com/juce-framework/JUCE/blob/master/LICENSE.md
- Windows process loopback: https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/
- WASAPI endpoint loopback: https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording
- Windows Bluetooth Classic audio profiles: https://learn.microsoft.com/en-us/windows-hardware/drivers/bluetooth/bluetooth-classic-audio
- PipeWire architecture, for later Linux planning: https://docs.pipewire.org/devel/page_overview.html

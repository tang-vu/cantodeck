# Roadmap

This is a development preview. The owner's external-speaker setup has unacceptable monitoring delay and vocal quality. That is the primary open product issue; adding controls or passing offline tests does not close it.

## Before calling release 0.1 usable

1. Establish a repeatable dry-vocal baseline with wired headphones and external speakers separately. Record the exact route, physical monitoring paths and user observations.
2. Measure microphone-to-output round-trip latency on a wired setup, then tune the backend, clock bridge and buffering against measured results and underruns.
3. Evaluate vocal transparency and the effects chain. Verify expander/compressor thresholds, gain staging, room/echo quality and bypass behavior using reproducible fixtures plus listening tests.
4. Complete the hardware matrix and a 30-minute soak, including USB microphone, separate output, disconnect/reconnect and recording failure cases.
5. Validate stable device identifiers, three-band parametric EQ and streaming WAV playback on hardware. Finish session completeness, localization/accessibility and release packaging validation.

## After the core audio gates pass

Follow [the specification](SPECIFICATION.md) in order: Windows application capture with explicit record-only/routed modes; local key/tempo processing with a verified dependency license; monitor/record/stream buses; independently clocked microphones; opt-in authenticated LAN remote; speech/podcast features.

No dates, zero-latency claims, guaranteed feedback suppression or universal device compatibility are promised. Detailed implemented/verified/untested distinctions live in [STATUS.md](STATUS.md).

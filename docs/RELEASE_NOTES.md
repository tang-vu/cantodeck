# 0.1.0 early alpha

Initial native Windows implementation: actual WASAPI input/output callbacks, output-clock adaptive bridge, vocal effects, conservative gain ramps, real meters, emergency mute, WAV player, queue selection, LRC/TXT lyrics, separate fullscreen lyrics, mix/dry/wet WAV recording, versioned JSON settings and diagnostics. Six starting presets and VI/EN primary controls.

This is a runnable vertical slice, not completion of every release-0.1 requirement. Full parametric EQ, de-esser, complete localization, stable endpoint IDs, streaming file decode, full real-time backend certification, optimized low latency, robust crash recovery and physical hardware validation remain. Release 0.2 is gated on those core checks; process capture, key/tempo, virtual buses, phone remote and multiple independent microphones are absent. Binaries are unsigned.

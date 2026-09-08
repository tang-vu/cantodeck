# 0.1.0 development preview — 2026-09-08

Initial native Windows implementation: actual WASAPI input/output callbacks, output-clock adaptive bridge, vocal effects, conservative gain ramps, real meters, emergency mute, WAV player, queue selection, LRC/TXT lyrics, separate fullscreen lyrics, mix/dry/wet WAV recording, versioned JSON settings and diagnostics. Six starting presets and VI/EN primary controls.

Follow-up work adds an optional direct WASAPI backend, transparent vocal path, sinc clock bridge with bounded backlog recovery, linked sample-peak limiter, three-band parametric EQ, diffused mono room return, streaming WAV playback, persistent queue/lyric offset, scrollable lyrics and more fault/lifecycle tests. Internal compatibility-backend control pauses now preserve monitor intent without restoring a stale enabled flag. See [AUDIO_REWORK.md](AUDIO_REWORK.md) for exact evidence and limitations by checkpoint.

Build identity is available in diagnostics, the title tooltip and `--build-info output.json` (no device enumeration). Portable packages include `BUILD_INFO.json` with the executable SHA256. `-dirty` identifies uncommitted source at configuration time; `source-archive` means no Git identity was available. The configuration timestamp is not a latency measurement or signing timestamp.

This is not completion of release 0.1 or approval for reliable live karaoke. The owner's latency/vocal-quality complaint remains unresolved by listening evidence. An earlier silent native 30-minute run had three FIFO underruns; a later clean 60-second probe does not erase that result. Actual RTT, external-speaker singing, physical unplug/reconnect, long playback/recording and storage-fault validation remain open. Complete localization/accessibility, lyric-file session association and robust crash recovery are also incomplete. De-esser is deferred.

Release 0.2 remains gated: process capture, key/tempo, virtual buses, phone remote and multiple independent microphones are absent. Binaries are unsigned; mathematical tests, packaging and CI results are not audibility or hardware compatibility certification.

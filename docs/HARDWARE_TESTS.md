# Manual hardware validation matrix

No acoustic round-trip or singing test has been performed by the agent. All cells below remain untested until a human records the exact setup and result.

| Setup | 44.1/48 kHz | Singing/echo/room | 30-min soak + recording | Unplug/reconnect | Measured RTT |
|---|---|---|---|---|---|
| Built-in mic + wired headphones | Untested | Untested | Untested | Untested | Unmeasured |
| USB mic + laptop output | Untested | Untested | Untested | Untested | Unmeasured |
| USB interface input + output | Untested | Untested | Untested | Untested | Unmeasured |
| Paired Bluetooth output | Untested | Untested | Untested | Untested | Unmeasured |

For each row: date, OS build, endpoint manufacturer/model, driver version, backend, requested and actual rate/block, FIFO occupancy/rate correction, under/overruns, recording frame count, audibility and faults. Test safe startup, mute, sustained notes, gain changes, playback/seek, record mix/stems, disk errors and unplug during recording. Confirm re-enable is deliberate. Measure RTT with a documented electrical/acoustic loopback procedure, subtract no delays without recording the method. Keep driver-reported latency distinct.

UI checks: no devices, denied mic permission, unavailable saved device, channel 2 absent, paused/end-of-track, clipping, Vietnamese filenames/LRC, long plain text, 100/150/200% DPI, keyboard focus and fullscreen lyrics. Lyrics now use read-only scrolling editors; complete manual interaction/high-DPI coverage remains open.
# Optional native lifecycle diagnostic

The separate compatibility-control check is explicit and excluded from CI:

```powershell
Start-Process ./build/CantoDeck_artefacts/Release/CantoDeck.exe -WindowStyle Hidden -Wait -ArgumentList '--test-compat-control "build/evidence-REPLACE/input.wav" "build/compat-control.txt"'
```

Use an existing generated synthetic WAV fixture and a new report name. This opens the first enumerated input/output, sets MUTE ALL before connecting and never clears it, checks monitor-intent preservation around internal callback pauses, and closes again. It starts neither recording nor test tones. Check endpoint names in its report before assigning hardware coverage. It does not measure RTT or test actual unplug/replug.

After building, explicitly run the following to open the selected endpoints with silent output, test three open/pause/resume/close cycles, and print observations. Captured samples are not inspected or saved. Use endpoint names present on your machine. This is not a listening, physical unplug or latency test. It does not change Windows audio defaults.

```powershell
./build/Release/native_backend_tests.exe --hardware-silent 'Microphone Array (Realtek(R) Audio)' 'Headphones (Realtek(R) Audio)'
```

Without `--hardware-silent`, this executable only tests invalid arguments and closed-state operations without opening device streams; that mode is included in CTest/CI. Omitting names in the explicit silent mode chooses the first enumerated endpoints, which may be virtual devices; always check printed names before attributing hardware coverage.

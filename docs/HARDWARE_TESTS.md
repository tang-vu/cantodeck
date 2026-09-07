# Manual hardware validation matrix

No acoustic round-trip or singing test has been performed by the agent. All cells below remain untested until a human records the exact setup and result.

| Setup | 44.1/48 kHz | Singing/echo/room | 30-min soak + recording | Unplug/reconnect | Measured RTT |
|---|---|---|---|---|---|
| Built-in mic + wired headphones | Untested | Untested | Untested | Untested | Unmeasured |
| USB mic + laptop output | Untested | Untested | Untested | Untested | Unmeasured |
| USB interface input + output | Untested | Untested | Untested | Untested | Unmeasured |
| Paired Bluetooth output | Untested | Untested | Untested | Untested | Unmeasured |

For each row: date, OS build, endpoint manufacturer/model, driver version, backend, requested and actual rate/block, FIFO occupancy/rate correction, under/overruns, recording frame count, audibility and faults. Test safe startup, mute, sustained notes, gain changes, playback/seek, record mix/stems, disk errors and unplug during recording. Confirm re-enable is deliberate. Measure RTT with a documented electrical/acoustic loopback procedure, subtract no delays without recording the method. Keep driver-reported latency distinct.

UI checks: no devices, denied mic permission, unavailable saved device, channel 2 absent, paused/end-of-track, clipping, Vietnamese filenames/LRC, long plain text, 100/150/200% DPI, keyboard focus and fullscreen lyrics. Long lyrics may overflow the label; scrolling plain-text lyrics remains incomplete.

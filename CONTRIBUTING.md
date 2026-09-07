# Contributing

Contributions are welcome. Start with [known gaps](docs/ROADMAP.md), [current evidence](docs/STATUS.md) and the [specification](docs/SPECIFICATION.md). The immediate priority is usable monitoring latency and natural vocal sound, not more effects or integrations.

## Development

Use Windows 11 x64, Visual Studio 2022 Desktop development with C++, Windows SDK, CMake and Git. The initial build fetches the pinned JUCE revision.

```powershell
./scripts/build.ps1
./scripts/verify.ps1
```

Use C++20 and keep UI/control/file operations off application audio callbacks. Preserve monitoring-off on every device change. Keep changes focused and explain the problem, resulting behavior, validation and remaining limitations in your pull request. Add regression coverage for DSP/routing behavior when appropriate. Report hardware results separately from deterministic tests using [the hardware matrix](docs/HARDWARE_TESTS.md).

Update `docs/STATUS.md` with actual evidence, not anticipated results. Do not add cloud services, codecs, DSP libraries or SDKs without reviewing their licenses. Do not commit private recordings, endpoint identifiers, access tokens, generated builds or dependency checkouts. Generated synthetic fixtures are fine when clearly labeled.

## Issues and pull requests

Use the bug or feature templates. Include reproducible steps, app revision, Windows version, audio device models, actual sample rates/buffers and whether the issue occurs with wired headphones or speakers. Remove personal paths/identifiers from diagnostics. A recording is optional and should be yours to share; do not upload private speech or copyrighted music.

Please follow [the code of conduct](CODE_OF_CONDUCT.md). Security issues follow [SECURITY.md](SECURITY.md), not a public bug report. Contributions are licensed under AGPL-3.0-only; by submitting a contribution you confirm you have the right to license it under these terms. No CLA is currently required.

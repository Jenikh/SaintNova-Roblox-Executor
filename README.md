# SaintNova Executor

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Release](https://img.shields.io/github/v/release/CorkyPotato/SaintNova-Roblox-Executor)](https://github.com/CorkyPotato/SaintNova-Roblox-Executor/releases)

A **Roblox Studio** executor with a replaceable backend, a local harness, a
named-pipe protocol, and a tkinter UI.

SaintNova runs Lua in a Roblox Studio play session through its own backend. It
compiles scripts with the engine by rewriting a prepared ModuleScript's
`Source`, executes them in the live client, and streams output, errors, and
yields back to the CLI or UI. The core is backend-agnostic; a Player backend is
not included.

## Download

Prefer building from source using the instructions below. GitHub Actions builds
the checked-out source with MSVC, runs the test suite, and creates a SHA-256
manifest plus a GitHub build-provenance attestation for tagged builds. A hash
proves artifact integrity, not that the program is safe; review the source and
workflow before running any build.

## Status

| Piece | Status |
| --- | --- |
| C++ core with Luau 0.747 and a Win32 named-pipe server | Working |
| Local harness backend (instances, signals, filesystem, clipboard, crypt, debug) | Working |
| Named-pipe protocol with log streaming, cancel, timeout, reset | Working |
| tkinter UI (run, cancel, reset, backends, log pane) | Working |
| CLI (`serve`, `run`, `compile`, `repl`, `backends`, `version`) | Working |
| Embeddable DLL with a C surface | Working |
| Roblox Studio backend | Working in a play session: engine-compiled ModuleScript transport, output streaming, errors, yields |
| Roblox Player backend | Not shipped; see [`docs/BACKENDS.md`](docs/BACKENDS.md) |

Unsupported functions are not silent no-ops. Each one raises with a reason; see
[`docs/FUNCTION-SURFACE.md`](docs/FUNCTION-SURFACE.md).

## Player backend

The public release is Studio-only. The Player transport explored in the original
workspace did not reach a stable release and is not part of this repository. The
backend seam is: a Player backend implements `IExecutionBackend` and registers
itself, while the protocol, UI, CLI, and job lifecycle stay unchanged.

See [`docs/BACKENDS.md`](docs/BACKENDS.md) for the contract, the registration
point, the reference live-client implementation, and the current status. The
public status and build plan live in
[`Roblox-Player-Backend`](https://github.com/CorkyPotato/Roblox-Player-Backend).

## Build

Requirements:

- CMake 3.20 or newer
- Ninja
- A C++20 compiler. The bundled script uses `gcc`/`g++` from `PATH`; MinGW-w64
  (GCC 13 or newer) is the tested toolchain.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
```

Outputs:

- `build\bin\saintnova.exe`
- `build\bin\libsaintnova.dll` (MinGW; `saintnova.dll` with MSVC)

With Visual Studio, configure CMake manually and use the generated configuration
path:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Studio quickstart

1. Open `place\saintnova_client.rbxlx` in Roblox Studio and press **Play**.
   Wait for `SAINTNOVA-READY` in the Output window.
2. Start the server:

   ```powershell
   build\bin\saintnova.exe serve
   ```

3. Start the UI:

   ```powershell
   python ui\saintnova_ui.py
   ```

   Choose the `roblox` backend and run a script.

From the CLI:

```powershell
build\bin\saintnova.exe run examples\basics.luau --backend roblox
```

The Studio place contains `StarterPlayerScripts.SaintNovaLoader` and
`ReplicatedStorage.SaintNovaTemplate`. The backend rewrites the template's
`Source` in the Studio process; the engine compiles it inside the play session.

## The place

`place\saintnova_client.rbxlx` is a ready-to-open Studio place with the loader
and template installed. The loader source is also in
`harness\client_loader.luau`.

## Documentation

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — process structure, backends, threading.
- [`docs/BACKENDS.md`](docs/BACKENDS.md) — backend contract, registration, Player status.
- [`docs/PROTOCOL.md`](docs/PROTOCOL.md) — named-pipe framing and messages.
- [`docs/FUNCTION-SURFACE.md`](docs/FUNCTION-SURFACE.md) — implemented and refusing functions.
- [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) — third-party licences.

## Contributors

See [`CONTRIBUTORS.md`](CONTRIBUTORS.md).

## Provenance

See [`NOTICE.md`](NOTICE.md) for provenance and source attribution.

## License

MIT licensed; see [`LICENSE`](LICENSE). Third-party components keep their own
licenses; see `THIRD_PARTY_NOTICES.md`.

Not affiliated with Roblox Corporation. Roblox is a trademark of Roblox
Corporation.

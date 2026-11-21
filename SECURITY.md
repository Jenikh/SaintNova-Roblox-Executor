# Security model and release verification

## Build verification

The Windows workflow builds the checked-out source revision with MSVC, runs the
repository test suite, and, for version tags, creates a SHA-256 manifest and a
GitHub build-provenance attestation for the release archive.

Verify a downloaded archive with:

```powershell
gh attestation verify saintnova-windows-x64.zip -R Jenikh/SaintNova-Roblox-Executor
```

After extracting it, compare each file against `SHA256SUMS.txt` using
`Get-FileHash -Algorithm SHA256`. A matching hash proves that bytes match the
manifest, not that the program is safe. Provenance identifies the workflow and
source revision that produced an artifact; it is not a security audit.

## Important execution warnings

SaintNova's Roblox Studio backend is not a sandbox. It writes a prepared
ModuleScript source buffer in the Roblox Studio process and asks the Studio
engine to compile and execute it. A submitted script runs with the capabilities
of that Studio LocalScript environment. Do not run scripts from untrusted
sources, and do not treat the backend as an isolation boundary.

The Studio backend requires process-memory query/read/write/operation access to
`RobloxStudioBeta.exe`. This is inherent to its current transport. Roblox
Studio updates can invalidate internal memory layouts and signatures; failures
can crash or destabilize Studio. Use a disposable test place and save work
before attaching.

Cancellation and timeout stop SaintNova waiting for a result; they cannot safely
pre-empt arbitrary Luau code already executing synchronously inside Studio. A
cancelled or timed-out script may continue running or leave the play session
unresponsive. Stop the play session manually if this occurs.

The local named pipe is a control interface. It is restricted to the current
Windows account and LocalSystem and rejects remote clients. Any process running
as the same Windows account can still attempt to connect. Do not run untrusted
software under the same account while SaintNova is serving.

The standalone local harness has capability switches and a workspace root.
These reduce accidental access but are not a Windows security boundary. Do not
use the harness to execute hostile scripts in a normal desktop account.

## Recommended operation

- Build from a reviewed source commit; do not trust an executable merely
  because it is hosted on a release page or has a matching hash.
- Verify the build-provenance attestation and SHA-256 manifest for tagged builds.
- Prefer a disposable VM or separate low-privilege Windows account.
- Use a separate Roblox test place and never paste secrets into scripts.
- Treat all Luau passed to the Studio backend as arbitrary code with the
  permissions of the Studio session.

# Security Policy

## Scope

This project is a reverse engineering toolkit and D3D9 proxy DLL for running Tomb Raider: Legend under NVIDIA RTX Remix. The software performs runtime memory patching of a third-party game binary.

Security reports are relevant for:
- Vulnerabilities in the proxy DLL itself (e.g., unsafe memory operations, buffer overflows)
- Security issues in the Python tooling (`retools/`, `livetools/`, `graphics/`, `autopatch/`, `automation/`)
- Dependency vulnerabilities in `requirements.txt`

Out of scope:
- Behavior of the underlying game (`trl.exe`) or third-party libraries (`d3d9_remix.dll`, `dxwrapper.dll`)
- Issues that require physical access to the machine or existing attacker code execution

## Supported Versions

Only the latest commit on `main` is actively maintained. No backport fixes are issued for older builds.

## Reporting a Vulnerability

Please **do not open a public GitHub issue** for security vulnerabilities.

Report security issues privately via [GitHub Security Advisories](https://github.com/skurtyyskirts/TombRaiderLegendRTX-/security/advisories/new). This allows the issue to be assessed and fixed before public disclosure.

Include:
- A description of the vulnerability and its potential impact
- Steps to reproduce (tool commands, input files, or code path)
- Any proposed fix or mitigation

Expect an initial response within 7 days. After a fix is published, coordinated public disclosure is welcome.

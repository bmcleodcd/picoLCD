# Historical macOS kext material

These codeless kexts and the original notes target the legacy SDK on Mac OS X,
including Leopard/Snow Leopard. They were based on material originally hosted
at `http://properkernel.com/picolcd/`.

The historical procedure copied the kext into `/System/Library/Extensions`,
updated that directory's timestamp and rebooted; Snow Leopard also required
root:wheel ownership. This is an archival description, not a verified procedure
for current macOS.

The current panel service uses Linux uinput, systemd/D-Bus and Linux/GNU APIs.
These kexts do not make that service portable to macOS. See the root
[README](../README.md) and [repository reference](../AGENTS.md) for the
maintained Linux path.

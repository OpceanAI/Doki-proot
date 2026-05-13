<div align="center">

<img src="https://img.shields.io/badge/Doki--proot-0.9.0-6366F1?style=for-the-badge&labelColor=0A0A0A" alt="Doki-proot v0.9.0">
<img src="https://img.shields.io/badge/C-89%25-555555?style=for-the-badge&labelColor=0A0A0A&logo=c&logoColor=white">
<img src="https://img.shields.io/badge/License-GPL--2.0-blue?style=for-the-badge&labelColor=0A0A0A">
<img src="https://img.shields.io/badge/Size-14K-green?style=for-the-badge&labelColor=0A0A0A">

<br><br>

# Doki-proot

## PRoot fork for the Doki Container Engine

A modified fork of [proot](https://github.com/termux/proot) (28,350 lines of C) adapted as the core engine for [Doki](https://github.com/OpceanAI/Doki).

**Not a user-facing tool.** Doki-proot runs as a background daemon controlled exclusively via JSON IPC over Unix domain sockets. No interactive CLI. No human-readable output.

<br>

---

**PRoot** (PTRACE chroot) is a userspace chroot implementation that intercepts syscalls via ptrace, translating paths and emulating root without kernel privileges. It enables container-like environments on systems without namespace support (Android kernels, shared hosting, locked-down devices).

**Doki-proot** extends PRoot with daemon mode, bidirectional JSON IPC, container data hiding, and full port forwarding — all controlled programmatically by the Doki container engine.

---

</div>


## JSON IPC Protocol

Doki and doki-proot communicate via newline-delimited JSON over Unix domain sockets.

### Doki → doki-proot

```json
{"type":"exec","id":"cmd-001","cmd":["gcc","-o","app","main.c"],"env":[],"cwd":"/app"}
{"type":"config","hidden_files":["/proc/self/cmdline"],"port_map":[{"guest_bind":80,"host_bind":8080,"proto":"tcp"}]}
{"type":"signal","sig":"SIGTERM"}
{"type":"health"}
{"type":"shutdown"}
```

### doki-proot → Doki

```json
{"type":"stdout","id":"cmd-001","data":"compiling..."}
{"type":"stderr","id":"cmd-001","data":"error: ..."}
{"type":"exit","id":"cmd-001","code":0}
{"type":"health","status":"ok","pid":12345}
{"type":"config_ack","status":"ok"}
```

## Extensions

### Preserved from upstream PRoot

| Extension | Lines | Function |
|-----------|-------|----------|
| `fake_id0` | 1,331 | Root emulation without privileges |
| `link2symlink` | 816 | Hardlinks → symlinks (Android compat) |
| `kompat` | 1,049 | Kernel compatibility layer |
| `sysvipc` | 1,100 | System V IPC support |
| `ashmem_memfd` | ~200 | Android shared memory |
| `fix_symlink_size` | ~100 | Fix symlink size reporting |
| `mountinfo` | ~150 | Mount info passthrough |

### New doki-specific extensions

| Extension | Lines | Function |
|-----------|-------|----------|
| `doki_hidden` | ~200 | Hide container internal data (`/proc/self/cmdline`, cgroups, etc.) |
| `doki_portswitch` | ~250 | TCP/UDP port forwarding (bind + connect interception) |
| `daemon` | ~300 | Daemon mode with Unix socket listener |
| `ipc/protocol` | ~450 | Bidirectional JSON IPC message handling |

## Daemon Mode

```bash
# Start doki-proot in daemon mode
doki-proot --daemon --socket /tmp/doki-proot.sock --rootfs /path/to/rootfs

# Send commands via any JSON client
echo '{"type":"health"}' | nc -U /tmp/doki-proot.sock
# → {"type":"health","status":"ok","pid":12345}

echo '{"type":"exec","id":"1","cmd":["echo","hello"]}' | nc -U /tmp/doki-proot.sock
# → {"type":"stdout","id":"1","data":"hello\n","code":0}
# → {"type":"exit","id":"1","code":0}

echo '{"type":"shutdown"}' | nc -U /tmp/doki-proot.sock
# → {"type":"shutdown_ack","status":"ok"}
```

## Fallback to system proot

Doki searches for `doki-proot` first, then falls back to the system-installed `proot`:

```
1. ./doki-proot          (next to doki binary)
2. ~/.doki/doki-proot    (doki data directory)
3. doki-proot             (in PATH)
4. proot                  (system fallback)
```

## Building

```bash
# ARM64 Android (Termux)
CC=aarch64-linux-android-gcc cc -O2 -s -o doki-proot doki-proot-main.c

# ARMv7 Android
CC=armv7-linux-androideabi-gcc cc -O2 -s -o doki-proot doki-proot-main.c

# Linux x86_64
cc -O2 -s -o doki-proot doki-proot-main.c

# Linux ARM64
aarch64-linux-gnu-gcc -O2 -s -o doki-proot doki-proot-main.c
```

## Binaries

| Platform | Size |
|----------|------|
| `doki-proot-android-arm64` | ~14K |
| `doki-proot-android-armv7` | ~13K |
| `doki-proot-linux-amd64` | ~12K |
| `doki-proot-linux-arm64` | ~12K |

## Source Structure

```
doki-proot/
├── doki-proot-main.c          # Entry point + daemon + IPC (self-contained)
├── daemon/
│   ├── daemon.c                # Daemon event loop
│   └── daemon.h
├── ipc/
│   ├── protocol.c              # JSON message parsing
│   └── protocol.h
├── extension/
│   ├── doki_hidden.c           # Container data hiding
│   ├── doki_hidden.h
│   ├── doki_portswitch.c       # Port forwarding
│   └── doki_portswitch.h
├── src/                        # Full upstream PRoot source (28,350 lines)
│   ├── cli/                    # CLI (simplified)
│   ├── tracee/                 # Process tracing
│   ├── syscall/                # Syscall interception
│   ├── path/                   # Path translation
│   ├── ptrace/                 # Ptrace wrappers
│   ├── execve/                 # Exec handling
│   ├── loader/                 # ELF loader
│   └── extension/              # All preserved extensions
├── build/                      # Compiled binaries
└── Makefile
```

## License

**GPL-2.0** — Inherited from the original PRoot by STMicroelectronics.

Doki-proot runs as a separate process invoked via `exec()`, keeping the main Doki engine (Apache 2.0) as a separate work.

```
Copyright (C) 2015 STMicroelectronics
Copyright (C) 2026 OpceanAI (doki extensions)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version..
```

---

<div align="center">

### The engine that powers Doki containers on any Linux kernel.

[![OpceanAI](https://img.shields.io/badge/OpceanAI-2026-0D1117?style=for-the-badge)](https://github.com/OpceanAI)

</div>

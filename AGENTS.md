# Repository Notes for Agents and Developers

## Project background

This repository contains a Wuhan University Computer School lab variant of
MIT's 2019 `xv6-riscv` labs. The current host used for local development is an
Apple Silicon Mac. xv6 is cross-compiled for RISC-V and runs under QEMU; it is
not compiled as a native macOS program.

## Verified macOS environment

The following environment was verified on 2026-09-09:

- Apple Silicon (`arm64`), macOS 26.6.2
- Homebrew QEMU 11.1.1
- Homebrew `riscv64-elf-gcc` 16.2.0
- Homebrew `riscv64-elf-binutils` 2.47
- Apple Clang 21.0.0 for the host-side `mkfs` utility
- GNU Make 3.81 supplied by macOS

Install the required packages with:

```sh
brew install qemu riscv64-elf-gcc
```

Build and run from the repository root with:

```sh
make clean
make qemu
```

The Makefile now detects Homebrew's `riscv64-elf-` prefix automatically, so
setting `TOOLPREFIX` manually is not required. Exit QEMU with `Ctrl-A`, then
`X`.

## macOS and modern-toolchain compatibility changes

Three compatibility changes are intentionally present. Preserve them when
working on lab code or rebasing lab branches.

### Homebrew toolchain prefix

`Makefile` recognizes `riscv64-elf-` in addition to the older
`riscv64-unknown-elf-` and Ubuntu's `riscv64-linux-gnu-` prefixes. Homebrew's
current cross-compiler installs commands such as `riscv64-elf-gcc`.

### GCC 16 warnings

The Makefile disables two warnings when the selected compiler accepts the
corresponding options:

- `-Wno-infinite-recursion`: xv6's shell deliberately walks a command tree
  recursively. GCC 16 diagnoses this design as infinite recursion.
- `-Wno-unused-but-set-variable`: the old `usertests.c` assigns the
  `fsblocks` test statistic without later consuming it.

The rest of `-Wall -Werror` remains enabled. These flags address compiler
version differences and do not alter xv6 runtime behavior.

### QEMU physical memory protection

`kernel/riscv.h` defines `w_pmpcfg0()` and `w_pmpaddr0()`. `kernel/start.c`
uses them before `mret` to grant Supervisor mode access to physical memory:

```c
w_pmpaddr0(0x3fffffffffffffull);
w_pmpcfg0(0xf);
```

The original 2019 source relied on older QEMU behavior that allowed this
access implicitly. QEMU 11 otherwise raises an instruction access fault at
the final `mret` in `start()`, producing no console output. This PMP setup is
also the approach used by modern xv6 and does not change lab-specific logic.

## Verification performed

After a clean rebuild, the patched repository was started with `make qemu`.
The following behavior was observed:

- the kernel printed `xv6 kernel is booting`;
- all three configured harts started;
- the virtio root disk initialized;
- `init` launched `sh` and displayed the `$` prompt;
- `echo mac-xv6-ok` printed the expected text;
- `ls` listed the root filesystem and compiled user programs.

Linker warnings that an xv6 binary has an RWX load segment are expected for
this old teaching kernel and did not prevent booting.

## Cross-platform notes

Ubuntu remains supported. With `gcc-riscv64-linux-gnu`,
`binutils-riscv64-linux-gnu`, and `qemu-system-misc` installed, the normal
commands remain `make clean` and `make qemu`. Always perform a clean build
when moving a working tree between Ubuntu and macOS because generated `.d`
files can contain absolute compiler paths.

Before submitting, review the three compatibility changes separately from
the lab implementation so the course submission contains only intended
changes under the platform's rules.

## Workspace hygiene

- Generated objects, dependency files, images, assembly listings, and symbol
  files are ignored and may be regenerated with `make clean` and `make`.
- Do not delete or modify unrelated user files. In particular,
  `kernel/.kalloc.c.swp` was already present as an untracked Vim swap file and
  is not part of the macOS compatibility work.
- Do not commit changes unless the user explicitly requests a commit.

## Lab branch organization

Each course lab lives on its own branch, such as `lab3` or `lab4`. Different
branches may start from different upstream xv6 versions, so do not merge one
lab branch into another. A `labN-start` tag records the platform snapshot
before work begins, and a `labN-finished` tag may record the tested result.

Keep course implementation, lab guides, and host compatibility changes in
separate commits. This makes it possible to inspect or submit only the changes
required by the course platform.

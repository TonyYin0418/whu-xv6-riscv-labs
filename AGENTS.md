# Repository Notes for Agents and Developers

## Project background

This repository contains a Wuhan University Computer School lab variant of
MIT's 2019 `xv6-riscv` labs. It is used for the 2026–2027 first-semester
course *Operating Systems Practice A*, a required professional-practice course
for the 2024 Lei Jun Class taught by Ruoshan Kong.

The current host used for local development is an Apple Silicon MacBook Air
with an M3 processor and macOS Tahoe 26.6.2. xv6 is cross-compiled for RISC-V
and runs under QEMU; it is not compiled as a native macOS program.

## Verified macOS environment

The following environment was verified on 2026-09-09:

- MacBook Air with Apple M3 (`arm64`), macOS Tahoe 26.6.2
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
- Respect the course's academic-integrity requirements. Do not help someone
  copy lab implementations before they have completed the work independently.
- Do not add files, targets, tests, or bookkeeping artifacts that the lab does
  not require. In particular, Lab4 does not need a `time.txt` submission file
  on the school's assessment platform.
- Remove temporary submission archives after they have served their purpose.
  A submission archive must contain only the lab files requested by the course;
  exclude macOS compatibility changes, guides, build products, and local notes.

## Lab branch organization

`main` is the remote default branch. It contains repository guidance only and
does not contain a lab implementation. Each course lab lives on its own
branch. `lab3` covers Buddy allocation and lazy allocation; `lab4` covers
copy-on-write `fork` and related memory-management work.

Different lab branches may start from different upstream xv6 versions, so do
not merge one lab branch into another. When the user supplies a corrected lab
archive, the last version they explicitly confirm is the authoritative starting
tree. Do not retain that archive's upstream Git ancestry, authors, remote
branches, or remote URL. Record its files as a new parentless root commit named
`labN: record initial platform snapshot`, and point the lightweight `labN-start`
tag at that commit. Do not create `labN-finished` tags; the branch head
represents its current state.

Keep commits in this order, matching the established Lab3 layout:

1. parentless initial platform snapshot and `labN-start` tag;
2. shared `AGENTS.md` repository notes;
3. host and modern-toolchain compatibility;
4. course lab implementation;
5. detailed lab guide.

Keep course implementation, lab guides, and host compatibility changes in
separate commits. Prefer the smallest changes that directly satisfy the lab,
with concise Chinese comments around non-obvious logic rather than excessive
annotation or extra abstractions. This makes it possible to inspect or submit
only the changes required by the course platform.

## Lab4-specific notes

- Lab4 implements copy-on-write `fork` from the user's corrected Lab4 archive.
- The xv6 command and test program are both named `cowtest`; the source is
  `user/cowtest.c`. Do not add or invoke a separate `cow` program.
- The corrected baseline already contains the `_cowtest` Makefile entry,
  `FSSIZE=2000`, and QEMU's `-bios none`; these are not student changes.
- The implementation should stay limited to the required COW page-table,
  reference-counting, trap, and `copyout` behavior. Compatibility-only Makefile
  changes belong to the separate build commit.
- During development, avoid repeated broad regression runs. Perform one final
  clean build and the required lab validation when needed. The school platform
  is authoritative when its result differs from a slow local grader timeout.
- The detailed Chinese guide should explain the experiment as a connected
  sequence of purposes and mechanisms, show important A-to-B code comparisons,
  and explain special functions and key lines without commenting every trivial
  statement.

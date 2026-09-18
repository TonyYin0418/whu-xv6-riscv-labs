# WHU xv6-riscv Labs

武汉大学计算机学院 2026-2027 学年第一学期《操作系统实践 A》课程实验记录，面向 2024 级雷军班。课程以 MIT PDOS 的 [xv6-riscv](https://github.com/mit-pdos/xv6-riscv) 教学操作系统及其 2019 年实验为基础，并按本课程要求调整了任务和测评。

> `main` 是导航分支，不包含某个 Lab 的实现。请切换到对应的 `labN` 分支查看代码、过程说明和提交记录。

## 课程模块

完整要求见 [课程设计任务书](%E8%AF%BE%E7%A8%8B%E8%AE%BE%E8%AE%A1%E4%BB%BB%E5%8A%A1%E4%B9%A6.pdf)。课程分为两个模块：

1. **希冀平台 xv6 实验**：包括入门、系统调用、内存分配、COW、文件系统、mmap、锁、网络、简单 shell、用户态线程与闹钟。本文档中的 Lab3-Lab10 属于该模块。
2. **xv6 的裁剪与拼接**：从启动到 S 模式开始，逐步补齐内存管理、中断、首个进程、系统调用、调度、磁盘、文件系统和 shell 等九个阶段；这是独立的后续模块，并非本仓库现有 Lab3-Lab10 的本地替代测试。

希冀平台是课程实验的测试与提交入口；本地 QEMU 仅用于开发、调试和预验证。本地结果不能替代平台测评结果。

## 实验分支

不同 Lab 可能基于不同上游快照，彼此独立。不要将一个 Lab 分支直接合并到另一个。

| 分支 | 主题 | 本地预验证入口 |
| --- | --- | --- |
| [`lab3`](https://github.com/TonyYin0418/whu-xv6-riscv-labs/tree/lab3) | Buddy 分配与惰性页分配 | 以课程平台要求为准 |
| [`lab4`](https://github.com/TonyYin0418/whu-xv6-riscv-labs/tree/lab4) | Copy-on-Write `fork` | [`cowtest`](https://github.com/TonyYin0418/whu-xv6-riscv-labs/blob/lab4/user/cowtest.c) |
| [`lab5`](https://github.com/TonyYin0418/whu-xv6-riscv-labs/tree/lab5) | 文件系统：大文件与符号链接 | `symlinktest`、`bigfile`、`usertests` |
| [`lab6`](https://github.com/TonyYin0418/whu-xv6-riscv-labs/tree/lab6) | `mmap` | `mmaptest`、`usertests` |
| [`lab7`](https://github.com/TonyYin0418/whu-xv6-riscv-labs/tree/lab7) | 锁竞争优化 | `kalloctest`、`bcachetest`、`usertests` |
| [`lab8`](https://github.com/TonyYin0418/whu-xv6-riscv-labs/tree/lab8) | E1000 与 UDP 套接字 | [`nettests`](https://github.com/TonyYin0418/whu-xv6-riscv-labs/blob/lab8/user/nettests.c) |
| [`lab9`](https://github.com/TonyYin0418/whu-xv6-riscv-labs/tree/lab9) | 简单 xv6 shell | `testsh nsh` |
| [`lab10`](https://github.com/TonyYin0418/whu-xv6-riscv-labs/tree/lab10) | 用户态线程与闹钟 | `uthread`、`alarmtest`、`usertests` |

## 获取、构建与运行

```sh
git clone https://github.com/TonyYin0418/whu-xv6-riscv-labs.git
cd whu-xv6-riscv-labs
git switch --track origin/lab10  # 替换为所需 Lab
make clean
make qemu
```

在 xv6 shell 中运行上表所列的程序。退出 QEMU：先按 `Ctrl-A`，松开后按 `X`。

Apple Silicon macOS 可通过 Homebrew 安装依赖：

```sh
brew install qemu riscv64-elf-gcc
```

Ubuntu 可安装 `gcc-riscv64-linux-gnu`、`binutils-riscv64-linux-gnu` 和 `qemu-system-misc`。跨操作系统或工具链切换工作树时，请先执行 `make clean`。

## 本地开发说明

xv6 的目标架构为 RISC-V，不是原生 macOS 或 Linux 程序：内核和用户程序由 RISC-V 交叉编译器构建，随后由 QEMU 运行。Homebrew 的 `riscv64-elf-` 工具链以及新版 QEMU 已在各 Lab 分支中适配。

本仓库用于课程学习与实验记录。应独立理解实验目标、代码和测试结果，禁止将他人实现直接抄袭后作为个人作业提交；具体实验要求和评分结果以希冀平台及课程文档为准。

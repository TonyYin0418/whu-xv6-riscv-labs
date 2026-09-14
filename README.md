# WHU xv6-riscv Labs

本仓库用于武汉大学计算机学院 **2026—2027 学年第一学期《操作系统实践 A》**
课程实验。这是 2024 级雷军班的必修专业实践课程，任课教师为孔若衫老师。

> 当前分支是仓库导航分支，不包含任何一个 Lab 的实验实现。
> 请切换到对应的 `labN` 分支查看代码、测试与提交记录。

## 快速开始

以 Lab4 为例，克隆仓库、切换实验分支并启动 xv6：

```sh
git clone https://github.com/TonyYin0418/whu-xv6-riscv-labs.git
cd whu-xv6-riscv-labs
git switch --track origin/lab4
make clean
make qemu
```

退出 QEMU：先按 `Ctrl-A`，再按 `X`。

## 实验基础

实验以麻省理工学院（MIT）PDOS 实验室的教学操作系统
[xv6-riscv](https://github.com/mit-pdos/xv6-riscv) 及其 2019 年课程实验为基础。
xv6 是以 ANSI C 编写、运行在 RISC-V 多处理器平台上的 Unix Version 6
教学版实现。武汉大学根据课程教学目标对实验内容、测试程序和评分要求作了适当调整，
因此本仓库的任务与 MIT 后续版本的 xv6 Labs 不一定完全一致。

## 分支导航

每个实验独立保存在一个分支中，不同 Lab 可能基于不同的上游快照。请勿直接合并不同
Lab 分支。

| 分支 | 实验主题 | 主要内容 |
| --- | --- | --- |
| `main` | 仓库导航 | 课程说明与分支索引，不含实验实现 |
| `lab3` | 内存分配 | Buddy（伙伴）内存分配与惰性页分配（Lazy Allocation） |
| `lab4` | 写时复制 | 为 `fork` 实现 Copy-on-Write（COW）等相关内存管理机制 |

后续实验可继续使用 `labN` 的命名方式添加到本表。

## 获取实验内容

先查看本地和远程已有分支：

```sh
git branch --all
```

切换到某个已经存在于本地的实验分支，例如：

```sh
git switch lab3
```

如果分支只存在于远程，则创建相应的本地跟踪分支：

```sh
git switch --track origin/lab3
```

切换前请使用 `git status` 检查尚未提交的修改，避免把一个实验的改动带入另一个实验。

## 构建与运行

xv6 需要 RISC-V 交叉编译工具链，并通过 QEMU 运行，不能作为原生 macOS 程序编译。
在 Apple Silicon Mac 上可使用 Homebrew 安装依赖：

```sh
brew install qemu riscv64-elf-gcc
```

进入具体 Lab 分支后执行：

```sh
make clean
make qemu
```

Ubuntu 可使用
`gcc-riscv64-linux-gnu`、`binutils-riscv64-linux-gnu` 和
`qemu-system-misc`。在不同操作系统之间切换工作树后应始终先执行 `make clean`。

### 实验验证

请以对应 Lab 发布的实验文档和评分要求为准。Lab4 可在 xv6 shell 中运行：

```sh
cowtest
```

其测试程序源文件为 `user/cowtest.c`。

### 验证环境

本仓库于 2026 年 9 月在以下环境完成过干净构建和启动验证：

- MacBook Air（Apple M3，`arm64`）
- macOS Tahoe 26.6.2
- Homebrew QEMU 11.1.1
- Homebrew `riscv64-elf-gcc` 16.2.0 与 `riscv64-elf-binutils` 2.47
- Apple Clang 21.0.0（编译宿主机侧的 `mkfs` 工具）
- macOS 自带的 GNU Make 3.81

Apple M3 是 ARM64 架构，而 xv6 的目标架构是 RISC-V。因此内核和用户程序由 RISC-V
交叉编译器生成，再交给 QEMU 模拟运行；只有制作文件系统镜像的 `mkfs` 等宿主机工具
由 Apple Clang 编译为原生 macOS 程序。

### macOS 兼容

各 Lab 分支保留了以下三类平台兼容改动。它们用于适配当前 Homebrew 工具链与新版
QEMU，不改变课程实验要求的核心逻辑。

1. **识别 Homebrew 工具链前缀。** `Makefile` 除了识别旧版常见的
   `riscv64-unknown-elf-` 和 Ubuntu 的 `riscv64-linux-gnu-`，也会自动识别
   Homebrew 使用的 `riscv64-elf-`，因此通常无须手动设置 `TOOLPREFIX`。
2. **兼容 GCC 16 的新增诊断。** 仅当编译器支持对应选项时，`Makefile` 会关闭
   `-Winfinite-recursion` 和 `-Wunused-but-set-variable`。前者会误报 xv6 shell
   有意采用的递归命令树遍历，后者来自旧版 `usertests.c` 中写入后未使用的测试统计量。
   其余 `-Wall -Werror` 检查仍然启用。
3. **为新版 QEMU 配置物理内存保护（PMP）。** 旧版 xv6 依赖早期 QEMU 默认允许
   Supervisor 模式访问物理内存；QEMU 11 不再隐式提供该权限。启动代码在执行 `mret`
   前配置 PMP，授予 Supervisor 模式所需的物理内存访问权限，否则内核会在进入
   Supervisor 模式时触发指令访问异常，控制台也不会出现启动信息。这一处理沿用现代
   xv6 的做法。

## 说明

- **请独立完成课程实验；在尚未独立完成相应 Lab 时，请勿查看或参考本仓库中的实现代码。**
- 本仓库用于课程学习与实验记录，具体要求以课程发布的实验文档和评分标准为准。
- xv6 原始代码版权归其作者所有；项目背景及原作者信息请参阅各 Lab 分支中的原始说明。

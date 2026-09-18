# Lab5：大文件与符号链接

## 目标与基线

本实验基于课程归档的 `origin/fs`（`03b7721`）完成两项文件系统扩展：

1. 用双重间接块把单个 inode 的最大文件从 268 块扩展到 65,803 块；
2. 增加 `symlink(target, path)`，并让 `open` 能按需跟随符号链接。

起始树中的 `bigfile.c` 和 `symlinktest.c` 是测试程序，不是新增的学生程序。为容纳
65,803 个 1 KiB 数据块及元数据，`FSSIZE` 从 2,000 调整为 200,000；每次干净构建都会
重新生成 `fs.img`。

## 双重间接块

原 inode 的地址布局是 12 个直接地址加 1 个单重间接地址：

```text
直接块[0..11] + 单重间接块[256] = 268 个数据块
```

磁盘 inode 的大小必须保持不变。因此将第 12 个直接地址改为双重间接地址：

```text
直接块[0..10] + 单重间接块[256]
              + 双重间接块[256 × 256]
            = 11 + 256 + 65536 = 65803
```

`kernel/fs.h` 中定义 `NDINDIRECT` 和新的 `MAXFILE`，并把 `dinode.addrs` 改为
`NDIRECT + 2`。`kernel/file.h` 的内存 inode 必须同步修改，否则 `iupdate()` 在内存与
磁盘 inode 间复制地址数组时会破坏布局。

### `bmap()` 的变化

原来的 `bmap()` 仅在逻辑块号落入直接块或单重间接块时返回数据块地址。新分支先减去
`NDIRECT + NINDIRECT`，再把剩余逻辑块号拆成两级索引：

```c
outer = bn / NINDIRECT;
inner = bn % NINDIRECT;
```

第一级索引从 inode 的双重间接块中找到单重间接块，第二级索引从该单重间接块找到数据
块。两层缺失时都按需 `balloc()`，并在修改缓存块地址表后调用 `log_write()`。每次
`bread()` 都有对应的 `brelse()`，避免耗尽缓冲块。

### `itrunc()` 的变化

删除文件时，不能只释放最外层双重间接块。新代码依次释放：数据块、每个单重间接块、
最后的双重间接块，并清零 inode 地址项。这样 inode 被 `unlink` 后可以回收完整的大文件
空间，而不会遗留磁盘块泄漏。

## 符号链接

### 系统调用链

新增 `SYS_symlink`、用户态声明和 `usys.pl` 桩代码；`kernel/stat.h` 增加
`T_SYMLINK`，`kernel/fcntl.h` 增加不与既有打开位冲突的 `O_NOFOLLOW`。Makefile 将
`_symlinktest` 放入文件系统镜像。

`sys_symlink()` 通过 `create(path, T_SYMLINK, 0, 0)` 创建 inode，再将以 NUL 结尾的
目标路径写入 inode 数据块。目标在创建时可以不存在，这正是软链接与硬链接的重要差异。

### `open()` 的变化与并发性

普通 `open` 取得并锁定 inode 后，若它是 `T_SYMLINK` 且没有 `O_NOFOLLOW`，便读取
其目标路径、释放旧 inode，并对目标重新执行 `namei()`。最多跟随 10 层；超出限制即返回
错误，因此 `a -> b -> a` 之类的环不会无限循环。

读取目标字符串期间保持 inode 锁。这样并发 `unlink` 不能在读取链接数据的临界区释放
该 inode；读取完成后由 `iunlockput()` 交还引用，再安全地解析下一层路径。带
`O_NOFOLLOW` 的打开直接返回符号链接 inode，供 `fstat` 观察其 `T_SYMLINK` 类型。

## 验证

在 Apple Silicon / Homebrew RISC-V 工具链与 QEMU 11 环境执行：

```text
make clean && make qemu
$ symlinktest
test symlinks: ok
test concurrent symlinks: ok

$ bigfile
wrote 65803 blocks
bigfile done; ok

$ usertests
ALL TESTS PASSED
```

构建中的 RWX 段链接器警告是旧 xv6 的已知现象，不影响启动或测试结果。

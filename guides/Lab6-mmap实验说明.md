# Lab6：惰性文件映射

## 目标

本实验基于课程归档 `origin/mmap`（`2ce91ae`），实现受限的
`mmap(addr, length, prot, flags, fd, offset)` 与 `munmap(addr, length)`：

- `addr` 与 `offset` 仅接受 0；
- 支持 `PROT_READ`、`PROT_WRITE`、`MAP_PRIVATE`、`MAP_SHARED`；
- 以缺页异常按需读入文件页；
- 支持从映射区开头、结尾或全部解除映射；
- `MAP_SHARED` 的可写、已装入页在解除映射或退出时回写；
- `fork` 复制 VMA 元数据和文件引用，子进程再次访问时独立按需装页。

## VMA：把映射描述与物理页分离

`struct proc` 新增固定的 16 项 `vma[]`。每项记录起始虚拟地址、页对齐长度、文件偏移、
权限、共享方式和 `struct file *`。`mmaptop` 从高于普通堆的地址开始单调分配，且始终低于
`TRAPFRAME`，因而不会覆盖堆、栈、陷阱帧或 trampoline。

`sys_mmap()` 不调用 `kalloc()` 或 `readi()`；它验证文件描述符及权限，增加文件引用，登记
VMA 后直接返回地址。这是“大文件映射不应立即消耗物理内存”的关键。

## 从页错误装入映射页

原 `usertrap()` 遇到加载或存储页错误会杀死进程。新路径仅对 `scause` 13/15 查找覆盖故障
地址的 VMA：

1. 将地址向下对齐到页边界，并检查读取/写入是否被 `prot` 允许；
2. 分配并清零一页；
3. 锁定 inode，以 `VMA offset + 页内相对偏移` 调用 `readi()`；文件末尾后的部分保留为零；
4. 以 `PTE_U` 和相应读写位映射页面。

RISC-V 不允许只有 `PTE_W` 而没有 `PTE_R` 的叶 PTE，因此可写映射同时设置读位；这是硬件
页表格式约束，不改变本实验测试所需的语义。

## 解除映射与回写

普通 `uvmunmap()` 假定每页都已映射，不能直接用于惰性 VMA。`uvmunmap_lazy()` 只释放实际
存在的叶 PTE，未访问页直接跳过。

`vma_munmap()` 限制地址范围为一个 VMA 的前缀、后缀或全部，先对 `MAP_SHARED | PROT_WRITE`
的已映射页逐页执行 `writei()`，再调用 `uvmunmap_lazy()`。若删除前缀，还同时推进 VMA 的
文件偏移；整个 VMA 消失时释放其额外文件引用。

`exit()` 和 `freeproc()` 通过 `vma_free()` 执行同一清理逻辑，确保高地址映射页在页表递归
释放前被移除。否则 `freewalk()` 会看到遗留叶 PTE 并触发内核 panic。

## fork

旧 `uvmcopy()` 只复制 `[0, p->sz)` 的普通用户内存，不应把 VMA 高地址页当作堆复制。`fork()`
仅复制 VMA 元数据并为每项调用 `filedup()`；子进程首次访问映射时从同一文件重新装入自己的
物理页。这样满足本实验的共享文件可见性要求，同时避免额外的物理页共享和复杂同步。

## 验证

在 macOS/Homebrew RISC-V 工具链和 QEMU 11 上完成干净构建后验证：

```text
$ mmaptest
mmap_test starting
mmap_test OK
fork_test starting
fork_test OK
mmaptest: all tests succeeded

$ usertests
ALL TESTS PASSED
```

`usertests` 中主动制造的非法访问会打印预期页错误信息，随后对应测试项显示 `OK`；最终
`ALL TESTS PASSED` 是回归通过的判据。

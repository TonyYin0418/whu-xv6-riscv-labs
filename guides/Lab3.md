# 实验目的和任务

本实验要求使用伙伴内存管理系统来分配和释放内核中的文件描述结构，这样的话，xv6-riscv就能拥有超过NFILE限制的打开文件描述符。更进一步地要求，在用户内存管理中，使用延后的方法分配内存。

xv6 只有一个页分配器，不能动态分配小于一个页的对象。为了绕过这个限制，xv6 将小于一个页的对象声明为静态的。例如，xv6 声明了一个文件结构体数组（file structs），一个进程结构体数组（proc structures）等。因此，系统可以打开的文件数量受到静态声明的文件数组大小的限制，这个数组包含 NFILE 个条目（参见 kernel/file.c 和 kernel/param.h）。

解决方案是采用 buddy 分配器，我们已经将其添加到 xv6 中，对应的文件是 kernel/buddy.c 和 kernel/list.c。

# 伙伴算法内存分配（一）

你的任务是进一步改进 xv6 的内存分配方式，包括两个方面：

修改 kernel/file.c，使用 buddy 分配器，使得文件结构体的数量由内存大小限制，而不是由 NFILE 限制。

buddy 分配器的空间效率较低。alloc 数组为每种大小的每个块都保留一个位。这里有一个巧妙的优化方法，它能将开销减少为每对块只用一个位。这个单个位的值是 B1_is_free 异或 B2_is_free（XOR），其中 B1 和 B2 是一对 buddy 块。每次分配或释放一个块时，翻转这个位以反映状态变化。

例如，如果 B1 和 B2 都已分配，这个位将为 0；如果 B1 被释放，这个位变为 1；如果该位是 1 且 B2 被释放，那么我们知道 B1 和 B2 应该合并。当 xv6 使用 buddy 分配器管理约 128MB 的空闲内存时，这个优化可以节省大约 1MB 的内存，因为每个块节省了 1/2 位。

为了帮助你测试你的实现，我们提供了一个 xv6 程序，名为 alloctest（源代码在 user/alloctest.c）。它包含两个测试。

第一个测试通过创建许多进程，每个进程打开多个文件描述符，从而分配超过 NFILE 的文件结构体。这个测试在未经修改的 xv6 上会失败。

第二个测试创建一个进程，分配尽可能多的内存，如果少于某个指定数量就会失败。这个测试实际上是在测试可用内存的多少。

如果内核使用了太多内存，测试就会失败。在我们提供的未经优化的 buddy分配器下，这个测试将失败。


当你实现了以后，内核应该可以运行 alloctest 和 usertests。也就是说：

$ alloctest

filetest: start

filetest: OK

memtest: start

memtest: OK

$ usertests

...

ALL TESTS PASSED

$

# 伙伴算法内存分配（二）

注意：

你需要删除 kernel/file.c 中第 19 行的代码，它声明了 file[NFILE]。

然后在 filealloc 中使用 bd_malloc 来分配 struct file。

在 fileclose 中释放这些分配的内存。注意你可以简化 fileclose，因为 ff 不再需要。

fileclose 仍然需要获取 ftable.lock，因为该锁保护的是 f->ref。

bd_malloc 返回的内存没有清零；换句话说，分配的内存保留了上一次使用时的内容。调用方不应假设它是清零的。

你可以使用 bd_print 来打印分配器的状态。

调用 bd_init 时会传入可分配的物理内存范围。

bd_init 会使用这部分内存为 buddy 数据结构分配内存。

它会适当地初始化其数据结构：对于用于 buddy 数据结构的内存会标记为已分配，从而不会被重新分配。

此外，我们对 bd_init 做了改动，使其能够处理不是 2 的幂的内存大小——通过将不可用的内存标记为已分配来处理。

最后，我们修改了 buddy 分配器，使其使用锁来串行化并发调用。


# 内存页的延迟分配（一）

本步骤是实现内存页延迟分配的第一步：将当前sbrk(n)系统调用实现中的页分配机制删除。sbrk(n)系统调用将进程的内存大小增加n字节，并且返回新分配的内存区域。

本步骤的修改完成后，sbrk(n)系统调用仅完成对进程结构的sz（内存大小值）成员的增加操作（并返回旧的内存大小值），本步骤不进行内存分配，所以需要实验者删除对growproc()的调用。

本步骤完成后，执行命令（例如echo hi）会报内存错误：

init: starting sh
$ echo hiusertrap(): unexpected scause 0x000000000000000f pid=3
            sepc=0x0000000000001258 stval=0x0000000000004008
va=0x0000000000004000 pte=0x0000000000000000
panic: uvmunmap: not mapped

以上的"usertrap():"信息由用户态异常处理函数报出(trap.c)；表明其捕获了一个其不知如何处理的异常。本步骤需要实验者理解为什么会发生这样的页错误。"stval=0x0..04008"表明引发页错误的虚地址为0x4008

# 内存页的延迟分配（二）

本步骤修改trap.c里的代码，通过将新分配的物理页映射到错误地址来响应用户态引发的页错误，然后返回用户态以让进程继续执行。实验者需要在产生“usertrap():..."消息的printf语句之前加入处理代码。如果添加正确，测试命令usertests会执行通过。

建议可以从trap.c的usertrap函数入手，不断修改代码，一直到"echo hi"命令不报错。本实验的最终目的是不断修改代码，一直到usertests命令执行通过（这是下一步骤的工作）。

注意事项：

实验者可以在usertrap()函数通过查看r_scause()是否是13或15来检查错误是否是页错误。

参考usertrap()函数里报页错误的printf的参数来学习怎样得到引发页错误的虚地址。

从vm.c的uvmalloc()函数中借鉴思想（这也是修改前sbrk()调用通过growproc()分配内存的思路）。实验者需要用到kalloc()和mappage()。

使用PGROUNDDOWN(va)来将错误的虚地址向下对其到页边界。

uvmunmap()有可能会崩溃；修改它以便当有些页未被映射时不崩溃。

当内核崩溃时，可以通过make qemu-gdb，以及gdb-multiarch ./kernel来调试内核。

可以打印页表内容作为调试手段。

如果在编译时遇到"incompletetype proc"错，在源代码中加入 include "proc.h"及include "spinlock.h"。

如果一切顺利，实验者的内存页延迟分配机制会让"echo hi"正常工作。"echo hi"的执行过程至少会引发一次页错误（也就是延迟分配），或者是两次页错误。

# 内存页的延迟分配（三）

不断修正内存页延迟分配的代码，以使usertests测试通过。

注意事项：

正确处理赋予sbrk()的负值参数。

当引发页错误的虚地址高于sbrk()分配的最高地址时，杀掉进程。

正确处理fork()。

正确处理进程把sbrk()返回的虚地址传给一个系统调用（例如read和write），而与地址有关的内存还未分配的情形。

正确处理内存耗尽：当页错误处理调用kalloc()失败时，杀掉当前进程。

处理在堆栈底部之下的非法页错误。

当能够通过lazytest和usertests时，实验者对内存页的延迟分配实现才算成功：

$lazytests
lazytests staring
running test lazy alloc
test lazy alloc: OK
running test lazy unmap...
usertrap(): ...
test lazy unmap: OK
running test out of memory
usertrap(): ...
test out of memory: OK
ALL TESTS PASSED
$usertests...
ALL TESTS PASSED
$



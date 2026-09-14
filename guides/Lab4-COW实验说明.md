# Lab4：写时拷贝 fork 实验说明

## 1. 实验要解决什么问题

原来的 `fork()` 会立即为子进程分配物理页，并逐页复制父进程的全部用户内存。假设父进程占用了 80 MB，即使子进程马上执行 `exec()`，内核仍会先完成这 80 MB 的分配和复制，既慢又浪费内存。

写时拷贝（Copy-on-Write，COW）的思路是：

1. `fork()` 时，父子进程暂时映射同一批物理页，不复制页面内容。
2. 原本可写的页面在父子页表中都改成只读，并用软件位 `PTE_COW` 标记。
3. 父进程或子进程第一次写这个页面时，CPU 产生写页面异常。
4. 内核为写入者复制一份页面，再恢复该页面的写权限。
5. 物理页只有在最后一个引用消失时才能真正释放。

整个实验可以用一条逻辑线串起来：

```text
fork
  |
  v
父子共享物理页 + 清除 PTE_W + 设置 PTE_COW
  |
  +--> 只读：继续共享，不发生复制
  |
  +--> 用户态写：scause=15 --> usertrap --> cowalloc
  |
  +--> 内核向用户页写：copyout ----------> cowalloc
                                      |
                                      v
                         独占时直接恢复写权限
                         共享时分配、复制、换页
```

## 2. 本次修改范围

| 文件 | 作用 |
| --- | --- |
| `Makefile` | 本地增加 Homebrew 工具链和 GCC 16 兼容参数 |
| `kernel/start.c` | 本地为新版 QEMU 配置 PMP 物理内存权限 |
| `kernel/riscv.h` | 定义 PMP 写函数和 `PTE_COW` |
| `kernel/kalloc.c` | 为物理页增加引用计数 |
| `kernel/defs.h` | 声明新增的跨文件函数 |
| `kernel/vm.c` | 改造 `uvmcopy()`，实现公共的 `cowalloc()`，处理 `copyout()` |
| `kernel/trap.c` | 捕获用户态写页面异常 |

实现尽量沿用原来的函数和锁，没有增加新模块，也没有改动 `fork()` 的主体。`fork()` 原本就调用 `uvmcopy()`，所以改变 `uvmcopy()` 的语义即可。

## 3. 先完成当前 MacBook 的运行适配

### 3.1 正确骨架已有的 QEMU 配置

正确 Lab4 骨架已经自带 `-bios none` 和 128 MB 内存配置，因此这里不需要修改：

```make
QEMUOPTS = -machine virt -bios none -kernel $K/kernel -m 128M -smp $(CPUS) -nographic
```

`-bios none` 的含义是让 QEMU 不加载默认 BIOS/固件，只加载 `-kernel` 指定的 xv6 内核。

### 3.2 Homebrew 与 GCC 16

参考 `lab3` 的平台适配，在工具链探测中增加 Homebrew 的前缀：

修改前（A）：

```make
elif riscv64-linux-gnu-objdump ...
```

修改后（B）：

```make
elif riscv64-elf-objdump -i ...
	then echo 'riscv64-elf-';
elif riscv64-linux-gnu-objdump ...
```

同时使用 `-std=gnu17` 保持 2019 年旧式 C 函数声明的语义，并按编译器是否支持来关闭 GCC 16 新增的两项警告：

```make
CFLAGS = ... -std=gnu17
CFLAGS += $(shell $(CC) -Wno-infinite-recursion ...)
CFLAGS += $(shell $(CC) -Wno-unused-but-set-variable ...)
```

新版 QEMU 还要求机器态在 `mret` 前通过 PMP 授权 S 模式访问物理内存：

```c
w_pmpaddr0(0x3fffffffffffffull);
w_pmpcfg0(0xf);
```

这些属于本地环境兼容。正确骨架原有的 `FSSIZE=2000`、`_cowtest`、Buddy 相关文件均保持不变。

## 4. 第一步：给 PTE 增加 COW 标志

### 4.1 为什么需要单独的标志

清除 `PTE_W` 后，只看页表无法分辨下面两种页面：

- 原本可写，因为 COW 才临时只读的页面；
- 代码段等原本就不允许写的页面。

如果把第二类页面也当成 COW，进程就能通过触发异常把只读代码页变成可写页。因此必须额外记录“这个页面原来可写”。

RISC-V 页表项的第 8、9 位是 RSW（Reserved for Software），硬件不解释它们，操作系统可以自行使用。本实验选择第 8 位：

```c
// RSW 位由软件保留，用第 8 位标记写时拷贝页面。
#define PTE_COW (1L << 8)
```

`1L << 8` 表示把整数 1 左移 8 位，只让第 8 位为 1。

### 4.2 PTE_FLAGS 已经保留软件位

正确 Lab4 骨架原本就是：

```c
#define PTE_FLAGS(pte) ((pte) & 0x3FF)
```

因此不需要修改该宏。Sv39 PTE 的低 10 位都是标志位，`0x3FF` 的二进制低 10 位全为 1。提取标志时会保留两个 RSW 位，`PTE_COW` 在嵌套 fork 时不会丢失。

本实验常用的 PTE 操作如下：

```c
flags & PTE_W              // 判断写权限是否存在
flags & ~PTE_W             // 清除写权限
flags | PTE_COW            // 设置 COW 标志
flags & ~PTE_COW           // 清除 COW 标志
PTE2PA(*pte)               // 从页表项中取物理页地址
PA2PTE(pa) | flags         // 用物理地址和标志重新组成页表项
```

## 5. 第二步：为物理页增加引用计数

### 5.1 为什么原来的 kfree() 不再正确

原实现默认一个物理页只属于一个映射，所以删除映射时可以立即执行 `kfree()`。COW 后，父进程退出不代表物理页已经无人使用，子进程可能仍映射该页。

因此每个物理页需要满足这个不变量：

```text
refcnt = 当前引用该物理页的有效映射数

kalloc() 新分配             refcnt = 1
fork() 新增共享映射          refcnt += 1
解除映射或替换 COW 页        refcnt -= 1
refcnt 降到 0               才能进入 freelist
```

### 5.2 引用计数数组

在 `kernel/kalloc.c` 中增加：

```c
#define NPAGE ((PHYSTOP - KERNBASE) / PGSIZE)

struct {
  struct spinlock lock;
  struct run *freelist;
  // 每个元素对应一页物理内存，和空闲链表共用一把锁。
  int refcnt[NPAGE];
} kmem;

static int
refindex(void *pa)
{
  return ((uint64)pa - KERNBASE) / PGSIZE;
}
```

- `PHYSTOP - KERNBASE` 是 xv6 管理的物理内存长度。
- 除以 `PGSIZE` 得到物理页数量。
- `refindex()` 把物理地址转换为数组下标。例如相邻的两个物理页地址相差 4096，数组下标也正好相差 1。
- 引用计数和空闲链表共用 `kmem.lock`，避免增加多余的锁结构。

### 5.3 增加和读取引用

新增两个小函数：

```c
void
kaddref(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kaddref");

  acquire(&kmem.lock);
  kmem.refcnt[refindex(pa)]++;
  release(&kmem.lock);
}

int
kgetref(void *pa)
{
  int n;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kgetref");

  acquire(&kmem.lock);
  n = kmem.refcnt[refindex(pa)];
  release(&kmem.lock);
  return n;
}
```

地址检查分别保证：地址按页对齐、没有落入内核镜像、没有超过 xv6 管理的物理内存。`acquire()`/`release()` 是 xv6 自旋锁接口，保证多个 hart 不会同时破坏计数。

### 5.4 修改 kalloc()

修改前（A）：

```c
acquire(&kmem.lock);
r = kmem.freelist;
if(r)
  kmem.freelist = r->next;
release(&kmem.lock);
```

修改后（B）：

```c
acquire(&kmem.lock);
r = kmem.freelist;
if(r){
  kmem.freelist = r->next;
  kmem.refcnt[refindex(r)] = 1;
}
release(&kmem.lock);
```

页面离开空闲链表后，调用者立即拥有一个引用，所以计数从 0 变为 1。

### 5.5 修改 kfree()

修改前（A）：

```c
memset(pa, 1, PGSIZE);
r = (struct run*)pa;

acquire(&kmem.lock);
r->next = kmem.freelist;
kmem.freelist = r;
release(&kmem.lock);
```

修改后（B）：

```c
acquire(&kmem.lock);
if(kmem.refcnt[refindex(pa)] < 1)
  panic("kfree ref");
kmem.refcnt[refindex(pa)]--;
if(kmem.refcnt[refindex(pa)] > 0){
  release(&kmem.lock);
  return;
}

// 最后一个引用消失后，才把页面放回空闲链表。
memset(pa, 1, PGSIZE);
r = (struct run*)pa;
r->next = kmem.freelist;
kmem.freelist = r;
release(&kmem.lock);
```

现在 `kfree()` 更准确的含义是“放弃一个引用”。只有计数变成 0 时，才执行原来的真正释放操作。`memset(pa, 1, PGSIZE)` 用垃圾值覆盖页面，是 xv6 原有的悬空引用检查手段。

### 5.6 处理 freerange() 的初始化

启动时所有静态计数都是 0，但 `freerange()` 需要调用 `kfree()` 把可用内存加入 freelist。新的 `kfree()` 不允许从 0 继续减，因此初始化时先建立一个临时引用：

修改前（A）：

```c
for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
  kfree(p);
```

修改后（B）：

```c
for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
  // kfree() 只释放最后一个引用，初始化时先人为建立一个引用。
  kaddref(p);
  kfree(p);
}
```

于是初始化过程是 `0 -> 1 -> 0 -> freelist`，与正常释放路径完全一致。

## 6. 第三步：把 uvmcopy() 从立即复制改成共享

`fork()` 在 `kernel/proc.c` 中仍然调用：

```c
if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
  freeproc(np);
  release(&np->lock);
  return -1;
}
```

不需要修改 `fork()`，只需改变 `uvmcopy()` 如何继承页面。

### 6.1 原来的立即复制（A）

```c
pa = PTE2PA(*pte);
flags = PTE_FLAGS(*pte);
if((mem = kalloc()) == 0)
  goto err;
memmove(mem, (char*)pa, PGSIZE);
if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
  kfree(mem);
  goto err;
}
```

每处理一页都调用 `kalloc()` 和 `memmove()`，这正是大进程 fork 失败的原因。

### 6.2 改成 COW 共享（B）

```c
pa = PTE2PA(*pte);
flags = PTE_FLAGS(*pte);

// 只有原本可写的页面才是 COW；代码段等只读页继续保持只读。
if(flags & PTE_W){
  flags = (flags & ~PTE_W) | PTE_COW;
  *pte = PA2PTE(pa) | flags;
}

// 子进程映射同一物理页，并记录新增的共享引用。
if(mappages(new, i, PGSIZE, pa, flags) != 0)
  goto err;
kaddref((void*)pa);
```

逐行理解：

1. `PTE2PA(*pte)` 取得父进程页面当前指向的物理地址。
2. 只有带 `PTE_W` 的页面才清除写位并添加 `PTE_COW`。
3. `*pte = ...` 修改父进程页表项；否则父进程仍能绕过 COW 直接写共享页。
4. `mappages(new, ..., pa, flags)` 让子进程映射相同的 `pa`，没有分配数据页。
5. 子映射成功后调用 `kaddref()`，物理页引用数加 1。

只读代码页也可以共享，但不设置 `PTE_COW`。进程尝试写代码页时，`cowalloc()` 会拒绝修复，进程仍会被杀死。

### 6.3 为什么要刷新 TLB

函数成功和失败返回前都增加：

```c
sfence_vma();
```

TLB 是 CPU 对页表翻译结果的缓存。虽然内存中的父进程 PTE 已清除 `PTE_W`，CPU 仍可能缓存着旧的“可写”结果。`sfence_vma()` 让 CPU 丢弃旧翻译，下次访问重新查询页表。

错误回滚调整为：

```c
if(i > 0)
  uvmunmap(new, 0, i, 1);
sfence_vma();
return -1;
```

`uvmunmap(..., 1)` 会对已经映射到子进程的页面调用 `kfree()`。由于 `kfree()` 现在按引用计数工作，它只是撤销刚才增加的引用，不会错误释放父进程仍在使用的页面。`i > 0` 避免在第一项映射就失败时传入长度 0。

## 7. 第四步：集中实现 cowalloc()

用户态写异常和内核 `copyout()` 最终要做相同的事情，因此在 `kernel/vm.c` 中只实现一次：

```c
int
cowalloc(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;
  uint flags;
  char *mem;

  va = PGROUNDDOWN(va);
  if(va >= MAXVA || (pte = walk(pagetable, va, 0)) == 0)
    return -1;
  if((*pte & (PTE_V | PTE_U | PTE_COW)) != (PTE_V | PTE_U | PTE_COW))
    return -1;

  pa = PTE2PA(*pte);
  flags = PTE_FLAGS(*pte);

  // 已经没有其他共享者时，无需复制，只恢复写权限即可。
  if(kgetref((void*)pa) == 1){
    *pte = PA2PTE(pa) | ((flags | PTE_W) & ~PTE_COW);
    sfence_vma();
    return 0;
  }

  if((mem = kalloc()) == 0)
    return -1;
  memmove(mem, (char*)pa, PGSIZE);
  *pte = PA2PTE(mem) | ((flags | PTE_W) & ~PTE_COW);
  sfence_vma();
  kfree((void*)pa);
  return 0;
}
```

### 7.1 地址和 PTE 检查

```c
va = PGROUNDDOWN(va);
```

`r_stval()` 给出实际出错地址，可能位于页中间。页表以整页为单位工作，`PGROUNDDOWN()` 把地址向下对齐到页面起始位置。

```c
if((*pte & (PTE_V | PTE_U | PTE_COW)) != (PTE_V | PTE_U | PTE_COW))
  return -1;
```

这里要求三个标志同时存在：

- `PTE_V`：映射有效；
- `PTE_U`：用户可以访问；
- `PTE_COW`：确实是 COW 页面。

因此普通非法地址、栈保护页和真正的只读代码页都不会被错误修复。

### 7.2 只有一个引用时的快速路径

如果其他共享进程已经退出，引用数可能只剩 1。此时当前进程已经独占物理页，不需要再次分配和复制：

```c
*pte = PA2PTE(pa) | ((flags | PTE_W) & ~PTE_COW);
```

它同时完成两件事：设置 `PTE_W`，清除 `PTE_COW`。

### 7.3 仍被共享时的复制路径

```c
mem = kalloc();                         // 新页的引用数为 1
memmove(mem, (char*)pa, PGSIZE);        // 复制旧页全部 4096 字节
*pte = PA2PTE(mem) |
       ((flags | PTE_W) & ~PTE_COW);   // 当前页表切换到新页
sfence_vma();                           // 清除旧的只读翻译
kfree((void*)pa);                       // 当前映射不再引用旧页
```

如果 `kalloc()` 失败，函数返回 `-1`，不会改动原映射。调用者会终止进程或让系统调用失败。

## 8. 第五步：处理用户态写页面异常

实验文档把 `scause == 15` 注释为 load page fault，这是一个笔误。RISC-V 中常见的同步页异常为：

| `scause` | 含义 |
| --- | --- |
| 12 | instruction page fault，取指异常 |
| 13 | load page fault，读异常 |
| 15 | store/AMO page fault，写异常 |

原来的 `usertrap()` 只认识系统调用和设备中断，其他异常全部杀死进程。

修改前（A）：

```c
if(r_scause() == 8){
  // system call
  ...
} else if((which_dev = devintr()) != 0){
  // ok
} else {
  // unexpected trap
  p->killed = 1;
}
```

修改后（B）：

```c
if(r_scause() == 8){
  // system call
  ...
} else if(r_scause() == 15){
  // 15 表示用户态写页面异常；仅修复合法的 COW 页面。
  uint64 va = r_stval();
  if(va >= p->sz || cowalloc(p->pagetable, va) < 0)
    p->killed = 1;
} else if((which_dev = devintr()) != 0){
  // ok
} else {
  // unexpected trap
  p->killed = 1;
}
```

`r_stval()` 读取发生异常的虚拟地址。`va >= p->sz` 排除进程有效用户内存以外的地址；`cowalloc()` 再检查页表项是否有效、属于用户且带 COW 标志。

修复成功后不增加 `epc`。与系统调用不同，发生异常的写指令还没有执行，返回用户态后应重新执行同一条指令，这一次页面已经可写。

## 9. 第六步：让 copyout() 主动处理 COW

`copyout()` 用于把内核数据写到用户地址，例如管道 `read()` 把读到的数据放进用户缓冲区。

CPU 此时运行在内核页表下，`copyout()` 通过 `walkaddr()` 找到物理地址后直接执行 `memmove()`。这种写入不会以用户态写指令的形式访问用户页，因此不会自动进入 `usertrap()`。

修改前（A）：

```c
while(len > 0){
  va0 = (uint)PGROUNDDOWN(dstva);
  pa0 = walkaddr(pagetable, va0);
  if(pa0 == 0)
    return -1;
  ...
}
```

修改后（B）：

```c
while(len > 0){
  va0 = (uint)PGROUNDDOWN(dstva);
  if(va0 >= MAXVA)
    return -1;
  pte_t *pte = walk(pagetable, va0, 0);
  // 内核写用户空间不会触发用户态页异常，需要主动完成 COW。
  if(pte && (*pte & PTE_COW) && cowalloc(pagetable, va0) < 0)
    return -1;
  pa0 = walkaddr(pagetable, va0);
  if(pa0 == 0)
    return -1;
  ...
}
```

`copyout()` 可能跨越多个页面，所以检查位于 `while` 循环内部，每进入一个新页面都重新判断。`va0 >= MAXVA` 必须在直接调用 `walk()` 前返回错误，否则 `pgbug` 传入恶意超大指针时会让内核 panic。若当前页不是 COW，保持原流程；若是 COW，先调用同一个 `cowalloc()` 再获得物理地址。

## 10. 第七步：补充跨文件声明

`kaddref()`、`kgetref()` 定义在 `kalloc.c`，调用点在 `vm.c`；`cowalloc()` 定义在 `vm.c`，调用点在 `trap.c`。因此在 `kernel/defs.h` 中增加：

```c
void            kaddref(void *);
int             kgetref(void *);
int             cowalloc(pagetable_t, uint64);
```

`defs.h` 是 xv6 内核集中存放函数原型的头文件。原型让编译器在跨文件调用时检查参数和返回值类型。

## 11. 一次 COW 写入的完整过程

假设父进程的虚拟页 `VA 0x4000` 映射物理页 `PA X`，初始状态为：

```text
父 PTE -> PA X，PTE_W=1，refcnt[X]=1
```

执行 `fork()` 后：

```text
父 PTE -> PA X，PTE_W=0，PTE_COW=1
子 PTE -> PA X，PTE_W=0，PTE_COW=1
refcnt[X]=2
```

子进程写 `0x4000`：

1. CPU 发现子 PTE 不可写，产生 `scause=15`。
2. `usertrap()` 从 `stval` 得到 `0x4000`。
3. `cowalloc()` 分配物理页 `PA Y`，`refcnt[Y]=1`。
4. 将 `PA X` 的内容复制到 `PA Y`。
5. 子 PTE 改为 `PA Y`，设置 W，清除 COW。
6. 子进程放弃对 `PA X` 的引用，`refcnt[X]` 从 2 变为 1。

结果为：

```text
父 PTE -> PA X，PTE_W=0，PTE_COW=1，refcnt[X]=1
子 PTE -> PA Y，PTE_W=1，PTE_COW=0，refcnt[Y]=1
```

如果父进程随后写同一虚拟页，`cowalloc()` 看到 `PA X` 的引用数已经是 1，直接恢复父 PTE 的写权限，不再进行一次无意义复制。

## 12. 测试程序分别验证了什么

正确 Lab4 骨架提供 `user/cowtest.c`，Makefile 生成的命令也是 `cowtest`。

`user/cowtest.c` 包含三类测试：

- `simpletest()`：申请超过物理内存一半的空间再 fork。它验证 fork 阶段确实没有复制全部物理页，并连续执行两次检查页面能否回收。
- `threetest()`：三个进程共享并分别写页面，验证引用计数、内容隔离和退出释放；连续执行三次。
- `filetest()`：通过管道 `read()` 让内核写入子进程用户缓冲区，验证 `copyout()` 会主动拆分 COW 页面，且不会覆盖父进程内容。

## 13. 构建与验证结果

从干净状态构建：

```sh
make clean
make qemu
```

启动结果：

```text
xv6 kernel is booting
virtio disk init 0
hart 1 starting
hart 2 starting
init: starting sh
$
```

执行 COW 专项测试：

```text
$ cowtest
simple: ok
simple: ok
three: ok
three: ok
three: ok
file: ok
ALL COW TESTS PASSED
```

仓库原装 `grade-lab-cow` 对 `simple`、`three`、`file` 的检查均为 `OK`。本机运行完整 `usertests` 时，各测试一直执行到最后的 `bigdir`，但 QEMU 在评分器固定的 150 秒上限到达时被终止；这是本机模拟速度导致的超时，不是测试断言或内核 panic。学校一键评测最终通过。

调试过程中还修复了 `pgbug` 暴露的问题：`copyout()` 在直接调用 `walk()` 前必须先检查 `va0 >= MAXVA`，否则恶意超大用户地址会使内核 panic。

构建中的 RWX load segment 警告来自旧版教学内核的链接布局，是仓库说明中记录的已知警告，不影响启动和测试结果。

## 14. 最终应记住的四个要点

1. COW 并不是“不复制”，而是把复制推迟到第一次写。
2. 只清除 `PTE_W` 不够，还要用 `PTE_COW` 区分“临时只读”和“原本只读”。
3. 共享映射必须配合物理页引用计数，否则一个进程退出会释放其他进程仍在使用的页面。
4. 用户写入由页面异常处理，内核 `copyout()` 不会产生同样的用户异常，必须主动调用 COW 处理函数。

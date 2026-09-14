本实验为fork调用实现内存的写时拷贝。当系统执行fork调用时，如果将父进程的所有用户态内存拷贝到子进程空间，不但费时，也有可能造成内存消耗过多。本实验在要求在内核中实现内存管理的写时拷贝，即fork时只增加对父进程用户态内存的引用，只有当对内存写时，才进行拷贝。本实验自带的cowtest程序首先分配大量内存，然后进行fork调用。如果不进行写时拷贝(COW)优化处理，则执行cowtest不会通过。当实验者在内存管理系统中实现了写时拷贝后，cowtest测试程序可以通过。

## 【实验步骤】写时拷贝的总体思路

fork调用写时拷贝的目的是延迟分配与拷贝子进程的物理内存页直到确实需要拷贝的时候。

具有COW功能的fork调用在fork时仅为子进程创建一个页表，此页表中用户态内存的PTE项指向父进程的物理内存。同时将父进程和子进程的用户态PTE项都标记为不可写。

当父进程/子进程试图写其中的COW页时，CPU会触发一个页错误。内核的页错误处理机制检测到这个问题，为引发错误的进程分配物理内存，将原页面的内容拷贝到新的页面，并且将相关PTE指向这个新的页，此时将PTE设置为可写。当页错误处理机制返回时，用户态进程就可以对它的拷贝进行写了。

具有COW功能的fork的让实现用户态内存的物理页释放机制变得复杂。一个物理页可能被多个进程的页表所引用，只有当最后一个引用消失时，才能真正释放该页。


##  【实验步骤】初步尝试执行cowtest

在实现COW前，先尝试执行cowtest测试命令:

1. 启动qemu

root@cg:~/xv6-riscv#make qemu
xv6 kernel is booting

virtio disk init 0
hart 1 starting
hart 2 starting
init: starting sh
2. 执行cowtest

$ cowtest
simple: fork() failed


失败的原因：

cowtest中，父进程首先分配大量内存，接着执行fork调用克隆子进程，因为系统物理内存不够了，最后导致失败

## 【实验步骤】调整fork时用户态内存拷贝机制

本步骤是对fork调用进行写时拷贝调整的第一步。

fork的实现通过调用函数uvmcopy来实现子进程对父进程用户态内存空间的继承 (proc.c):

273fork(void)
274{
275  int i, pid;
276  struct proc *np;
277  struct proc *p = myproc();
278
279  // Allocate process.
280  if((np = allocproc()) == 0){
281    return -1;
282  }
283
284  // Copy user memory from parent to child.
285  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
286    freeproc(np);
287    release(&np->lock);
288    return -1;
289  }
本步骤主要修改uvmcopy (vm.c)把用户态内存的拷贝，改为共享，uvmcopy原有的实现如下：

300int
301uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
302{
303  pte_t *pte;
304  uint64 pa, i;
305  uint flags;
306  char *mem;
307
308  for(i = 0; i < sz; i += PGSIZE){
309    if((pte = walk(old, i, 0)) == 0)
310      panic("uvmcopy: pte should exist");
311    if((*pte & PTE_V) == 0)
312      panic("uvmcopy: page not present");
313    pa = PTE2PA(*pte);
314    flags = PTE_FLAGS(*pte);
315    if((mem = kalloc()) == 0)
316      goto err;
317    memmove(mem, (char*)pa, PGSIZE);
318    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
319      kfree(mem);
320      goto err;
321    }
322  }
323  return 0;
324
325 err:
326  uvmunmap(new, 0, i / PGSIZE, 1);
327  return -1;
328}

其中第309行至321行挨个对内存页进行拷贝，并将新页映射到新的进程空间

本步骤要求实验者不分配新的物理页，而只是将父/子PTE去掉可写位，增加物理页面的引用计数，并将父/子PTE映射到一物理地址。同时标记父子进程的PTE项为COW（写时拷贝），可以使用RISC-V PTE的RSW（reserved for software）位来标记。

## 【实验步骤】增加物理页面的引用计数功能

写时拷贝的一个技术基础是物理页面的引用计数。当fork共享用户态内存时，需要将对应的物理页的引用计数增加1。

当页错误捕获机制利用写时拷贝分配和复制新的物理页时，需要将对应的物理页减1。当需要释放物理页时，如果此时物理页的引用计数为1，则立即将该页释放，否则引用计数减1。

可以利用哈系表的方式来维护物理页的引用计数。将每个物理页映射到哈系表的一项，并在此项中维护引用计数。

## 【实验步骤】实现页错误捕获/修正机制

修改trap.c的usertrap()函数来识别一个页错误。当一个COW页发生了一个页错误后，使用kalloc()分配一个新的物理页，将旧页的内容拷贝到新页中，并且在PTE中安装新的物理页，同时将可写位置位。

例如可在trap.c的usertrap()函数中按照如下起始代码开始进行修改：

70  } else if(r_scause() == 15) {
71    // 15: load page fault
72    uint64 fault_addr = r_stval();
73    uint64 vpage_head = PGROUNDDOWN(fault_addr);
74
75    pte_t *pte;
76    if((pte = walk(p->pagetable, vpage_head, 0)) == 0) {

77      printf("usertrap(): page not found\n");
78      p->killed = 1;
79      goto end;
80    }
81   分配新页，拷贝，重新安装PTE，可写位置位等

## 【实验步骤】在copyout函数中处理COW

参照前一个步骤：实现页错误捕获/修正机制，对copyout函数中涉及的COW页进行类似处理。
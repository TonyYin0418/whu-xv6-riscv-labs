# Lab7：锁竞争优化

## 目标

本实验针对多核 xv6 的两处热点锁：页分配器的 `kmem` 与块缓存的 `bcache`。
目标不是删除同步，而是在保持同一资源正确性的前提下，让不同 CPU 操作不同资源时不再争抢同一把全局锁。

## 每 CPU 页分配器

原实现只有一个 `freelist`，所有 `kalloc()` 和 `kfree()` 都获取 `kmem.lock`。新实现将它拆成
`NCPU` 个 `kmem_cpu`：每个 CPU 有自己的空闲链表和名称均为 `kmem` 的锁。

释放页时在 `push_off()` 与 `pop_off()` 之间读取 `cpuid()`，把页加入当前 CPU 链表；关闭中断
保证取得 CPU 编号到操作对应链表期间不会发生迁移。分配优先从本地链表取页，仅在本地耗尽时逐个
检查其他 CPU 的链表并偷取一页。正常高频路径没有跨 CPU 锁竞争，偶发偷取则不会同时持有两把
分配器锁，避免锁顺序死锁。

## 哈希块缓存

全局 LRU 链表使每一次 `bread()`、`brelse()` 都竞争 `bcache.lock`。新实现使用 13 个桶：

```text
bucket = blockno % 13
```

每个桶有独立的 `bcache.bucket` 锁和单链表。命中时只锁目标桶，增加 `refcnt` 后取得该 buffer
的 sleeplock；释放和 pin/unpin 同样只锁所属桶。因此访问不同块且散列不同的进程可并行执行。

未命中时使用短暂的 `bcache.evict` 锁串行化回收：先重新检查目标桶，避免两个 CPU 为同一块创建
副本；随后从任一桶摘下 `refcnt == 0` 的 buffer，更新其设备号和块号，再插入目标桶。桶中移动在
各自桶锁保护下完成，且不会同时持有多个桶锁，从而避免循环等待。该设计不维护 LRU 时间，但保证
每个 `(dev, blockno)` 同时只在一个桶中存在。

## 验证

在 3 CPU QEMU 上执行：

```text
$ kalloctest
test0 OK
test1 OK

$ bcachetest
test0: OK
test1 OK

$ usertests
ALL TESTS PASSED
```

实际结果中三把 `kmem` 锁及 `bcache.evict` / 各 `bcache.bucket` 锁的 test-and-set 均为 0；
说明压力测试的目标锁竞争已消除。`virtio_disk` 与 `proc` 的竞争不属于本实验要求。

# Lab10：用户线程与闹钟

`uthread` 在每个线程对象中保存 `ra`、`sp` 和 RISC-V 的 12 个被调用者保存寄存器。
创建线程时把 `ra` 设为线程函数、`sp` 设为其独立栈顶；`thread_switch` 将旧线程
寄存器存入第一个参数并从第二个参数恢复，因此第一次恢复会直接返回到线程函数。

闹钟在 `proc` 中记录周期、累计滴答、处理函数地址、活动标志和完整 trapframe 备份。
`usertrap` 仅在时钟中断到达、周期到期且处理函数未运行时备份现场并把 `epc` 改为
处理函数；`sigreturn` 还原 trapframe 并清除活动标志，从被中断指令继续执行，避免重入。
系统调用号、用户声明和汇编桩已一起接入，并将 `alarmtest` 纳入镜像。

验证：执行 `make clean && make` 后，在 QEMU 中运行 `uthread`，三个线程均从 0
运行到 99 并退出；运行 `alarmtest`，`test0 passed` 和 `test1 passed` 均出现。

# Lab8：E1000 与 UDP 套接字

## 目标与数据路径

本实验把一帧 UDP 数据在 xv6 中走通：用户程序 `write()` 进入套接字，
`sockwrite()` 复制负载并调用 `net_tx_udp()`；协议层依次补上 UDP、IP、
以太网头，E1000 发送描述符把该帧交给设备。反方向由 E1000 中断回收接收
描述符，`net_rx()` 解析以太网/IP/UDP 头，最后按四元组将负载放进目标套接字
的接收队列，阻塞的 `read()` 被唤醒。

## E1000 描述符环

`kernel/e1000.c` 使用初始化代码已有的 16 项 TX/RX 环。

- 发送时读取 `E1000_TDT` 指向的槽位。只有 `E1000_TXD_STAT_DD` 已置位才
  可复用；释放该槽先前保存的 mbuf，填写 `addr`、`length` 与
  `E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS`，保存新 mbuf，最后将 TDT 前移一格。
  若槽位仍由设备使用则返回 `-1`，调用方负责释放本次 mbuf。
- 接收时从 `(E1000_RDT + 1) % RX_RING_SIZE` 开始，只要 DD 置位就用描述符
  给出的长度扩展原 mbuf，并立即替换为新分配的空 mbuf。清空状态、推进 RDT
  后设备才能再次使用该槽位。
- 描述符数组和寄存器访问由 `e1000_lock` 保护。接收路径在描述符重新交还给
  设备后释放该锁才调用 `net_rx()`：协议层处理 ARP 时可能发送回复，若持锁
  调用会递归进入 `e1000_transmit()`。

这一区分保证了超过 16 个连续报文时，已经消费的 RX 槽会不断被补充并交回
设备，而不会在环绕时停住。

## 把套接字接入文件层

`struct file` 已有 `FD_SOCK` 和 `sock` 成员。本实验在 `kernel/file.c` 的三个
分支接入对应方法：

| 文件操作 | 套接字方法 | 结果 |
| --- | --- | --- |
| 最后一次 `close` | `sockclose` | 从全局链表摘除，释放排队 mbuf 与 socket |
| `read` | `sockread` | 队列为空时以 socket 为 channel 睡眠；取出一个完整 UDP 负载并 `copyout` |
| `write` | `sockwrite` | 分配带头部余量的 mbuf，`copyin` 后调用 `net_tx_udp` |

`sockread()` 的空队列检查使用 `while` 包住 `sleep()`，因此即使发生虚假唤醒
或另一读取者抢先取走数据，也会继续等待。写入长度限制为 mbuf 可容纳的数据，
避免在预留头部后越过缓冲区。

## UDP 分流与并发

`sockalloc()` 创建的对象以 `(远端 IP, 本地端口, 远端端口)` 组成唯一匹配项。
`sockrecvudp()` 在全局 `socktbl` 锁下查找该项，取得目标 socket 的队列锁后入队
并 `wakeup(si)`；没有匹配项则释放 mbuf。关闭时先在全局链表中移除对象，再在
该 socket 锁下清空队列。这一锁顺序阻止接收路径向已经摘除的 socket 继续投递。

## 验证方式

宿主机先启动回显服务，再在另一个终端启动 xv6：

```sh
make server
make qemu
```

在 xv6 shell 运行：

```sh
nettests
```

本地验证中，单 ping、100 次单进程 ping、10 个并发进程 ping 均输出 `OK`，
宿主回显服务收到了全部报文，说明 TX/RX 环已多次跨过 16 项边界。DNS 请求也
成功收到并解析响应；但当前 QEMU user-network 返回的 `pdos.csail.mit.edu`
地址为 `198.18.0.176`，而旧版 `nettests` 将历史地址 `128.52.129.126` 写死，
因此它在地址比较处报告 `wrong ip address`，不是驱动或套接字传输失败。课程
评测环境的 DNS 返回值与本机不同，以课程平台结果为准。

完成后可运行 `./grade-lab-net`；该脚本会自动启动课程提供的 Python 2 回显服务。

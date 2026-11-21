# 里程碑 4e · 阶段 4 验收全部完成（Trial745）

2026-11-21。xaga，`boot_a`，恢复原厂后 64 MiB 逐字节相同，SELinux Enforcing。

## 卸载成了

```
xnusys-blk: unmount: mp=0xfffffff80d226000 from a lookup of /redwood/lib
xnusys-blk: unmount: calling dounmount
xnusys-blk: unmount /redwood/lib -> 0
xnusys-unmount: chdir / -> 0
xnusys-unmount: unmount -> 0
xnusys-unmount: after unmount, open the file -> -1
xnusys-unmount: PASS
```

卸载之后那个文件打不开了——这是"真的卸掉了"和"调用返回了 0"的区别。

## 根因是一个参数

`dounmount(mp, flags, withref, ctx)` 的第三个参数 `withref` 我传了 **1**。它的含义是"调用者持有
一个要交回的引用"，`dounmount` 会在早期 `mount_drop(mp, 1)` 把它还掉。我并没有持有那个引用，
于是挂载的引用计数被减到不该有的值，函数卡在里面再也不返回。

改成 **0** 立刻通了。

这一处之前试过三种解释都没对：不是超时（150→420 现象不变，时间戳只到 166 秒）、不是
`dounmount` 主动返回、不是 panic。真正的错在参数语义上，而那三种推测都在函数外面找原因。

## 阶段 4 验收项对照

| 路线图要求 | 状态 | 证据 |
|---|---|---|
| 只读挂载 | ✅ | `mount /dev/xnusys on /redwood/lib -> 0` |
| `stat` | ✅ | `ino=2`（挂载点）、`ino=61314`（目标文件） |
| 目录枚举 | ✅ | 12 条：`.` `..` `usr` `bin` `sbin` `etc` `System` `var` `Library` `private` `redwood` `dev` |
| `open` / `read` | ✅ | 5230 字节，`head=bplist00` |
| 文件哈希与阶段 3 相同 | ✅ | `fnv1a=f28fe48a`，与主机独立计算一致 |
| 写打开被拒 | ✅ | 三条路径各自 -1（块设备、挂载卷上的文件、只读描述符） |
| 卸载 | ✅ | `unmount -> 0`，之后文件不可打开 |
| 读错误后释放请求并返回原厂链路 | ✅ | 每轮都以 64 MiB 读回逐字节相同收尾；`return-gate ... state=2 allowed=1` |

## 这一阶段修掉的错误（累计）

1. 设备节点建在错误的命名空间（块 major 上建字符节点）
2. ioctl 里重复 `copyin`（`VOP_IOCTL` 已经拷过）
3. 挂载打开程序自己持有的 vnode（`EBUSY`）
4. `getdirentries64` 的第四参数是出参，传 0 导致 `EFAULT`
5. `dounmount` 的 `withref` 传错（本轮）

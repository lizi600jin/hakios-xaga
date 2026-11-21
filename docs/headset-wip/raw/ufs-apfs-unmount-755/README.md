# 里程碑 6f · 卸载三项验收（Trial755）

2026-11-21。设备已恢复原厂，`boot_a` 逐字节相同。

## 结果：三项全过

```
xnusys-unmount2: unmount while a file is open -> -1
xnusys-unmount2: the held file still reads -> 64
xnusys-unmount2: unmount with nothing open -> 0
xnusys-unmount2: after the unmount, the volume's file -> -1
xnusys-unmount2: remount -> 0
xnusys-unmount2: after remount, read 7814 bytes fnv=30630d20
xnusys-unmount2: PASS
```

| 场景 | 期望 | 实测 |
|---|---|---|
| 文件占用中卸载 | 拒绝 | `-1`（EBUSY），且持有句柄仍能读 64 字节 |
| 无占用卸载 | 成功 | `0`，内部 flush 先完成（`polls=268`） |
| 卸载后访问原文件 | 打不开 | `-1` |
| 重挂载 | 成功 | `0` |
| 重挂载后读原文件 | 内容不变 | 7814 字节，`fnv=30630d20`（主机同值） |

## 卸载自己触发 flush，不靠用户态预先调用

这是 codex 明确要求的：**不能让程序先手动 flush 一次就算过**。改动在
`hakios.xnusys_blk.c` 的卸载分支：

```c
if (!hakios_mt6895_ufs_zone_flush()) {
    printf("xnusys-blk: unmount %s: the flush failed, staying mounted\n", at);
    return EIO;      /* 挂载保持不变 */
}
...
error = dounmount(mp, 0, 0, ctx);   /* 不再是 MNT_FORCE */
```

两件事一起改：

- **flush 失败就不许卸**——宁可挂载留着，也不让一个丢了写的卷走掉
- **去掉 `MNT_FORCE`**——强制卸载会把仍有使用者的卷赶走，那样"忙碌时 EBUSY"这条
  就会因为错误的原因通过

## codex 纠正了我两处

**一、`_IO` 本身是可用的。** 我之前说"`_IO` 到不了内核"，是因为自己按 `0x00007805` 发，
丢了 `IOC_VOID` 位。正确编码是 `0x20007805`。

**二、`_IOW` 回传不了结果，要 `_IOWR`。** 这一条实测印证了：改 `_IOWR` 之后
`synchronize cache -> rc=0 done=1`（之前 `done=0`）。

`polls` 只作为耗时旁证，成功判据仍看完整命令响应。

## 一个自己造成的问题

上一轮改了 `_IOWR` 却没重编用户程序，结果 `rc=-1`、内核一条 flush 日志都没有——
**改了源码不等于改了镜像**。这和之前 `-DXNU_RETURN_AFTER` 那次是同一类错误。

## 顺带解决的

`xnusys_zonewrite` 这个旧测试在 `rc` 里，它跑完内核就交还控制权，把后面的程序全挡住了。
把 `rc` 精简到本轮需要的五个程序后，`unmount2` 才跑起来。

## 仍未做

- 注入明确的 flush 失败来验证"卸载拒绝"那条分支（代码在，但没有真实触发过）
- 突然断电的持久性（按 codex 判断不做）

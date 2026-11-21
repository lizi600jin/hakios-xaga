# 里程碑 4d · 写拒绝取证完成，卸载未成（Trial744）

2026-11-21。xaga，`boot_a`，恢复原厂后 64 MiB 逐字节相同。

## 成立的部分：写拒绝

三条路全部被拒，每一条都单独试过：

```
open /dev/xnusys for write                        -> -1
open a file on the mounted volume for write       -> -1
write through a read-only descriptor              -> -1
```

第一条来自块设备的 `d_open`（`EROFS`），第二条来自 APFS 的只读挂载，第三条来自只读描述符本身。
这是阶段 4 验收里"写打开被拒绝"那一项。

## 没成立的部分：卸载

`dounmount` 加上去之后，程序走到 `control node -> 3`（控制节点拿到了），然后内核就交还控制权，
卸载的结果没有打印出来。日志末尾是 `loader_return_after expired` 横幅与
`psci: cpu 2 ends the system`。

查过、排除了的：

- **不是超时。** 把 `loader_return_after` 从 150 提到 420，现象不变；内核时间戳只到 166 秒。
- **不是 `dounmount` 主动返回。** `apfs_vfsops.c` 与 `vfs_syscalls.c` 里都没有调 `loader_return`。
- **不是 panic。** 日志里没有 panic 记录，只有那条横幅。

剩下最可能的是 `dounmount` 在等一个它等不到的东西——挂载点上有引用没放。而横幅是交还时补写
的，不是实时打的，所以"expired"这四个字不能当作超时的证据。

## 一处诊断上的教训

那次把 `loader_return_after` 从 150 改到 420 时，第一次改错了：`json` 里那个字符串是
`-DXNU_RETURN_AFTER=\"150\"`，替换 `\"150\"` 没命中（实际存的是 `"150"`），构建照样成功、
`strings` 一看还是 150。第二次直接替换 `150` → `420` 才生效。

## 阶段 4 现在的账

| 验收项 | 状态 |
|---|---|
| 只读挂载 | ✅ |
| `stat` | ✅ |
| 目录枚举 | ✅ |
| `open` / `read` | ✅ |
| 文件哈希与阶段 3 相同 | ✅ |
| 写打开被拒 | ✅（本轮） |
| 卸载 | ❌ 未成 |
| 读错误后释放请求并返回原厂链路 | 未单独取证 |

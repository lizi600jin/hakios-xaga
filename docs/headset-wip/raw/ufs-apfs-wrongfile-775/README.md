# 里程碑 6l · 改错了文件：ufs=2 从未生效（Trial775）

2026-11-22。设备已恢复原厂，`boot_a` 逐字节相同。

## 一、这一轮做了什么

四项配置第一次全部到位：

```
✅ 内核含 hakios.ufs.c（standard，ufs.o 160 KB）
✅ hakios.stubs.c 的空实现关闭
✅ rc 里只有 xnusys_ufsdev
✅ boot-arg 带 ufs=2（loader.bin 里验证过）
```

刷入后设备**正常回安卓**（`state=0` → `state=2 allowed=1`），程序也执行了：

```
xnusys-ufs: opening /dev/ufs_xnusys
xnusys-ufs: no such device (-1)
```

## 二、为什么设备不存在

追到 `hakios_ufs_init()`：

```c
if (!PE_parse_boot_argn("ufs_shared_pa", &pa, sizeof(pa)) || pa == 0) {
    return;    /* the loader was not asked for ufs=2 */
}
```

HLOG 里搜 `ufs_shared_pa`：**0 次**。`ufs=2` 出现了 1 次，但共享页地址从没被注入。

## 三、真正的根因：我改的文件不参与构建

先在 `xnuloader/loader.c` 里找到了完整的协议（`struct loader_block` v3、
`block_start()`、`block_poll()`），以为只差一个开关。

**实际上编译的是 `mtk/loader/mtkloader.c`。**

```
loader-build.json 的编译单元:
  [1] ['mtk/loader/mtkloader.c']
```

而 `mtkloader.c` 里搜 `loader_block` / `RDWBLOCK` / `ufs_shared_pa`：**零匹配**。

我给 `xnuloader/loader.c` 加的诊断 `out("ufs: block_start() requested...")`
在源码里存在，但 `strings loader.bin` 里**找不到**——那一刻就该发现文件错了。

## 四、两个 loader 是什么关系

| 文件 | 角色 |
|---|---|
| `xnuloader/loader.c` | 完整的 UEFI loader，有自己的块设备共享页协议 |
| `mtk/loader/mtkloader.c` | **当前在用的**，直接操作 UFS 控制器，有 `ufs_read()` |

两者同名不同物。`ufs=2` 只被前者认识。

## 五、我判断错了两次

1. **"loader 侧都写好了，只差一个开关"** —— 没确认哪些代码参与构建
2. **"改对了文件，只是没编译进去"** —— 诊断字符串缺席时，我以为要清 .d 缓存

两次都是同一个错误：**没确认"我改的文件是不是被编译的那个"**。

这和之前"改了源码不等于改了镜像"是同一类，只是层次更深——那次是产物新旧，
这次是**根本不在构建里**。

## 六、移植工作量

要给 `mtkloader.c` 加共享页通道，需要：

1. 定义 `struct loader_block`（从 `xnuloader/loader.c` 抄，~70 行）
2. 分配一页共享内存
3. 在 `boot_note` 之后注入 `ufs_shared_pa=`
4. 在现有主循环里加 `block_poll()`
5. 实现读写分支（可复用已有的 `ufs_read`）

**半轮到一轮，不是加一个 boot-arg。**

## 七、这一轮的真实价值

不是"又失败了"，而是**搞清楚了 ufs=2 为什么无效**。这个信息之前不存在：
我一直以为这条路"几乎免费"，实际上是"需要移植"。

## 八、设备与数据

- `boot_a` 恢复原厂并逐字节验证
- 闪存上 HFS+ 卷 10 块、三块实验数据完好
- 本轮改动未提交（下一轮移植时一并处理）

# 原厂 PTE 取样复现材料

`android-real-pte.json` 是第一轮无事件样本，不是排除证据。
`android-alloc-real-pte.json/.trace` 是第二轮受控分配，64 条 native 返回值，cleanup_verified=true。
`mali-alloc-sample.c` 仅通过 vendor CSF UAPI 分配自己的缓冲，未提交 GPU job。

所用交叉编译命令（KROOT 指本机的 vendor midgard 目录）：

```sh
aarch64-linux-gnu-gcc -static -O2 -Wall -Wextra -DMALI_USE_CSF=1 \
  -I "$KROOT/include/uapi/gpu/arm/midgard" \
  mali-alloc-sample.c -o mali-alloc-sample
```

已运行 ELF SHA256=`37d69f70502853be078f98d6d840d27dd44e9d4313d3010d91c889cf419a001d`。
脚本要求原厂 boot MD5 和 boot_completed=1，并只安装/删除自己命名的 return probes/private instance。
不解引用探针参数，不采集屏幕内容，不触碰物理 MMIO。native-mgm-source.json 保存参考源码行与指纹。

675–678 每轮先备份并清零精确的专用 logbook 窗口后运行。完整原始 logbook 与清理前备份留在本地；此处记录 SHA256、参数计数与当前轮提取。

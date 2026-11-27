# GPU 攻坚交接快照（2026-11-27 收工）

## 一、团队与分工

| 角色 | handle | 职责 |
|---|---|---|
| Neon | term_654ddc1a-8bed-4cb8-9f36-c74c5905363f（Gemini 4 Neon high） | 规划、架构契约、终审 |
| Carbon | term_69c87505-7ec5-401c-80b9-07175af018d0（Gemini 4 Carbon high） | 写代码（M3 主责） |
| Astra | term_c52f26f4-ea08-4fe8-983a-49f1197ee2af（GPT-6.2-Astra xhigh） | 深水区协防/二审 |
| DSH | 本会话 | 编译、刷机、采数据、验证、审计核查 |

### 后续分工原则

- 棘手且 Carbon 强项的部分才启用 Carbon
- 其余情况 Neon 来
- 审计核查 Neon -> DSH
- 一次只和一方发消息

## 二、GPU 进度总表

| 阶段 | 状态 | 镜像 | 证据 |
|---|---|---|---|
| M0 识别 | OK | — | gpu_id 0xa8670004 (csf 0x40a0412) |
| M1 固件 | OK | boot-935 | M1 done |
| M2a 队列 | OK | — | group 0 stream 0 running |
| M2b 指令 | OK | boot-935 | sync 0x10000000cafebabe |
| M2c AS1 MMU | OK | boot-991 | 三个 PA 全 6.49 GiB，PASS |
| Task 2 shader | OK | boot-992 | 6 核上电，matched 16/16，PASS |
| M3 服务 | 半 | boot-993 | 服务启动，但测试找不到 |
| M4a Mesa 集成 | 未 | — | — |
| M4b 零拷贝 | 未 | — | — |
| M5 上线 | 未 | — | — |

## 三、M3 的下一步（确定性工作）

### 1. 修 panthor_test 一行

```c
/* userland/src/panthor_test.c:382 */
- service = IOServiceGetMatchingService(0, IOServiceMatching("HakiosGPU"));
+ service = IOServiceGetMatchingService(0, IOServiceNameMatching("HakiosGPU"));
```

### 2. 补 vmDestroy() 的 5 行（Neon 和 Carbon 都发现）

iokit/Kernel/hakios.mali610.cpp:1230-1236。加 uint32_t fGroupVms[PANTHOR_MAX_CLIENT_GROUPS]，
在 vmDestroy() 里把属于 req->id 的 fGroups[g] 一并 groupDestroy/清零。

### 3. 重编重刷

```bash
cd userland
source ../xnu-env.sh
CC="xnu-clang -arch arm64 -Os -static -nostdlib -ffreestanding -fno-stack-protector -fno-builtin -Wall -Wextra"
LD="-Wl,-e,_start -Wl,-pagezero_size,0x100000000"
$CC -Ilibmini $LD -o obj/panthor_test.new src/panthor_test.c obj/mini.o obj/start.o
python3 ../tools/apfs/mkapfs.py -z --plain fw -o /tmp/root-994.apfs redwood=root
# 打包时 boot-args 保持 mali_gpu=1
```

### 4. 验收清单

```
10/10 轮 compute=16/16
magic 低字 0xdeadbeef
gstate=0x0 / vstate=0x0
panthor_test: PASS (gpu_id=0xa8670004, seq=10, wrote=0xdeadbeef)
0 次 MMU/GPU/CS fault
RETURN < 40s
```

## 四、关键文件位置

```
仓库：/home/lizi/IOS/repo/hakios-mt6895
工作树：/home/lizi/IOS/hakios-mtk
内核树：/home/lizi/IOS/hakios-mtk/logs/ufs-apfs-fstree-root-736/

驱动（三份必须字节一致）：
  kernel-src/bsd/dev/hakios.mali610.c        ea7d3fc1...
  kernel-src/iokit/Kernel/hakios.mali610.cpp a886b0d3...
  kernel-src/iokit/Kernel/hakios.stubs.cpp   a4306124...
  副本：mtk/mt6895/drivers/gpu/

工具：
  userland/src/panthor_test.c     测试程序源码
  userland/obj/panthor_test       已编（50576 B，Mach-O arm64）
  userland/root/bin/panthor_test  在 ramdisk 里（100755）

规划与参考：
  docs/headset-wip/raw/gpu-attack/PLAN.md   五阶段路线 + 验收标准 + 风险矩阵
  docs/headset-wip/raw/gpu-attack/refs/     19 个上游参考

Mesa 25.0.7：
  xnu-deps/build/mesa/mesa-25.0.7/
    src/panfrost/compiler/valhall/asm.py    Valhall 汇编器
    src/panfrost/lib/genxml/v10.xml         G610 描述符定义
    src/panfrost/lib/kmod/panthor_kmod.c    Mesa 侧 panthor UAPI
```

## 五、镜像清单

```
golden/boot-985b-golden.img       UI 基线，sha256 ae41a7ee...
golden/boot-991-m2c-pass.img      M2c，sha256 beb5e96f...
golden/boot-992-compute-pass.img  Task 2，sha256 91ee276c...
boot-993                          M3（服务启动，测试 bug），sha256 ea316c70...
```

## 六、boot-arg 分级（保险丝）

```
默认            -> boot-990 行为（AS0 M2b）
mali_m2c=1      -> M2c（AS1 高位 PA）
mali_m2c=2      -> Task 2（AS1 + shader 执行）
mali_gpu=1      -> M3（HakiosGPU 服务，panthor UAPI）
gl=0 / gl=1     -> M4a 之后控制 backboardd 走软/硬渲染
```

## 七、Valhall 汇编器用法（已实测）

```bash
cd xnu-deps/build/mesa/mesa-25.0.7/src/panfrost/compiler/valhall
python3 asm.py shader.va shader.bin
```

语法：
- 助记符全大写（MOV.i32 不是 mov）
- staging 寄存器用 @ 前缀，排第一位
- 立即数 name:value（offset:0x0）
- modifier 必需（.slot0 / .w0）

## 八、编译与刷机流程

### 编译内核

```bash
R736=/home/lizi/IOS/hakios-mtk/logs/ufs-apfs-fstree-root-736
# 新增 cpp 时必须先删旧 Makefile（否则漏编）
rm -f $R736/kernel-src/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/Makefile
rm -f $R736/kernel-src/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/makedeps.late

XNU_SRC="$R736/kernel-src" bash /home/lizi/IOS/hakios-mtk/tools/make-kernel.sh -j6
# exit 2 = 已知的 kext symbol-set 检查失败，不是编译错误
```

### 打包

```bash
python3 mtk/tools/pack_xaga.py pack --tag NNN \
  --from-manifest <prev.manifest.json> \
  --android-boot backup/boot_a_stock_real.img \
  --loader $R736/build/loader-out/loader.bin \
  --kernel $R736/kernel-src/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837 \
  --ramdisk userland/root.apfs --out-dir $R736/trial736
```

### 刷机

```bash
adb -s GAJZ4HIN99AQ69Y9 reboot bootloader
fastboot -s GAJZ4HIN99AQ69Y9 flash boot_a <img>
fastboot -s GAJZ4HIN99AQ69Y9 reboot
```

### 采集

```bash
sudo ip addr replace 10.0.0.1/24 dev enx020000006801
sudo ip neigh replace 10.0.0.2 lladdr 02:00:00:00:68:02 dev enx020000006801 nud reachable
# 协议：PING / LOG / LS / GET / PUT / CHMOD / EXEC / RETURN
# RETURN 前必须先 PING 确认
```

## 九、已知坑（本轮踩过的）

1. umask 0002 让 plist 变 0664 —— launchd 静默拒载，必须 chmod 644
2. setName 不等于 IOServiceMatching —— 前者改 name，后者匹配 class
3. 新增 cpp 必须删旧 Makefile —— 否则漏编，链接缺符号
4. hakid 死锁 —— 客户端不干净关闭会卡在 accept()，只能长按电源 10s
5. /tmp 重启清空 —— 上传的工具要重新传
6. 触摸冷启动竞态 —— SPI 读到 0xfd/0xfe 时重启即可

## 十、下次开工第一步

```bash
# 1. 确认设备状态
adb devices; fastboot devices

# 2. 修 panthor_test 那一行
# 3. 重编重打重刷 boot-994
# 4. 跑 panthor_test 验收
```

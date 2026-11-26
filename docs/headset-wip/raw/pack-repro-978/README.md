# 可复现打包：同一个 sha256 能重建出来

2026-11-26。工具 `mtk/tools/pack_xaga.py`（483 行，md5 `51327197…`）。

## 做了什么

以前每次刷机的镜像都是手搓的，事后无法说清「当时刷的到底是哪一份」。
这个工具把三件事固定下来：

```
pack      一次产出 payload.bin + boot.img + manifest.json，打印全部输入的 SHA-256
inspect   查看任意已有 boot.img 的 sha256、内嵌 boot-args、各段哈希
verify    对照 manifest 校验 boot.img / payload.bin / 内嵌 boot-args / 各段哈希
```

还有两个防呆参数：

```
--require-arg cpus=8    boot-args 里必须有这个 token，没有就失败
--forbid-arg  bl=1      boot-args 里不能有这个 token，有就失败
```

**第二个是给自己上的锁。** 之前有过两次「忘了删旧 boot-arg」：
`bl=1` 让背光驱动跳过设备节点注册，`i2c=5` 让默认开启变成手动开启。
两次都是刷完才发现。现在打包时就会拦住。

## 验收结果

### 两个基线补 manifest

```
boot-977  3c3fdfb0d728f4a890d9d7bc9bb365a08dce275ff258fdf78b3ebb36fb8687dc  cpus=4
boot-978b 52fe2f5765490b439dac3c624e1fd003775d3932508a5ff3d304ff976e5b64cc  cpus=8
```

`verify` 两个都 PASS。

### 逐字节复现

```
pack --check-repro  →  repro: PASS (byte-for-byte identical)
```

两次打包结果一致。

**但这里有个坑值得记下来。**

## 坑：kernel.ver 里带着 git 提交号

第一次 `--check-repro` 报了 PASS（两次打包确实一致），但产出的 sha256 是
`8808ad14…`，**不是** 978b 的 `52fe2f57…`。

差别只有 `kernel.ver` 一项：

```
978b    2.1.13-hakios-darwin22.5-mt6895-gdb08f63c-dirty
重打包  2.1.13-hakios-darwin22.5-mt6895-g6a51ef21-dirty
```

`gdb08f63c` 是编内核时的提交，`g6a51ef21` 是现在的 HEAD。

**所以「输入文件相同」不等于「产出相同」** —— 版本串是从 git 现算的，
而它被嵌进了镜像。

复现历史镜像要用 `HAKIOS_VERSION_SUFFIX`：

```sh
HAKIOS_VERSION_SUFFIX="-gdb08f63c-dirty" pack_xaga.py pack --tag repro978b2 ...
  sha256: 52fe2f5765490b439dac3c624e1fd003775d3932508a5ff3d304ff976e5b64cc
```

**和 978b 一字不差。**

这个环境变量在 `tools/kernel-version.py:21-22` 的注释里就写了用途：
「replaces the git part (a reproducible build, or a tree that is not a git checkout)」。

**不加这个，回滚到旧镜像就不可能逐字节一致** —— 因为 git 已经往前走了。

## 一个必然的不一致

`--check-repro` 只保证「同一次运行里两次打包一致」，不保证「跨时间一致」。
跨时间一致需要固定三样输入：

1. 三个输入文件（android_boot / loader / kernel）的字节内容
2. ramdisk
3. `HAKIOS_VERSION_SUFFIX`（否则随 git HEAD 变）

manifest 里记了前两样的 SHA-256，第三样要自己记住。

**建议：manifest 里也应该记下当时的 `kernel_version` 字符串**，
这样任何时候都能拿它去重建。现在 manifest 里其实已经记了（`kernel_version` 字段），
所以只要读旧的 manifest 就能知道该用什么后缀。

## 现在的可追溯链

```
manifest.json
  ├── boot.img sha256
  ├── payload.bin sha256
  ├── loader 段 sha256
  ├── Image 段 sha256
  ├── kernel / ramdisk / kernel.ver 各自 sha256
  ├── 完整 boot-args
  └── kernel_version 字符串（重建时用它当 HAKIOS_VERSION_SUFFIX）
```

配上 trial 目录里的真机结果（`i2c: c6:`、`m2b: PASS`、`RETURN 32s` 那些日志），
就形成「镜像 ↔ 真机结果」的可追溯链。

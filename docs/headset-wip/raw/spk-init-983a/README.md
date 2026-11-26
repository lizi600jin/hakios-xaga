# 两颗功放都配好了，一个位不差

2026-11-26，`boot-983a`。第一次写 TFA9873 的寄存器。

## 结果

```
i2c: spk: /dev/hakios-speaker ready (zero boot writes; run 'init' then 'tone'|'left'|'right'|'stop')

i2c: spk: init Left(RCV) (0x34):
  00=4211 02=0628 20=2883 21=c1f0 22=047c 26=0010 27=0001 61=0183 70=23f9 73=0000 [MATCH]

i2c: spk: init Right(SPK) (0x35):
  00=4211 02=0628 20=2883 21=c1f0 22=047c 26=1001 27=0000 61=0183 70=23f9 73=0000 [MATCH]

i2c: tfa: ktz8863a post-probe sanity raw11 = 1649 [OK]
```

**`[MATCH]` 的判据是 10 个核心寄存器逐位比对**，返回
`match ? 0 : EIO` —— 任一不符就不算成功，也不允许下一步放音。

## 值得看的三个值

### `22 = 047c` —— 字长已经是 32-bit

`TDMSSIZE` 在 `[6:2]`：`0x047c >> 2 & 0x1f` = **31**，即 32-bit 字长。

出厂默认是 `0x045c`（23 = 24-bit）。这一版按 `tfa98xx.cnt` 改成了 31，
和 AFE 的 I2S3 32-bit 帧对齐。

### `26/27` 两颗不同 —— 槽位映射真的分左右

```
Left  26=0010 27=0001
Right 26=1001 27=0000
```

**这是左右声道唯一的区别，也正是它该有的样子。**
上一版 `boot-980` 只读时两颗完全相同（`0010`/`0001`? 不，那时是 `0010`/`0001` 也一样），
现在左用 slot 0/1、右用 slot 1/0。

### `00 = 4211` vs 之前的 `4201`

`bit4` 从 0 变 1。`0x4211` 和 `0x4201` 差在 `bit4`。**这一位是什么我还没查**，
但它从 `init` 前变到 `init` 后，说明配置生效了。

`73 = 0000` —— 这是 `key2` 被锁定后的读回值。解锁态下 `0x03` 是 `0x0b73`（型号+版本），
锁上之后 `0x73` 这个地址读出 0。**符合「写完锁 key2」的设计。**

## 一个差点又误判的地方

第一次刷完 `boot-983a` 后设备没起来，我等了 90 秒没等到网卡就报失败。
**实际是我在 `adb reboot bootloader` 之后直接刷、刷完 `fastboot reboot`，
中间设备重启了两次，网卡名字变了又变回来。**

第二次刷（操作者重进 fb 后）一次成功。

**教训和之前一样：设备重启后网卡要重新配，别急着判断死活。**
这次终于记住了 —— 测 RETURN 前先 `PING` 拿 `PONG` 再发命令，一次就成。

## 保护逻辑（核实过的）

```c
rc_l = tfa_init_one_locked(Left...);
rc_r = tfa_init_one_locked(Right...);
if (rc_l == 0 && rc_r == 0) {
        i2c_spk_inited = true;        /* 两颗都必须 MATCH */
}

/* tfa_init_one_locked 结尾 */
return match ? 0 : EIO;

/* spk_tone 的分支 */
if (!i2c_spk_inited) {
        printf("spk_tone=%d refused because spk_init did not [MATCH]\n", ...);
}
```

**整个 `init` 过程 `PWDN=1`、`AMPE=0`，功放输出级不上电，所以绝对不出声。**
这一版写寄存器是安全的。

## 验证

| 项 | 结果 |
|---|---|
| 两颗 `[MATCH]` | ✅ |
| `ktz8863a sanity` | `1649 [OK]` |
| 8 核 | `5 of 8`（28 秒后 8） |
| 电池 | 间隔 10.0 秒 |
| GPU | `m2b: PASS` |
| **RETURN** | **32 秒** |

**写 TFA9873 不影响返回路径。**

## 镜像

`trial736/boot-983a.img`，sha256 `f0e5371451d4…`，boot-args
`cpus=8 tfa_probe=1 spk_init=1`（无 `spk_tone`、无 `bl=1`）。

**下一步 `boot-983b` 只加 `spk_tone=1`，不用重编内核，只改 loader boot-arg。**

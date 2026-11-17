# Android UFS 只读基线 716

> 2026-11-17 13:19，DSH。**纯只读。不刷写、不重启、不写设备文件、不碰 MMIO。**
> CMD 身份：`13fe3751ee8159a2bf064037c1d2205aa93a397ef402348cee626460178d7ec6`

**🔴 原始 GPT 数据留本地，未上传。**

---

## 0. 结论

| 项 | 结果 |
|---|---|
| **设备身份** | ✅ `boot_id 11aead4e-…`、`boot_a` MD5 `a63b2e35`（原厂）|
| **LUN 2 对应** | ✅ **sysfs 路径含 `0:0:0:2`** |
| **逻辑块长** | ✅ **4096**（`blockdev --getss`）|
| **前 64 KiB 捕获** | ✅ 精确 65536 B |
| **GPT 结构** | ✅ **两个 CRC 都精确匹配** |

---

## 1. 设备身份与前置

| 项 | 值 |
|---|---|
| `boot_id` | **`11aead4e-9595-4574-8e6a-6b229e287480`** ✅ 与 CMD 记录一致 |
| `boot_completed` | 1 |
| slot | `_a` |
| model | `xaga` |
| **`boot_a` MD5** | **`a63b2e35a7c2522cb24a6a10332b9bc8`** ✅ 原厂 |

---

## 2. LUN 2 对应（从 sysfs 推导，不假定字母）

### 2.1 解析链

```
/dev/block/by-name/xnusys
  → /dev/block/sdc88            (canonical)
  → DEVTYPE=partition, PARTN=88, PARTNAME=xnusys
```

**父盘 `sdc` 的完整 sysfs 路径：**

```
/sys/devices/platform/soc/112b0000.ufshci/host0/target0:0:0/0:0:0:2/block/sdc
```

### 2.2 🔴 这就是 LUN 2 的证据

| 路径段 | 含义 |
|---|---|
| **`112b0000.ufshci`** | **与 live DT 的 `ufshci@112b0000` 一致** ✅ |
| `host0/target0:0:0` | SCSI host/target |
| **`0:0:0:2`** | **SCSI 地址，末位 = LUN，即 LUN 2** ✅ |

**→ 与 loader 使用的 `UFS_LUN 2`（`ufs.h:42`）一致。**

### 2.3 额外观察

**`/sys/class/block/sdc/device/vendor` = `SAMSUNG`** —— 只记录，不作结论。

---

## 3. 块几何

| 命令 | rc | 输出 |
|---|---|---|
| `blockdev --getss /dev/block/sdc` | **0** | **4096** ✅ |
| `blockdev --getsize64 /dev/block/sdc` | **0** | **127959826432** |
| `blockdev --getss /dev/block/sdc88` | **0** | 4096 |
| `blockdev --getsize64 /dev/block/sdc88` | **0** | **2685370368** |

**盘总大小 127959826432 B，`getsize64` 是分区 2685370368 B（与 xnusys 一致）。**

**→ 逻辑块长 4096，符合 CMD 的「geometry 必须为 4096」要求。**

---

## 4. 前 64 KiB 捕获

**方式**：`adb -s <SERIAL> exec-out su -c 'dd if=/dev/block/sdc bs=4096 count=16'`
**→ 二进制直接写到主机，不在手机上建临时文件。**

| 项 | 值 |
|---|---|
| native rc | **0** |
| timed_out | **False** |
| 字节数 | **65536（精确）** |
| **SHA256** | **`03628d70602e4f21e36ace76f5c4e27ed1b10e50ae690704035799ea34e4e82b`** |
| **CRC32** | **`0x207fee0b`** |

**逻辑块 1（offset 4096，len 4096）：**

| 项 | 值 |
|---|---|
| SHA256 | **`f94a6807b4afbc7899d3621f570108eec4ffb5504a332198d0e05293f029449c`** |
| CRC32 | **`0x28a2836c`** |

**🔴 两个 bin 留本地**（`lun2-first64k.bin` / `lun2-lba1.bin`），未上传。

---

## 5. GPT 解析（含边界检查）

### 5.1 保护性 MBR（LBA 0）

| 检查 | 值 | 结果 |
|---|---|---|
| 末两字节 | `0x55aa` | ✅ |
| 分区 1 类型 | **`0xee`** | ✅ GPT 保护分区 |
| 起始 LBA | 1 | ✅ |

### 5.2 GPT 头（LBA 1）

| 字段 | 值 |
|---|---|
| magic | **`EFI PART`** ✅ |
| revision | `0x00010000` |
| **header_size** | **92**（期望 92）✅ |
| my_lba | 1 |
| alternate_lba | 31240191 |
| first_usable | 34 |
| last_usable | 31240183 |
| entries_lba | 2 |
| num_entries | 128 |
| entry_size | 128 |
| **header CRC32（记录）** | **`0x7fdb9634`** |
| **重算 header CRC32** | **`0x7fdb9634`** ✅ **匹配** |
| **entries CRC32（记录）** | **`0xce39daa8`** |
| **重算 entries CRC32** | **`0xce39daa8`** ✅ **匹配** |

### 5.3 边界检查

| 项 | 结果 |
|---|---|
| `header_size == 92` 且在 LBA 内 | ✅ |
| entries 偏移 | **8192 B（LBA 2）** |
| entries 长度 | **16384 B（128 × 128）** |
| **在 64 KiB 捕获范围内** | ✅ **8192 + 16384 = 24576 <= 65536** |
| entries CRC 可在本次核验 | ✅ 已验 |

**→ 前 64 KiB 足以覆盖主 GPT 的全部元数据（头 + 分区数组），两个 CRC 都可核验。**

---

## 6. 这个基线能做什么

**为一次固定的 `READ(10)` 提供可比对参考：**

| 可用 | 说明 |
|---|---|
| LUN 2 的 LBA 0/1 字节 | 若将来 XNU 侧 `READ(10)` 读到这两个 LBA，可逐字节比对 |
| 逻辑块长 4096 | 校验 READ(10) 的块单位 |
| 两个 CRC | 快速完整性判断 |

---

## 7. 🔴 这个基线**不能**做什么

| # | 不能 |
|---|---|
| 1 | **不推断链路速率**（CMD 明确禁止）|
| 2 | **不推断实际时钟** |
| 3 | **不做性能测试** |
| 4 | **不证明 XNU 侧能读** —— 只给了 Android 侧的参考值 |
| 5 | **不涉及 UFS MMIO** |
| 6 | **不含次 GPT**（alternate_lba 31240191 在盘尾，本次未读）|
| 7 | 未 `mount`、未 `ioctl` 透传、未写任何设备文件 |

---

## 8. 关于候选 `ufs_read_contract.h`

**Codex 已写好，我核对了身份：**

| 项 | 值 |
|---|---|
| `candidate/ufs_read_contract.h` | **4684 B / `0012fb4f4fa84a82a9616cf3f61c61f10d6268ac13262bbb37e740219f45f3ce`** |
| `candidate/test-ufs-read-contract.c` | 4492 B |
| `candidate/contract-arm64.c` | 398 B |

**它的开篇声明（我原样引用）：**

> 「Host-tested protocol/codec candidate only. **Not wired to XNU or the loader.**
> State transitions below are pure planning logic, **NOT atomic shared-memory operations.**
> Integration must supply serialization, mapping/barriers and a loader gate before ALL UFS IO.」

**🔴 我未接入、未声称共享原子性。完整的租约/超时交还设计由 Codex 准备。**

---

## 9. 边界

**未做**：刷写、重启、写设备文件或磁盘、MMIO 读、mount、ioctl 透传、
性能测试、服务变更、上传播放原始 GPT。

**手机保持原厂，本轮无 flash 无 reboot。**

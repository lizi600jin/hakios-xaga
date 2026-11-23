# 里程碑 6o · `/private/var` 是一个真的持久卷（Trial793a–793b）

2026-11-23。设备已回安卓。

## 一、结果

`/private/var` 现在是闪存上的 HFS+ 卷，跨重启保留：

```
-- writable filesystem --
/private/var: HFS Plus from /dev/xnusys_var, kept between boots
110 directories, 8 files
```

第一轮结束后系统在上面建了 765 个文件；重启之后**一个没丢**，还长到 4980 个目录。

## 二、只改了两处，没动一行 C

这套机制 `init.c` 里早就写好了，缺的只是把设备名传进去。

### 1. loader 的启动参数

`mtkloader.c` 里 `XNU_EXTRA_ARGS` 有 `#ifndef` 保护：

```c
#ifndef XNU_EXTRA_ARGS
#define XNU_EXTRA_ARGS      "cpus=1"
#endif
```

所以 `coordinator/loader-build.json` 里把编译参数改成：

```
-DXNU_EXTRA_ARGS="cpus=4 ... xnusys_blk=1 ufs=2 var_hfs=xnusys_var"
```

`var_hfs=xnusys_var` 就进了内核命令行。验证方式是从 `loader.bin` 里把字符串捞出来看。

### 2. `var.skel` 删掉一行

```
- d	755	0	0	/private/var/vm
```

## 三、为什么必须删掉 `/private/var/vm`

`vm_compressor_backing_store.h`：

```c
#if !XNU_TARGET_OS_OSX              // ← 我们是这条
#define MIN_SWAP_FILE_SIZE   (64 * 1024 * 1024ULL)
#define MAX_SWAP_FILE_SIZE   (128 * 1024 * 1024ULL)
#else
#define MIN_SWAP_FILE_SIZE   (256 * 1024 * 1024ULL)
#define MAX_SWAP_FILE_SIZE   (1 * 1024 * 1024 * 1024ULL)
#endif
```

`vm_swapfile_open` 带 `O_CREAT`：

```c
vnode_open(path, (O_CREAT | O_TRUNC | FREAD | FWRITE), S_IRUSR|S_IWUSR, 0, vp, ctx)
```

只要 `/private/var/vm` 存在，第 19 秒的 `vm_swapfile_create_thread` 就会在那里
建 swapfile，上限 128 MiB —— 而卷总共 174 MiB、空闲 162 MiB。

**实测确认删对了**：793a 之后 `freeBlocks` 只从 41440 掉到 39217，2223 块 = 8.7 MiB，
正是 110 个目录 + 8 个文件 + launchd 运行数据的体积。如果 swapfile 建了，
应该掉 32768 块（128 MiB）。

## 四、这套机制原来是这样的

`init.c:834` 的 `mount_data_volume()`：

```c
const char *name = boot_arg("var_hfs");
if (name == NULL) return 0;                  // 没传就回退 ramfs
strcpy(dev, "/dev/"); strcpy(dev + 5, name);
args.fspec = dev;
if (mount("hfs", "/private/var", 0, &args) != 0) {
    printf("  HFS Plus from %s: %s; a memory filesystem instead, empty at every boot\n", ...);
    return 0;
}
printf("  /private/var: HFS Plus from %s, kept between boots\n", dev);
return 1;
```

挂上之后 `mount_writable()` 做四件事：

1. `empty_tree("/private/var/tmp")`、`/run`、`/vm` —— 每次启动清空这三个
2. 读 `/etc/var.skel`，建 110 个目录、2 个符号链接、8 个文件（权限和属主都在模板里）
3. 写三个 `.migrated` 标记，让 logd 认这些是新目录
4. 挂不上就回退 ramfs，不影响启动

`in_image()` 拼的是 `/redwood` + 路径，所以 `var.skel` 里那 8 个 plist 的**源在只读系统镜像里**，
不是内存盘上的 `/var`。这一点很关键：它意味着把 `/var` 换成符号链接不会动到拷贝源。

## 五、793a / 793b 的数据

```
             attributes    freeBlocks   fileCount  folderCount
基线         0x80002100    41440        9          —
793a 之后    0x80002000    39217        765        —
793b 之后    0x80002000    38079        765        4980
```

`793a` 写的东西在 `793b` 里一个没少。`freeBlocks` 两次都是正常的小幅下降。

`793b` 没有出现 `replay_journal` —— 因为 793a 结束时 `fsync` 已经把事务 checkpoint 了
（`start == end`），没有未提交的东西要回放。它照样挂上了脏卷（`Unmounted=0`），
这本身就是 journal 在工作：没有 journal 的卷在这个状态下会拒绝可写挂载。

## 六、还没解决：`/var` 没有指向 `/private/var`

运行时的证据：

```
wifid[8:232] store URL: file:///var/root/Library/Caches/com.apple.wif...
```

`wifid` 在往 `/var/root/Library/Caches/` 写，而 `/var` 还是内存盘上的实体目录
（`drwxr-xr-x /redwood/var`）。这些数据下一轮就没了。

所以现在只有一半持久化：

| 路径 | 存储 | 跨重启 |
|---|---|---|
| `/private/var/*` | HFS+ 卷 | 保留 |
| `/var/*` | 内存盘 | 丢失 |

标准 iOS 的做法是 `/var -> private/var`。`var.skel` 里没有这一条，要单独加。
难点是 `/var` 已经是个非空目录（里面有 `mobile/Library/Preferences` 的一份），
`symlink()` 会 `EEXIST`，得先把它删掉。

## 七、文件

| 文件 | 内容 |
|---|---|
| `var.skel` | 删掉 `/private/var/vm` 之后的骨架（110 目录 / 2 链接 / 8 文件） |
| `loader-build.json` | 带 `var_hfs=xnusys_var` 的 loader 编译参数 |

镜像：`logs/ufs-apfs-fstree-root-736/trial736/boot-793a.img`（793b 用同一个，只重启不刷机）。

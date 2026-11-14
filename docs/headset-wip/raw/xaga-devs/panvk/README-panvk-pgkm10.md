# PanVK for OnePlus PGKM10（MT6895 / Mali-G610 MC6）

本仓库是从 [wxcvm/MoveCertificate](https://github.com/wxcvm/MoveCertificate) 的 PanVK
实验驱动子工程（原 `driver-fix/`）分离出来的独立仓库；`git subtree split` 保留了完整提交历史。
MoveCertificate 主仓库只负责证书模块，不再承载驱动工作。

- 目标设备：OnePlus PGKM10（OP5565）/ Android 17 / Mali-G610 MC6 / MTK kbase
- 驱动来源：上游 [panvk-kbase-android](https://github.com/zenithblue-oss/panvk-kbase-android)
  的 RC3，叠加本仓库 `patches/` 019–021 与 `diagnostics/` 022–030
- 安全默认：`enable_global=0`（使用原厂 Vulkan）；已知平台（PGKM10 + SDK 37）拒绝全局替换
- 构建：GitHub Actions（`.github/workflows/panvk-*.yml`），产物为 ICD `.so` 与构建溯源
- 本机验证入口：`tests/driver-test.sh`、`tests/wsi3-launcher-test.sh`、
  `scripts/build-panvk-experimental.sh`
- 许可证：Mesa 派生补丁遵循 MIT；完整声明见 `licenses/`

目录：`patches/`（019–021 源码修复）、`diagnostics/`（022–030 诊断/兼容补丁）、
`probe/`（显示诊断 APK 源码）、`wsi3-module/`（实验模块包装）、`scripts/`、`tests/`、
`.github/workflows/`。

> 说明：合并前的设备实验证据（`diagnostics/` 大目录、截图、日志）未随仓库迁移，
> 仍保留在本地；仓库只包含可复现的源码、补丁、脚本与文档。
>
> 注意：`scripts/build-panvk-present-probe.sh` 与 `.github/workflows/panvk-wsi3-module.yml`
> 仍固定引用原 MoveCertificate 仓库的历史构建产物（artifact id），用于溯源；在本仓库
> 完成第一次构建后，应改为引用本仓库的 artifact。

---

## PanVK RC3 与原厂回退包

新增的 [WSI3 实验模块](wsi3-module/README.md) 单独打包为
`PanVK-WSI3-PGKM10-experimental.zip`，使用完成 AFBC 对齐对照的固定 WSI3 库。
安装默认关闭，通过 Root 管理器的“操作”按钮选择下次启动是否启用。
它包含本机生成的 ColorX 兼容副本流程，但不分发 OEM 库；仅接受已核对的原厂库版本。
临时实屏对照与隔离命名空间激活通过，但随后全局启用出现微信 Flutter ANR，
并记录到 SurfaceFlinger 的 P010 缓冲区纹理创建失败。目前不适合日常全局启用；
应关闭全局替换并完整重启，详见 [WSI3 兼容性复核](WSI3-COMPATIBILITY.md)。
以下旧 RC3 安装包及其兼容限制继续保留；它们没有自动升级为 WSI3。

两个包使用相同模块 ID `MTK_D8100_Vulkan1.4.Panvk`，用于替换手机现有的旧 PanVK 模块。
安装任意一个都需要完整重启。不写 vendor 分区；两个包都默认使用原厂 Vulkan、OpenGL/EGL。

- `PanVK-RC3-D8100-Vulkan.zip`：wrapper2 包含官方 RC3 Android arm64 二进制，默认关闭全局替换；库可供支持自选驱动的应用单独测试。
- `PanVK-guarded-launcher-v1.0.2.zip`：不包含新二进制，默认关闭替换，重启回到原厂 HAL。

RC3 来自 [上游发布](https://github.com/zenithblue-oss/panvk-kbase-android/releases/tag/panvk-kbase-v0.1.0-rc3)。
上游记录了 Mali-G610 MC3 的测试；2026-10-12 已在 PGKM10 的 G610 MC6 / Android 17
进行全局 Vulkan HAL 对照。星穹铁道两次在 ColorX 的
`colorx::vk::core::CxPhysicalDevice::constructFragmentShadingRatesInfo` 内调用空指针，
崩溃映射确认 RC3 BuildID。解除 RC3 挂载后用户确认能进入游戏。
因此启动器明确拒绝在 `ro.product.model=PGKM10` 且 SDK 37 时全局替换。
Quetta 部分花屏尚未单独定位。这是恢复原厂的兼容处理，未修复 PanVK 内部实现，
也未修改、禁用 ColorX 系统应用或伪装 Vulkan 扩展支持。
官方 Android 构建关闭了 OpenGL/EGL/GLES，所以本包仅更新 Vulkan。

随后进行了两次可撤销 ColorX 绕过测试：屏蔽增强库、或保留增强库并精确跳过 Vulkan
注入，星穹铁道均继续崩溃于系统 `GetSwapchainImagesKHR` 的空 swapchain 路径。
精确绕过曾短暂显示正常，随后闪退；交换链创建失败原因尚未确认。这些失败方案未加入
安装包，所有测试绑定已撤销。已知平台的全局替换限制继续保留。

后续独立检查已复现系统 native-buffer 路径误把 `gralloc_extra` 当作 DMA-BUF 的错误；
同一缓冲区走 AHB 路径可以成功导入。已准备 FD 搜索源码补丁，并在本机验证其中的
helper 能导入真实 GPU 内存、拒绝元数据及处理变化的 FD 索引。RC3-anbfix1 已完整
重建，实机通过图像导入、GPU 清屏回读及 64 次导入销毁检查。但游戏实际加载 PanVK
后仍黑屏，独立探针进一步定位到 kbase 错用 DRM 同步复制；RC3-anbfix2 的对应
修复已通过实机同步导出测试，但导入回归暴露了每轮泄漏一个 FD 的问题。
RC3-anbfix3 的 waiter 生命周期修复已通过实机回归：64 次同步往返的 FD 数量
保持 23 → 23，图像导入销毁保持 22 → 22，GPU 像素回读正常。星穹铁道已确认
实际使用新版 Vulkan 并能显示画面，但用户仍报告闪烁、重影或错位，尚不适合
作为本机日常全局驱动；浏览器也未通过验证。安装包中的 `.so`
仍是官方 RC3；详情及验证边界见
[INVESTIGATION.md](INVESTIGATION.md)。

实验构建使用独立的 [PanVK Actions 工作流](.github/workflows/panvk-experimental.yml)。
`PanVK-RC3-anbfix3-experimental` 产物包含 `libvulkan_panfrost.so`、固定源提交、补丁
及校验记录，是供实机验证的驱动库归档，不是 Root 模块安装包。构建输入见
`experimental-build.json`，本地构建入口为 `scripts/build-panvk-experimental.sh`，
所需 Ubuntu / Android NDK 环境见工作流。实验驱动仅提供 Vulkan，未包含 OpenGL。

Actions 从固定发布下载，核对 ZIP 和库的 SHA-256、BuildID、AArch64 ABI、API 35、HMI
及三个 ICD 导出和系统动态依赖；保留二进制原始字节，不修改驱动声明或伪装扩展支持。
源提交、校验值见 `upstream.json`；上游原始构建记录保留在新包 `upstream/`，本包自己的
`VALIDATION.json` 记载本机全局运行失败和原厂对照结果。构建时的 GPL/系统调用例外、MIT、Mesa
许可证声明随包保留；`licenses/Mesa` 来自固定 Mesa 提交，官方源码来源见上游 SOURCE.json。

原旧模块将同一 arm64 库挂到 32 位目录，并在卸载时尝试写回不存在的 old_mali.so；
这两项已移除。新库在安装及启动时检查校验值和最低 SDK，错误时保留原厂 HAL。
支持自选 ICD 的应用需要各自验证；本包不保证普通 Android 游戏可加载自选 ICD。

如需从新驱动回退，可在 Root 管理器禁用此模块并重启，或安装上述原厂回退包再重启。
旧 wrapper1 也可将模块目录 `driver.conf` 中的 `enable_global=1` 改为 `0` 后重启。
wrapper2 在已知失败的平台阻止全局替换，即使配置被改为 `1` 也会保留原厂。

本地打包新驱动：`python3 scripts/prepare-driver.py`（或 `--archive 官方adpkg.zip`），
然后运行 `python3 scripts/package.py`。驱动二进制只出现在忽略的 `driver-dist/` 和产物
ZIP 中，不进入源码 Git 历史。

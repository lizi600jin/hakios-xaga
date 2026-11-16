# 定点读取器审计与修正版

Codex，2026-11-16。未访问设备。

原始DSH目录 logs/regmap-reader-prep-20261116-161918 保持原样。
其源码、ARM64产物、构建记录和日志备份见本目录original/与original-manifest.json。

发现并处理：

1. 核心忽略metadata/registers的close错误，可能错误退出0。
   已增加R_NAME_CLOSE、R_RANGE_CLOSE、R_REGS_CLOSE；只尝试关闭一次，失败不重试，不进入下一阶段。
   cache属性的错误诊断改为“open未返回ENOENT”，不把EACCES等错误误报为文件存在。
2. 旧夹具registers返回fd100，但read把fd100解释成name；对read(fd105)的断言不能证明该fd未被误读。
   新C夹具使用正确的typed fd并检查pread目标；完整ELF仿真又故意为各文件反复复用fd3，按打开状态辨别类型。
3. 原pread夹具把0用作“正常18”哨兵，无法注入真正零返回；现直接注入实际返回值，并只写入实际短读字节。
4. 新增metadata read/close及stdout不完整/错误场景。C夹具的轨迹匹配改为前缀，防止read与pread子串互相计数。
5. 原构建记录写test_reader.c为12653 B、5b6f04e1...，当前交付源码却为13190 B、933668e6...。
   所以原记录不能绑定当前测试源码和产物。新清单冻结6项输入并检查构建前后相同。
   当前原源码只有32个case_expect调用；“40个场景”的旧表述不成立，不以191条断言代替覆盖清单。

验证：

- replay-checks.py：源码直接运行ASan/UBSan，48个场景、287条断言全部通过，零诊断。
- test-reader-elf.py：最终ARM64 ELF从_start实际执行，Linux系统调用边界由模型提供；60个场景通过。
  核验每个openat的AT_FDCWD、O_RDONLY|O_CLOEXEC|O_NOFOLLOW与固定路径；
  只允许metadata read，registers只许一次pread64(18,86526)，无其他系统调用；
  覆盖错误、短读、close错误、XX/格式、stdout短写后完成/停止/失败及00/ff。
- 使用相同最终ELF模型对原1d96bdd5...候选做负对照：60例中恰好3例失败，
  即name/range/registers的close EIO均仍退出0。结果在original-elf-model.json；此FAIL是保留的缺陷证据。
- 最终修正版ELF4232 B，SHA256 63688efccd5728b0155a782eb299db979d6150b2096b4dacbc1e083429aa30e1。
  AArch64 EXEC，无INTERP、动态段或非空未定义符号。源码和测试身份见review-inputs.sha256。

实际系统调用、内核挂载/SELinux、当前模块身份和电气行为均未执行；不能宣称工具已在手机可用。
该ELF仅为已通过主机验证的候选。未来设备CMD仍必须先核验运行kernel/module身份与播放/元数据前提。
不改变mode4生产源码或内核，不打boot/payload镜像，不生成trim默认值或设备boot-args。

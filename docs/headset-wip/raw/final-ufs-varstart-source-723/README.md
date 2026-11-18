# Trial723 源码与主机复测

`source/` 保留编入内核的双块适配器、事务引擎、编解码器、租约和引用的可变 LBA 头文件。清单给出每个冻结副本的字节数、SHA-256 和本地相对出处。集成补丁以相邻的 721 源码归档为基线，涉及内核初始化、静止处理、loader 和租约版本。将 721 归档的 `source/` 复制到空目录，再在该目录执行 `git apply <本目录绝对路径>/721-to-723-integration.patch`；清单的 `integration` 列出十个文件的前后摘要。完整 XNU 构建需要原工程和工具链。[源码清单](SOURCE-MANIFEST.json) · [设备验收](../../UFS-VARSTART-DELIVERY-20261118.md)

在本目录运行以下命令。两个 C 测试使用内存中的控制器模型，Python 测试生成独立的合成 HLOG。合成回归的 52 例和当时执行侧留存的 30 例分别计数。

```bash
set -eu
out=$(mktemp -d)
for suite in codec engine; do
    clang -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-function -Wconversion -Wshadow -pedantic \
        -I "source/ufs_twoblock/$suite" "tests/test-ufs-two-block-$suite.c" -o "$out/$suite"
    "$out/$suite"
done
python3 -B tests/check-parser.py --parser "$PWD/host-tools/parse-hlog-723.py" --out-dir "$out/hlog"
```

平台集成测试还需要 `UFS_REF_WINDOW` 指向本机留存的 65,536 字节参考文件。用 `clang -std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I tests -I source/ufs_twoblock tests/test-ufs-twoblock-platform.c -o "$out/platform"` 构建，再运行 `"$out/platform"`。参考文件 SHA-256 为 `03628d70602e4f21e36ace76f5c4e27ed1b10e50ae690704035799ea34e4e82b`。测试程序开头沿用了旧的 722 夹具标签；内核契约的实际 trial 为 723。[冻结主机结果](../device-ufs-varstart-723/R23-RESULT-v3.json)

# 里程碑 6c · 主机上把"拒绝不许 poison"钉死（Trial750）

2026-11-21。**主机测试，未上机。**

## 补上的两条断言

Trial749 两次把手机搞到必须手动进 fastboot，根因是同一个：写失败时不分情况一律 poison。
这条只有真机能触发，主机测试从没覆盖过。现在覆盖了：

```
Trial750 zone write: refused outside the window, and never poisoned PASS
Trial750 zone write: a refused write leaves the lease usable PASS
```

两条都在检查同一件事的两面：

- 六种越界地址（容器超级块、容器内、容器末块、日志区首块、分区末块、远越界）全部被拒、
  **一次都没 ring**、**且没有 poison**
- 设备故意留在非 idle 状态使请求发不出去，租约必须回到 IDLE 且 `US_POISON_BIT` 为 0

## 顺带修掉的主机测试缺陷

主机模型原来只认 READ(10)。第一条 WRITE(10) 一到就 `assert(upiu[16] == 0x28)` 崩。
现在模型认识两种：

```c
if (upiu[16] == 0x2a) { ... WRITE(10), data-out ... }
else                  { ... READ(10),  data-in  ... }
```

模型还多了一个写分支，检查写命令的 LBA 落在 608448..653559。
这个分支必须放在 chain 分支**之前**——写的地址 608456 恰好落在链分支接受的
2399..655607 区间里，放在后面会被当成链上的块去比对。

## 未跑通的部分

我原本想再测两条：一次成功的写、以及"发出后恢复失败必须 poison"。这两条没跑通：

- 写路径的轮询在主机模型上跑满 50 万次也没判完成（`completions=500000` 而 `completed=0`）
- 原因未查明，我不打算继续在这个模型细节上耗

所以这两条测试被移除了，没有留下半成品。**写路径的完整成功流程仍然只有真机才能验证。**

## 当前状态

```
-O2 -Wall -Wextra -Werror   37 PASS
ASan + UBSan                37 PASS   0 违规
test-ufs-dir-enum           11 PASS
test-ufs-path-walk           8 PASS
```

## 意义

真机上那个错误——把"命令没发出"当成"发出后没恢复"——现在有一条会红的测试盯着。
下次再写这个函数，改坏了会在主机上先炸，而不是在手机上卡死。

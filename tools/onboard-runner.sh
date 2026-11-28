#!/usr/bin/env bash
# 在 cloud VM 上注册 GitHub self-hosted runner，并把它拉起来。
#
# 为什么需要这一层：cloud VM 没有入站入口（eth0 仅有 /32 沙盒地址），
# 因此不可能由外部直接连入；runner 是"主动出站拉活"的模型，正好绕开这一点。
# 注册后，持有仓库写权限的调用方即可通过 workflow_dispatch 让命令在本机执行。
#
# 用法（token 只从环境变量读，不落盘、不进日志）：
#   RUNNER_TOKEN=<settings/actions/runners 页面的一次性注册令牌> \
#     bash tools/onboard-runner.sh
#
# 可选环境变量：
#   REPO_URL     默认 https://github.com/lizi600jin/hakios-xaga
#   RUNNER_NAME  默认 xaga-vm
#   RUNNER_DIR   默认 $HOME/actions-runner
#   LABELS       默认 self-hosted,linux,x64,xaga-vm
#
# 注意：RUNNER_TOKEN 一次性有效（官方 1 小时过期），用过即作废；
#       若需重新注册，请回页面重新生成，不要复用旧值。

set -euo pipefail

REPO_URL="${REPO_URL:-https://github.com/lizi600jin/hakios-xaga}"
RUNNER_NAME="${RUNNER_NAME:-xaga-vm}"
RUNNER_DIR="${RUNNER_DIR:-$HOME/actions-runner}"
LABELS="${LABELS:-self-hosted,linux,x64,xaga-vm}"
LOG_DIR="${LOG_DIR:-$RUNNER_DIR/logs}"

say() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
die() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }

# ---- 预检 ----
[ -n "${RUNNER_TOKEN:-}" ] || die "RUNNER_TOKEN 未设置；请从仓库 Settings → Actions → Runners → New self-hosted runner 获取一次性令牌"

if [ "$(id -u)" -eq 0 ]; then
  die "拒绝以 root 运行（与合规要求一致：runner 不得以 root 执行任务）"
fi

[ -x "$RUNNER_DIR/config.sh" ] || die "未找到 $RUNNER_DIR/config.sh，请先解压 actions-runner 发行包"
mkdir -p "$LOG_DIR"

say "repo=$REPO_URL"
say "name=$RUNNER_NAME"
say "dir=$RUNNER_DIR"
say "labels=$LABELS"

# 已注册则直接用，不重复 config（重复 config 会消耗新令牌）
if [ -f "$RUNNER_DIR/.runner" ]; then
  say "检测到已注册（.runner 存在），跳过注册步骤"
else
  say "执行注册"
  cd "$RUNNER_DIR"
  ./config.sh \
    --url "$REPO_URL" \
    --token "$RUNNER_TOKEN" \
    --name "$RUNNER_NAME" \
    --labels "$LABELS" \
    --work "$RUNNER_DIR/_work" \
    --unattended \
    --disable-auto-update \
    --replace
  say "注册完成"
fi

# 拉起（幂等：已有 listener 进程则不再起第二个）
if pgrep -f "Runner.Listener" >/dev/null 2>&1; then
  say "listener 已在运行，跳过启动"
else
  cd "$RUNNER_DIR"
  nohup ./run.sh > "$LOG_DIR/runner.out" 2>&1 &
  sleep 3
  pgrep -f "Runner.Listener" >/dev/null 2>&1 \
    && say "listener 已拉起，日志: $LOG_DIR/runner.out" \
    || die "listener 未起来，请查 $LOG_DIR/runner.out"
fi

say "完成。回到 GitHub 页面确认 runner 状态为 Idle（绿点）"

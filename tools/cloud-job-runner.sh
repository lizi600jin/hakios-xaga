#!/bin/sh
# The only entry point a remote job is allowed to use.
#
# A job is a directory holding a job.json that names one of the actions below and,
# for the apply action, a patch file. Nothing in the job is ever eval'd: the action
# name is matched against a list of verbs, and every path argument must resolve
# inside the job directory. An allowlist plus confinement is the only reason a
# folder of dropped files is safe to consume unattended.
#
# Usage: tools/cloud-job-runner.sh <job-dir>
#
# Writes <job-dir>/status.json and <job-dir>/build.log beside the job, so the result
# lands where the queue was watched for it. A refusal also writes status.json: a
# job that vanishes is worse than one that fails, because the other side cannot
# tell it apart from one that never arrived.

set -eu

REPO=$(cd "$(dirname "$0")/.." && pwd)
JOB=${1:-}

refuse() {
	reason=$1
	code=${2:-2}
	printf 'refused: %s\n' "$reason" >&2
	if [ -n "$JOB" ] && [ -d "$JOB" ]; then
		printf '{"exit_code": %d, "error": "%s"}\n' "$code" "$reason" \
			> "$JOB/status.json"
	fi
	exit "$code"
}

[ -n "$JOB" ] || refuse "no job directory given"
[ -d "$JOB" ] || refuse "job directory does not exist"

JOB=$(cd "$JOB" && pwd)
JOB_JSON=$JOB/job.json
[ -f "$JOB_JSON" ] || refuse "job.json is missing"

# Root is never acceptable here: the runner owns the repository and nothing else,
# and a root job could reach every other user's files on this host.
[ "$(id -u)" -ne 0 ] || refuse "must not run as root" 1

# job.json is read with sed rather than a JSON parser on purpose: one flat object,
# two keys, and a parser would be one more thing to get wrong. A malformed line
# simply does not match, and the job is refused for having no action.
read_key() {
	sed -n 's/.*"'"$1"'"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$JOB_JSON" | head -1
}

ACTION=$(read_key action)
PATCH=$(read_key patch)

[ -n "$ACTION" ] || refuse "job.json has no action"

# A path argument may not leave the job directory, however it is spelled.
inside_job() {
	_canon=$(cd "$(dirname "$1")" 2>/dev/null && pwd) || return 1
	_resolved=$_canon/$(basename "$1")
	case "$_resolved" in
		"$JOB"/*) return 0 ;;
		*) return 1 ;;
	esac
}

START=$(date -u +%Y-%m-%dT%H:%M:%SZ)
LOG=$JOB/build.log
: > "$LOG"

cd "$REPO"

RC=0

# Every command runs under a timeout: a hung compiler on a shared build host costs
# the next job, not this one. Arguments go to timeout directly, never through a
# shell for re-parsing.
run() {
	_to=$1
	shift
	printf '== %s ==\n' "$*" >> "$LOG"
	timeout "$_to" "$@" >> "$LOG" 2>&1
}

case "$ACTION" in
check_wireless)
	run 600 sh tools/check-wireless.sh || RC=$?
	;;

apply_and_check)
	[ -n "$PATCH" ] || refuse "apply_and_check needs a patch"
	[ -f "$JOB/$PATCH" ] || refuse "patch not present in the job directory"
	inside_job "$JOB/$PATCH" || refuse "patch path escapes the job directory"
	# --check first, and no apply if it fails: a half-applied patch is worse than a
	# rejected one, because the next job then sees a tree nobody described.
	run 60 git apply --check "$JOB/$PATCH" || refuse "the patch does not apply cleanly" 3
	run 60 git apply "$JOB/$PATCH" || refuse "git apply failed" 3
	run 600 sh tools/check-wireless.sh || RC=$?
	;;

build_xnu)
	[ -n "${XNU_SRC:-}" ] || refuse "XNU_SRC is not set on this host"
	# The cd happens in a subshell so the repository stays the working directory
	# for the status write at the end.
	(
		cd "$XNU_SRC" && exec timeout 1800 make -j"$(nproc)" BUILD_LOCK=1
	) >> "$LOG" 2>&1 || RC=$?
	;;

qemu_smoke)
	run 600 sh tools/qemu-smoke.sh || RC=$?
	;;

*)
	refuse "action '$ACTION' is not in the allowlist"
	;;
esac

END=$(date -u +%Y-%m-%dT%H:%M:%SZ)
printf '{"exit_code": %d, "action": "%s", "started_at": "%s", "finished_at": "%s", "head": "%s"}\n' \
	"$RC" "$ACTION" "$START" "$END" \
	"$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo unknown)" \
	> "$JOB/status.json"

exit "$RC"

#!/bin/sh
# B1 preflight: can this machine be a runner for the wireless workflow?
#
# Read-only. It writes nothing outside a temp directory, starts no service, and
# prints one line per check so the answer can be pasted back verbatim. A FAIL here
# is cheaper than a workflow run that dies at step 4 with a compiler error nobody
# can see.
#
# Usage: sh tools/preflight-b1.sh [repo-dir]
#
# Exit 0 when there is nothing that blocks a run; 1 when something must be fixed
# first. WARN lines never fail the check -- they are the things that only matter
# for some actions (qemu_smoke needs a qemu checkout, build_xnu needs XNU_SRC).

set -u

REPO=${1:-$(cd "$(dirname "$0")/.." && pwd)}
FAILS=0
WARNS=0

pass() { printf 'PASS  %s\n' "$*"; }
warn() { printf 'WARN  %s\n' "$*"; WARNS=$((WARNS + 1)); }
fail() { printf 'FAIL  %s\n' "$*"; FAILS=$((FAILS + 1)); }
info() { printf '      %s\n' "$*"; }

echo "host: $(uname -srm 2>/dev/null || echo unknown)"
echo "repo: $REPO"
echo

# ---- 1. the user the runner will run as -------------------------------------
if [ "$(id -u)" -eq 0 ]; then
	fail "running as root -- the workflow refuses this, register the runner as a normal user"
else
	pass "not root (uid $(id -u), user $(id -un))"
fi

if command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
	warn "passwordless sudo is available to this user -- a runner does not need it and it widens what a bad job could reach"
else
	pass "no passwordless sudo"
fi

# ---- 2. the repository -----------------------------------------------------
if [ -d "$REPO/.git" ]; then
	pass "git checkout at $REPO"
	HEAD_SHORT=$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo unknown)
	info "HEAD $HEAD_SHORT ($(git -C "$REPO" rev-parse --abbrev-ref HEAD 2>/dev/null || echo '?'))"
	if [ -n "$(git -C "$REPO" status --porcelain 2>/dev/null)" ]; then
		warn "working tree is dirty -- a workflow checks out a clean tree, so local edits will not be tested"
	fi
else
	fail "no .git at $REPO -- pass the repository directory as an argument"
	HEAD_SHORT=unknown
fi

if [ -f "$REPO/.github/workflows/wireless.yml" ]; then
	pass "workflow present: .github/workflows/wireless.yml"
else
	fail "workflow missing -- copy .github/workflows/wireless.yml into the repository"
fi

# ---- 3. the tools the checks use -------------------------------------------
MISSING=""
for t in git sh cc timeout nproc sed; do
	command -v "$t" >/dev/null 2>&1 || MISSING="$MISSING $t"
done
if [ -z "$MISSING" ]; then
	pass "core tools present (git sh cc timeout nproc sed)"
else
	fail "missing tools:$MISSING"
fi

for t in ninja meson pkg-config; do
	if command -v "$t" >/dev/null 2>&1; then
		pass "$t present"
	else
		warn "$t missing -- only needed to build qemu"
	fi
done

if command -v pkg-config >/dev/null 2>&1; then
	for lib in glib-2.0 pixman-1; do
		if pkg-config --exists "$lib" 2>/dev/null; then
			pass "$lib $(pkg-config --modversion "$lib" 2>/dev/null)"
		else
			warn "$lib missing -- only needed to build qemu"
		fi
	done
fi

# ---- 4. the optional pieces each action needs ------------------------------
if [ -n "${XNU_SRC:-}" ] && [ -d "${XNU_SRC:-}" ]; then
	if [ -f "$XNU_SRC/Makefile" ]; then
		pass "XNU_SRC=$XNU_SRC with a Makefile (build_xnu will work)"
	else
		warn "XNU_SRC set but has no Makefile -- build_xnu would fail"
	fi
else
	warn "XNU_SRC not set -- build_xnu is skipped by the workflow (check_wireless still runs)"
fi

QEMU_FOUND=""
for cand in "${QEMU_DIR:-}" "$REPO/../xaga-qemu" "$REPO/../xaga-work/xaga-qemu"; do
	[ -n "$cand" ] && [ -d "$cand" ] && QEMU_FOUND=$cand && break
done
if [ -n "$QEMU_FOUND" ]; then
	if [ -x "$QEMU_FOUND/build/qemu-system-aarch64" ]; then
		pass "qemu built at $QEMU_FOUND/build/qemu-system-aarch64"
	else
		warn "qemu checkout at $QEMU_FOUND but not built -- qemu_smoke will SKIP (not fail)"
		info "build it with: cd $QEMU_FOUND/build && ../configure --target-list=aarch64-softmmu --disable-werror && ninja"
	fi
	if [ -f "$QEMU_FOUND/fw/LUA_1" ] && [ -f "$QEMU_FOUND/data/lu2.img" ]; then
		pass "vendor dumps present (a boot can be attempted)"
	else
		warn "vendor dumps absent (fw/LUA_1, data/lu2.img) -- smoke will SKIP the boot step, that is expected"
	fi
else
	warn "no xaga-qemu checkout found -- qemu_smoke will SKIP"
fi

# ---- 5. room to build -------------------------------------------------------
AVAIL_KB=$(df -Pk "$REPO" 2>/dev/null | awk 'NR==2 {print $4}')
if [ -n "${AVAIL_KB:-}" ]; then
	AVAIL_GB=$((AVAIL_KB / 1024 / 1024))
	if [ "$AVAIL_GB" -ge 15 ]; then
		pass "$AVAIL_GB GiB free under the repository"
	else
		warn "only ${AVAIL_GB} GiB free -- a kernel or qemu build wants more"
	fi
else
	warn "could not read free space"
fi

# ---- 6. the actual check, run once now -------------------------------------
echo
echo "== running tools/check-wireless.sh =="
if [ -f "$REPO/tools/check-wireless.sh" ]; then
	if OUT=$(sh "$REPO/tools/check-wireless.sh" 2>&1); then
		pass "check-wireless.sh passed"
		info "$(printf '%s\n' "$OUT" | tail -1)"
	else
		fail "check-wireless.sh failed -- the workflow would fail too"
		printf '%s\n' "$OUT" | tail -15 | sed 's/^/      /'
	fi
else
	fail "tools/check-wireless.sh missing"
fi

echo
echo "== summary =="
echo "fails: $FAILS   warns: $WARNS"
if [ "$FAILS" -eq 0 ]; then
	echo "RESULT: ready to register this machine as a runner (HEAD $HEAD_SHORT)"
else
	echo "RESULT: fix the FAIL lines above before registering"
fi
exit "$([ "$FAILS" -eq 0 ] && echo 0 || echo 1)"

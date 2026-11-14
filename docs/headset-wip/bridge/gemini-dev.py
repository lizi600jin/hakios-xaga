#!/usr/bin/env python3
"""
gemini-dev — Gemini 4 Hydrogen as a project participant.

Why this exists: Codex runs in an Orca terminal and can `orca terminal send` the
DSH controller directly. Gemini is API-only, so it needs its own loop with
tools and a mailbox before it can take part in the same collaboration.

Tools given to the model are all read-only except two writers:
  write_note      append a note to the shared bridge (harness -> DSH)
  write_file      write inside docs/headset-wip/ only

Usage:
  ./gemini-dev.py --ask "question"                 one shot
  ./gemini-dev.py --task "prompt" --rounds 8       agent loop, up to N tool rounds
  ./gemini-dev.py --session NAME --ask "..."       keep history under NAME
  ./gemini-dev.py --reset --session NAME           drop history
  ./gemini-dev.py --show-session NAME

Environment:
  GEMINI_API_KEY   required (never hardcode it in a committed file)
  GEMINI_MODEL     default gemini-4-hydrogen
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request

ENDPOINT = "https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent"

DEFAULT_MODEL = "gemini-4-hydrogen"
STATE_DIR = os.path.expanduser("~/.local/share/gemini-dev")
ROOT = "/home/lizi/IOS/repo/hakios-mt6895"
WIP = os.path.join(ROOT, "docs/headset-wip")
BRIDGE_IN = os.path.join(WIP, "bridge", "to-dsh-from-gemini.md")

MAX_TOOL_OUTPUT = 24000        # chars per tool result, keeps the context sane
SHELL_TIMEOUT = 60

# Anything that could damage the tree or grab the phone is refused outright.
SHELL_DENY = re.compile(
    r"\b(rm|mv|dd|mkfs|shutdown|reboot|kill|pkill|chmod|chown|sudo|"
    r"git\s+(reset|checkout|clean|stash|push|commit|add)|"
    r"adb|fastboot|flash|make|build\.sh|npm|cargo\s+build)\b"
)
SHELL_ALLOW = re.compile(r"^\s*(ls|cat|head|tail|sed|awk|grep|rg|wc|find|file|"
                         r"stat|md5sum|sha256sum|git\s+(log|show|diff|status|cat-file|ls-tree)|"
                         r"python3|sh\s+-n|du|sort|uniq|cut|tr)\b")


# ---------------------------------------------------------------- tool impl

def _truncate(text):
    if len(text) <= MAX_TOOL_OUTPUT:
        return text
    keep = MAX_TOOL_OUTPUT // 2
    return (text[:keep] + "\n\n... [%d chars elided] ...\n\n" % (len(text) - keep * 2)
            + text[-keep:])


def _resolve(path):
    """Resolve a path, refusing anything outside the repo."""
    if not path:
        return None
    p = path if os.path.isabs(path) else os.path.join(ROOT, path)
    p = os.path.realpath(p)
    if not (p == ROOT or p.startswith(ROOT + os.sep) or p.startswith("/tmp/")):
        return None
    return p


def tool_read_file(args):
    path = _resolve(args.get("path", ""))
    if not path or not os.path.isfile(path):
        return "ERROR: path is outside the repo, or is not a file: %r" % args.get("path")
    offset = max(1, int(args.get("offset", 1)))
    limit = min(4000, max(1, int(args.get("limit", 400))))
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            lines = fh.readlines()
    except OSError as exc:
        return "ERROR: %s" % exc
    out = []
    for i, line in enumerate(lines[offset - 1: offset - 1 + limit], start=offset):
        out.append("%5d  %s" % (i, line.rstrip("\n")))
    body = "\n".join(out) or "(no lines in that range)"
    return _truncate("FILE %s  (%d lines total)\n%s" % (path, len(lines), body))


def tool_grep(args):
    pattern = args.get("pattern", "")
    path = _resolve(args.get("path", "")) or ROOT
    if not os.path.exists(path):
        return "ERROR: no such path: %r" % args.get("path")
    cmd = ["rg", "-n", "--no-heading", "-m", "200"]
    if args.get("include"):
        cmd += ["-g", str(args["include"])]
    cmd += [pattern, path]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=SHELL_TIMEOUT)
    except subprocess.TimeoutExpired:
        return "ERROR: grep timed out"
    body = proc.stdout or "(no matches)"
    if proc.returncode not in (0, 1):
        body += "\nSTDERR: " + proc.stderr[:2000]
    return _truncate(body)


def tool_glob(args):
    pattern = args.get("pattern", "")
    base = _resolve(args.get("path", ROOT)) or ROOT
    matches = []
    for dirpath, dirnames, filenames in os.walk(base):
        dirnames[:] = [d for d in dirnames if d not in (".git", "node_modules", "__pycache__")]
        for name in filenames:
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, base)
            if _glob_match(pattern, rel) or _glob_match(pattern, name):
                matches.append(rel)
                if len(matches) >= 300:
                    break
        if len(matches) >= 300:
            break
    return _truncate("\n".join(sorted(matches)) or "(no matches)")


def _glob_match(pattern, text):
    import fnmatch
    if fnmatch.fnmatch(text, pattern):
        return True
    # "**/x" should also match a bare "x"
    if pattern.startswith("**/") and fnmatch.fnmatch(text, pattern[3:]):
        return True
    return False


def tool_shell(args):
    cmd = (args.get("cmd") or "").strip()
    if not cmd:
        return "ERROR: empty cmd"
    if SHELL_DENY.search(cmd):
        return ("REFUSED: that command can modify the tree or grab the device. "
                "This session is read-only apart from write_note/write_file.")
    if not SHELL_ALLOW.match(cmd):
        return ("REFUSED: command must start with one of: ls cat head tail sed awk grep rg wc "
                "find file stat md5sum sha256sum git(log/show/diff/status/cat-file/ls-tree) "
                "python3 sh-n du sort uniq cut tr")
    try:
        proc = subprocess.run(cmd, shell=True, capture_output=True, text=True,
                              timeout=SHELL_TIMEOUT, cwd=ROOT)
    except subprocess.TimeoutExpired:
        return "ERROR: timed out after %ds" % SHELL_TIMEOUT
    body = (proc.stdout or "") + (("\nSTDERR: " + proc.stderr) if proc.stderr else "")
    return _truncate(body or "(no output, exit %d)" % proc.returncode)


def tool_write_note(args):
    text = (args.get("text") or "").strip()
    if not text:
        return "ERROR: empty text"
    kind = (args.get("kind") or "NOTE").upper()
    title = (args.get("title") or "untitled").strip()
    stamp = time.strftime("%Y-%m-%d %H:%M")
    block = "\n## [%s] %s — %s\n状态: open\n\n%s\n\n---\n" % (kind, stamp, title, text)
    os.makedirs(os.path.dirname(BRIDGE_IN), exist_ok=True)
    with open(BRIDGE_IN, "a", encoding="utf-8") as fh:
        fh.write(block)
    return "note appended to bridge/to-dsh-from-gemini.md (%d chars)" % len(block)


def tool_write_file(args):
    path = _resolve(args.get("path", ""))
    content = args.get("content")
    if content is None:
        return "ERROR: content required"
    if not path:
        return "ERROR: path outside the repo"
    if not path.startswith(os.path.realpath(WIP) + os.sep):
        return "ERROR: Gemini may only write under docs/headset-wip/"
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(content)
    return "wrote %s (%d bytes)" % (path, len(content.encode()))


TOOLS = {
    "read_file": (tool_read_file,
                  "Read a UTF-8 text file inside the repo. Returns line-numbered content.",
                  {"path": "repo-relative or absolute path",
                   "offset": "first line, 1-based (optional)",
                   "limit": "max lines (optional, default 400)"}),
    "grep": (tool_grep,
             "Search file contents with ripgrep. Use this instead of shell grep.",
             {"pattern": "regex", "path": "dir or file (optional)",
              "include": "glob filter like *.c (optional)"}),
    "glob": (tool_glob,
             "Find files by glob pattern.",
             {"pattern": "e.g. **/*.md", "path": "base dir (optional)"}),
    "shell": (tool_shell,
              "Run a READ-ONLY shell command in the repo root. Destructive commands are refused.",
              {"cmd": "command line"}),
    "write_note": (tool_write_note,
                   "Append a message to the bridge file the DSH controller polls. "
                   "Use kind in {ASK,DONE,BLOCK,OBJ,DEV,NOTE}.",
                   {"text": "body", "kind": "ASK|DONE|BLOCK|OBJ|DEV|NOTE",
                    "title": "one-line title"}),
    "write_file": (tool_write_file,
                   "Write a file under docs/headset-wip/ only.",
                   {"path": "path", "content": "full content"}),
}

DECLS = [{
    "name": name,
    "description": fn.__doc__ or desc,
    "parameters": {
        "type": "OBJECT",
        "properties": {k: {"type": "STRING", "description": v} for k, v in props.items()},
        "required": [k for k in props if "optional" not in props[k]],
    },
} for name, (fn, desc, props) in TOOLS.items()]


# ---------------------------------------------------------------- transport

def call_model(api_key, model, contents, system_text, retries=4):
    payload = {
        "contents": contents,
        "tools": [{"functionDeclarations": DECLS}],
        "systemInstruction": {"parts": [{"text": system_text}]},
        "generationConfig": {"temperature": 0.7, "maxOutputTokens": 32768},
    }
    body = json.dumps(payload).encode()
    url = ENDPOINT.format(model=model)
    delay = 3
    last = ""
    for attempt in range(retries):
        req = urllib.request.Request(url, data=body, method="POST", headers={
            "Content-Type": "application/json", "X-goog-api-key": api_key})
        try:
            with urllib.request.urlopen(req, timeout=180) as resp:
                return json.loads(resp.read().decode())
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode(errors="replace")[:800]
            last = "HTTP %s: %s" % (exc.code, detail)
            if exc.code in (429, 500, 502, 503, 504):
                time.sleep(delay)
                delay = min(delay * 2, 45)
                continue
            raise SystemExit("model call failed: " + last)
        except (urllib.error.URLError, TimeoutError) as exc:
            last = "network: %s" % exc
            time.sleep(delay)
            delay = min(delay * 2, 45)
    raise SystemExit("model call failed after %d attempts: %s" % (retries, last))


def parts_of(resp):
    cands = resp.get("candidates") or []
    if not cands:
        fb = resp.get("promptFeedback", {})
        raise SystemExit("no candidates (promptFeedback=%s)" % json.dumps(fb)[:400])
    return cands[0].get("content", {}).get("parts", []) or []


def text_of(parts):
    return "".join(p.get("text", "") for p in parts if "text" in p)


# ---------------------------------------------------------------- agent loop

def run(api_key, model, contents, system_text, rounds, quiet=False):
    transcript = []
    for rnd in range(rounds + 1):
        resp = call_model(api_key, model, contents, system_text)
        parts = parts_of(resp)
        calls = [p["functionCall"] for p in parts if "functionCall" in p]
        said = text_of(parts)
        if said.strip():
            transcript.append(said)
            if not quiet:
                print(said, flush=True)
        if not calls:
            return "\n".join(transcript), contents

        contents.append({"role": "model", "parts": parts})
        results = []
        for call in calls:
            name = call.get("name")
            args = call.get("args") or {}
            fn = TOOLS.get(name, (None,))[0]
            if fn is None:
                out = "ERROR: unknown tool %r" % name
            else:
                try:
                    out = fn(args)
                except Exception as exc:                    # noqa: BLE001
                    out = "ERROR: %s: %s" % (type(exc).__name__, exc)
            if not quiet:
                print("  · %s -> %d chars" % (name, len(out)), file=sys.stderr, flush=True)
            results.append({"functionResponse": {"name": name,
                                                 "response": {"result": out}}})
        contents.append({"role": "user", "parts": results})
    return "\n".join(transcript) + "\n[stopped: tool-round limit reached]", contents


# ---------------------------------------------------------------- session

def session_path(name):
    os.makedirs(STATE_DIR, exist_ok=True)
    return os.path.join(STATE_DIR, "%s.json" % re.sub(r"[^A-Za-z0-9_.-]", "_", name))


def load_session(name):
    p = session_path(name)
    if os.path.isfile(p):
        with open(p, "r", encoding="utf-8") as fh:
            return json.load(fh)
    return {"contents": []}


def save_session(name, contents):
    with open(session_path(name), "w", encoding="utf-8") as fh:
        json.dump({"contents": contents, "updated": time.strftime("%Y-%m-%d %H:%M:%S")},
                  fh, ensure_ascii=False)


SYSTEM = """You are Gemini 4 Hydrogen, joining the HakiOS project as a third participant.

The project: port Apple's XNU kernel to a Xiaomi xaga (MediaTek MT6895, Mali-G610 CSF).
Two goals are live — 3.5 mm headset detection through the MT6368 PMIC (SPMI slave 5),
and bringing up the Mali CSF MCU. A third workstream is the loader's logging, which
currently loses the kernel log on a black screen.

Who else is here:
- DSH (the controller) dispatches research subagents, owns the docs repo, verifies
  every claim by re-running it, and is the only one who talks to the phone for
  read-only forensics.
- Codex 6.1 Sol owns all production code in /home/lizi/IOS/hakios-mtk and does the
  on-device flashing experiments.

Your seat: adversarial reviewer and second implementer. The controller is fast but
has been wrong in ways that cost real time — it misread a MMU interrupt bit, it
thought ACCDET converted ADC counts to ohms, and it quoted 6 register writes for a
70-write audio path. Your value is catching that class of error before it reaches
the phone. Read primary sources. Prefer "no evidence" over a plausible guess.

House rules, non-negotiable:
- Report only numbers and evidence boundaries. Every claim needs file:line or a
  command output. Never write "should work", "probably", "theoretically".
- Label anything unverified as unverified. Distinguish "read from vendor source",
  "measured on device", and "inferred from reading code".
- /home/lizi/IOS/hakios-mtk must never be modified by you — it holds ~50
  uncommitted changes that are load-bearing. Read only.
- Never touch Android's spmi_access sysfs (it has bricked the device twice).
- Never read physical address 0x13c40000 (GPUEB; gating unconfirmed).
- PMIF accesses stay inside 0x1c804000 + 0x880..0x8a8.
- The user has no headset, so nothing may claim to have verified insertion.

Your tools are read-only except write_note and write_file. Use write_note with
kind ASK/DONE/BLOCK/OBJ to reach the controller; it polls that file. Be concise
there — one screen maximum. Use write_file to draft reports under docs/headset-wip/.

Start by reading docs/headset-wip/MASTER-STATE.md and docs/headset-wip/COLLAB-PROTOCOL.md.
"""


def main():
    ap = argparse.ArgumentParser(description="Gemini 4 as a project participant")
    ap.add_argument("--ask", help="one-shot question")
    ap.add_argument("--task", help="task for the agent loop")
    ap.add_argument("--rounds", type=int, default=10, help="max tool rounds (default 10)")
    ap.add_argument("--session", default="main", help="history file name")
    ap.add_argument("--reset", action="store_true", help="drop this session's history")
    ap.add_argument("--show-session", action="store_true", help="print stored history size")
    ap.add_argument("--model", default=os.environ.get("GEMINI_MODEL", DEFAULT_MODEL))
    ap.add_argument("--quiet", action="store_true", help="suppress streamed text")
    args = ap.parse_args()

    if args.show_session:
        s = load_session(args.session)
        print(json.dumps({"session": args.session, "turns": len(s["contents"]),
                          "bytes": len(json.dumps(s["contents"]))}, indent=2))
        return 0

    api_key = os.environ.get("GEMINI_API_KEY", "").strip()
    if not api_key:
        key_file = os.path.expanduser("~/.config/gemini-dev/api-key")
        if os.path.isfile(key_file):
            with open(key_file, "r", encoding="utf-8") as fh:
                api_key = fh.read().strip()
    if not api_key:
        raise SystemExit("no API key: set GEMINI_API_KEY or write ~/.config/gemini-dev/api-key")

    if args.reset:
        p = session_path(args.session)
        if os.path.isfile(p):
            os.remove(p)
        print("session %r dropped" % args.session)
        return 0

    prompt = args.task or args.ask
    if not prompt:
        raise SystemExit("give --ask or --task")

    sess = load_session(args.session)
    contents = sess["contents"]
    contents.append({"role": "user", "parts": [{"text": prompt}]})

    rounds = args.rounds if args.task else 0
    answer, contents = run(api_key, args.model, contents, SYSTEM, rounds, args.quiet)
    save_session(args.session, contents)

    if args.quiet:
        print(answer)
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Scope/permission enforcement for the loop (instantiated to .loop/check_scope.py).

Compares everything changed since the loop's origin HEAD against the
machine-readable policy in .loop/policy.json.

Exit codes:  0 = within policy (or unenforceable — warned, not blocked)
             2 = violation found (caller should set status=blocked,
                 stop_reason=blocked_permission_required)

Path rules: an entry ending in "/" is a prefix match; anything else is an
fnmatch glob. deny_paths is checked first; if allow_paths is non-empty, every
changed path must also match it. Paths under .loop/ are always exempt (the
loop's own bookkeeping). Limitation: enforcement needs git; without a repo we
can only warn, and the worker's self-check is the remaining line of defense.
"""
import fnmatch
import json
import pathlib
import subprocess
import sys

POLICY = pathlib.Path(".loop/policy.json")
STATE = pathlib.Path(".loop/state.json")


def sh(*args):
    return subprocess.run(args, capture_output=True, text=True)


def match(path: str, pattern: str) -> bool:
    if pattern.endswith("/"):
        return path.startswith(pattern)
    return fnmatch.fnmatch(path, pattern)


def main() -> int:
    if not POLICY.exists():
        print("[scope] no .loop/policy.json — nothing to enforce, skipping")
        return 0
    scope = json.loads(POLICY.read_text(encoding="utf-8")).get("scope", {})
    allow = scope.get("allow_paths", []) or []
    deny = scope.get("deny_paths", []) or []

    if sh("git", "rev-parse", "--is-inside-work-tree").returncode != 0:
        print("[scope] WARNING: not a git repo — cannot enforce scope here")
        return 0

    base = ""
    try:
        base = (json.loads(STATE.read_text(encoding="utf-8")).get("git") or {}).get("origin_head") or ""
    except Exception:
        pass
    if not base:
        base = "HEAD"
        print("[scope] WARNING: no git.origin_head in state.json — "
              "checking against HEAD only (committed iterations not covered)")

    changed = set()
    r = sh("git", "diff", "--name-only", base)
    if r.returncode != 0:
        # A bad base must not fail open as "0 changed paths".
        print(f"[scope] WARNING: git diff against '{base}' failed "
              f"({(r.stderr or '').strip() or 'unknown error'}) — falling back to HEAD")
        r = sh("git", "diff", "--name-only", "HEAD")
        if r.returncode != 0:
            print("[scope] ERROR: git diff failed even against HEAD — "
                  "cannot enforce scope; failing closed")
            return 2
    changed |= {l.strip() for l in r.stdout.splitlines() if l.strip()}
    r = sh("git", "status", "--porcelain")
    for line in r.stdout.splitlines():
        if line.startswith("??"):
            changed.add(line[3:].strip())

    violations = []
    for p in sorted(changed):
        if p.startswith(".loop/"):
            continue
        if any(match(p, d) for d in deny):
            violations.append((p, "deny_paths"))
            continue
        if allow and not any(match(p, a) for a in allow):
            violations.append((p, "outside allow_paths"))

    if violations:
        print("[scope] VIOLATIONS:")
        for p, why in violations:
            print(f"  {p}  ({why})")
        return 2
    print(f"[scope] OK — {len(changed)} changed path(s) within policy")
    return 0


if __name__ == "__main__":
    sys.exit(main())

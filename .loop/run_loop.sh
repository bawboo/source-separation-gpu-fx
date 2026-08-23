#!/usr/bin/env bash
# .loop/run_loop.sh — external loop driver (Engine B, Ralph-style).
# Instantiated for melband-roformer-integration (Windows Git Bash).
# Customizations vs the stock template:
#   1. $PY shim — anaconda `python` (the WindowsApps python3 alias is unreliable).
#   2. usage-limit retry — if the agent output matches a usage/rate-limit
#      pattern and the iteration counter did not advance, sleep 20 minutes and
#      retry WITHOUT counting a failed launch (auto-continue across the 5-hour
#      usage window resets, per LOOP_PLAN §9).
# Run from the PROJECT ROOT (the transfer tree):  bash .loop/run_loop.sh
# Pilot first:  MAX_LAUNCHES=1 bash .loop/run_loop.sh
# Walk-away:    nohup bash .loop/run_loop.sh > .loop/nohup.out 2>&1 &
# Rerunning later resumes from .loop/state.json.
set -uo pipefail

# ── CONFIG (from LOOP_PLAN.md §9) ───────────────────────────────────────────
# Engine CLI: claude headless (user logged in 2026-08-23).
# codex alternative (weekly usage limit resets 2026-08-27 17:07):
#   ["C:\\nvm4w\\nodejs\\node_modules\\@openai\\codex\\node_modules\\@openai\\codex-win32-x64\\vendor\\x86_64-pc-windows-msvc\\bin\\codex.exe","exec","--skip-git-repo-check","--dangerously-bypass-approvals-and-sandbox"]
AGENT_CMD_JSON=${AGENT_CMD_JSON:-'["C:\\nvm4w\\nodejs\\node_modules\\@anthropic-ai\\claude-code\\bin\\claude.exe","-p","--permission-mode","acceptEdits","--allowedTools","Bash,Edit,Write,Read,Glob,Grep,Task,WebFetch,WebSearch"]'}
# PROMPT_MODE=ref passes only a one-line bootstrap on the command line (cmd.exe
# has an 8191-char limit — the full prompt is read from the file by the agent).
PROMPT_MODE=${PROMPT_MODE:-ref}
MAX_WALL_MINUTES=${MAX_WALL_MINUTES:-0}
ITERATION_TIMEOUT_MINUTES=${ITERATION_TIMEOUT_MINUTES:-40}
MAX_LAUNCHES=${MAX_LAUNCHES:-0}
SLEEP_SECONDS=${SLEEP_SECONDS:-10}
USAGE_LIMIT_SLEEP_SECONDS=${USAGE_LIMIT_SLEEP_SECONDS:-1200}
LOOP_DIR=".loop"
PROMPT_FILE="$LOOP_DIR/ITERATION_PROMPT.md"
LOG_FILE="$LOOP_DIR/driver.log"
LASTRUN_FILE="$LOOP_DIR/lastrun.log"
# ────────────────────────────────────────────────────────────────────────────

# ── Python resolver: prefer a working python3, else anaconda python ──
PY=python3
if ! command -v python3 >/dev/null 2>&1 || ! python3 -c "import sys" >/dev/null 2>&1; then
  PY=python
fi
command -v "$PY" >/dev/null 2>&1 || {
  echo "[driver] FATAL: no working python found ($PY) — required by this driver." >&2; exit 1; }

for _v in MAX_WALL_MINUTES ITERATION_TIMEOUT_MINUTES MAX_LAUNCHES SLEEP_SECONDS USAGE_LIMIT_SLEEP_SECONDS; do
  eval "_val=\$$_v"
  case "$_val" in
    ''|*[!0-9]*) echo "[driver] FATAL: $_v must be a non-negative integer, got '$_val'." >&2; exit 1;;
  esac
done
[ -d "$LOOP_DIR" ] || {
  echo "[driver] FATAL: $LOOP_DIR/ not found — run from the project root." >&2; exit 1; }

log() { echo "[driver] $*" | tee -a "$LOG_FILE"; }

LOCK_DIR="$LOOP_DIR/driver.lockdir"
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
  echo "[driver] FATAL: another driver appears to be running (lock: $LOCK_DIR)." >&2
  echo "[driver] If no driver is running, remove the stale lock: rmdir $LOCK_DIR" >&2
  exit 1
fi
cleanup() { rmdir "$LOCK_DIR" 2>/dev/null || true; }
trap cleanup EXIT
trap 'exit 130' INT TERM

field() {  # field <key> — read a top-level key from state.json ("" if absent)
"$PY" - "$1" <<'PYEOF'
import json, sys
try:
    d = json.load(open(".loop/state.json", encoding="utf-8"))
    v = d.get(sys.argv[1], "")
    print(v if v is not None else "")
except Exception:
    print("")
PYEOF
}

set_state() {  # set_state <status> <stop_reason> — atomic tmp+rename write
"$PY" - "$1" "$2" <<'PYEOF'
import datetime, json, os, pathlib, sys
p = pathlib.Path(".loop/state.json")
d = json.loads(p.read_text(encoding="utf-8"))
d["status"] = sys.argv[1]
d["stop_reason"] = sys.argv[2]
d["updated_at"] = datetime.datetime.now().astimezone().isoformat(timespec="seconds")
tmp = p.with_name(p.name + ".tmp")
tmp.write_text(json.dumps(d, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
os.replace(tmp, p)
PYEOF
log "state.json → status=$1 stop_reason=$2"
}

run_agent() {  # one fresh-context iteration; exit code = the agent's
"$PY" - "$AGENT_CMD_JSON" "$PROMPT_FILE" "$PROMPT_MODE" "$ITERATION_TIMEOUT_MINUTES" <<'PYEOF'
import json, os, pathlib, signal, subprocess, sys
cmd = json.loads(sys.argv[1])
prompt = pathlib.Path(sys.argv[2]).read_text(encoding="utf-8")
mode = sys.argv[3]
timeout = int(sys.argv[4] or "0") * 60
BOOTSTRAP = ("Read the file .loop/ITERATION_PROMPT.md in the current directory and "
             "follow it exactly: execute exactly ONE loop iteration, then stop "
             "with the LOOP STATUS line.")
if mode == "ref":
    # insert right after ["<exe>", "-p"] so variadic flags (--allowedTools)
    # can never swallow the positional prompt
    cmd = cmd[:2] + [BOOTSTRAP] + cmd[2:]
elif mode != "stdin":
    cmd = cmd[:2] + [prompt] + cmd[2:]
kw = {"text": True, "encoding": "utf-8"}
if os.name == "posix":
    kw["start_new_session"] = True
if mode == "stdin":
    kw["stdin"] = subprocess.PIPE

def kill_agent(p):
    if os.name != "posix":
        # Windows: kill the whole process tree (cmd shim -> node -> children)
        try:
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(p.pid)],
                           capture_output=True)
            p.wait(timeout=10)
            return
        except Exception:
            pass
    sigs = [signal.SIGTERM]
    if hasattr(signal, "SIGKILL"):
        sigs.append(signal.SIGKILL)
    for sig in sigs:
        try:
            if os.name == "posix":
                os.killpg(os.getpgid(p.pid), sig)
            else:
                p.kill()
            p.wait(timeout=10)
            break
        except Exception:
            continue

def _sigterm(_sig, _frame):
    raise KeyboardInterrupt

signal.signal(signal.SIGTERM, _sigterm)
p = subprocess.Popen(cmd, **kw)
try:
    p.communicate(input=prompt if mode == "stdin" else None,
                  timeout=timeout if timeout > 0 else None)
    raise SystemExit(p.returncode)
except subprocess.TimeoutExpired:
    kill_agent(p)
    print("[driver] hang guard: agent exceeded the iteration timeout and was killed",
          file=sys.stderr)
    raise SystemExit(124)
except KeyboardInterrupt:
    kill_agent(p)
    print("[driver] interrupted — agent process killed, state left as-is (rerun resumes)",
          file=sys.stderr)
    raise SystemExit(130)
PYEOF
}

final_report() {
"$PY" - <<'PYEOF'
import glob, json, pathlib
st = json.loads(pathlib.Path(".loop/state.json").read_text(encoding="utf-8"))
lines = ["# FINAL REPORT — loop run", "",
         f"- status: **{st.get('status')}**",
         f"- stop_reason: `{st.get('stop_reason')}`",
         f"- iterations: {st.get('iteration')} / max {st.get('max_iterations')}",
         f"- best_metric: {st.get('best_metric')}",
         f"- started_at: {st.get('started_at')}  ·  updated_at: {st.get('updated_at')}"]
git = st.get("git") or {}
if git:
    lines.append(f"- git: origin `{git.get('origin_branch')}`@`{git.get('origin_head')}`"
                 f" → loop branch `{git.get('loop_branch')}` (switch back / merge is your call)")
recs = sorted(glob.glob(".loop/iterations/*.json"))
files = set()
if recs:
    lines += ["", "| iter | decision | status | metric | criteria |", "|---|---|---|---|---|"]
    for rp in recs:
        try:
            r = json.loads(pathlib.Path(rp).read_text(encoding="utf-8"))
        except Exception:
            continue
        crit = " ".join(f"{k}:{v}" for k, v in (r.get("criteria") or {}).items())
        met = (r.get("metrics") or {}).get("value")
        lines.append(f"| {r.get('iteration')} | {r.get('decision')} | {r.get('status')} | {met} | {crit} |")
        files.update(r.get("files_touched") or [])
if files:
    lines += ["", "Files touched across the run:", ""]
    lines += [f"- `{f}`" for f in sorted(files)]
lines += ["", "Evidence and per-iteration narrative: `.loop/journal.md`",
          "Structured records: `.loop/iterations/*.json`"]
pathlib.Path(".loop/FINAL_REPORT.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
print("[driver] wrote .loop/FINAL_REPORT.md")
PYEOF
}

[ -f "$LOOP_DIR/state.json" ] || { log "FATAL: $LOOP_DIR/state.json missing"; exit 1; }
[ -f "$PROMPT_FILE" ]        || { log "FATAL: $PROMPT_FILE missing"; exit 1; }

start_ts=$(date +%s)
wall_desc="disabled"; [ "$MAX_WALL_MINUTES" -gt 0 ] && wall_desc="${MAX_WALL_MINUTES}m"
iter_to_desc="disabled"; [ "$ITERATION_TIMEOUT_MINUTES" -gt 0 ] && iter_to_desc="${ITERATION_TIMEOUT_MINUTES}m"
log "start $(date +%Y-%m-%dT%H:%M:%S%z)  AGENT_CMD_JSON='$AGENT_CMD_JSON'  prompt_mode=$PROMPT_MODE  wall=$wall_desc  iter_timeout=$iter_to_desc  max_launches=$MAX_LAUNCHES  py=$PY"

failed_launches=0
launches=0
pilot_stop=0
while true; do
  status=$(field status)
  iter=$(field iteration)
  maxit=$(field max_iterations)

  if [ -f "$LOOP_DIR/STOP" ]; then
    rm -f "$LOOP_DIR/STOP"
    if [ "$status" = "running" ]; then
      set_state "aborted" "manual_abort"
      log "stop: $LOOP_DIR/STOP detected — manual abort"
      break
    fi
    log "stale $LOOP_DIR/STOP removed (status='$status')"
  fi
  if [ "$status" != "running" ]; then
    log "stop: agent set status='$status' (stop_reason=$(field stop_reason)) after $iter iterations"
    break
  fi
  if [ -n "$maxit" ] && [ -n "$iter" ] && [ "$iter" -ge "$maxit" ] 2>/dev/null; then
    set_state "max_iterations" "iteration_budget_exhausted"
    log "stop: iteration $iter >= max_iterations $maxit (ceiling enforced by driver)"
    break
  fi
  if [ "$MAX_WALL_MINUTES" -gt 0 ]; then
    elapsed_min=$(( ( $(date +%s) - start_ts ) / 60 ))
    if [ "$elapsed_min" -ge "$MAX_WALL_MINUTES" ]; then
      set_state "aborted" "max_wall_time"
      log "stop: wall-clock ceiling ${MAX_WALL_MINUTES}m reached at iteration $iter"
      break
    fi
  fi

  log "iteration $((iter + 1)) launching  $(date +%Y-%m-%dT%H:%M:%S%z)"
  run_agent 2>&1 | tee "$LASTRUN_FILE" | tee -a "$LOG_FILE"
  rc=${PIPESTATUS[0]}
  launches=$(( launches + 1 ))
  log "agent process exited rc=$rc"
  [ "$rc" -eq 124 ] && log "WARNING: agent exceeded ITERATION_TIMEOUT_MINUTES=${ITERATION_TIMEOUT_MINUTES} and was killed (hang guard)"

  # ── usage-limit auto-continue (LOOP_PLAN §9): if the agent hit the Claude
  #    usage/rate limit and made no progress, sleep and retry — not a failure ──
  if [ "$(field iteration)" = "$iter" ] && \
     grep -qiE "usage limit|rate.?limit|limit reached|resets? (at|in)|overloaded_error|too many requests" "$LASTRUN_FILE"; then
    log "usage/rate limit detected — sleeping ${USAGE_LIMIT_SLEEP_SECONDS}s then retrying (not counted as a failed launch)"
    if [ "$MAX_LAUNCHES" -gt 0 ] && [ "$launches" -ge "$MAX_LAUNCHES" ]; then
      pilot_stop=1
      log "pilot stop during usage-limit wait: MAX_LAUNCHES=$MAX_LAUNCHES reached"
      break
    fi
    sleep "$USAGE_LIMIT_SLEEP_SECONDS"
    continue
  fi

  if [ -f "$LOOP_DIR/check_scope.py" ]; then
    if ! "$PY" "$LOOP_DIR/check_scope.py" 2>&1 | tee -a "$LOG_FILE"; then
      if [ "$(field status)" = "running" ]; then
        set_state "blocked" "blocked_permission_required"
        log "stop: scope violation detected by check_scope.py — user approval required"
        break
      fi
    fi
  fi

  new_iter=$(field iteration)
  if [ "$new_iter" = "$iter" ]; then
    failed_launches=$(( failed_launches + 1 ))
    log "WARNING: iteration counter did not advance (failed launch #$failed_launches, rc=$rc)"
    if [ "$failed_launches" -ge 2 ]; then
      set_state "blocked" "stalled_no_state_update"
      log "stop: two consecutive launches produced no state update — check agent auth/permissions"
      break
    fi
  else
    failed_launches=0
  fi

  if [ "$MAX_LAUNCHES" -gt 0 ] && [ "$launches" -ge "$MAX_LAUNCHES" ]; then
    pilot_stop=1
    log "pilot stop: MAX_LAUNCHES=$MAX_LAUNCHES reached — state untouched (status=$(field status)); rerun to continue, nohup/tmux for walk-away"
    break
  fi

  sleep "$SLEEP_SECONDS"
done

if [ "$pilot_stop" = "1" ]; then
  log "pilot done $(date +%Y-%m-%dT%H:%M:%S%z) — FINAL_REPORT skipped (run not terminated; status=$(field status)). Check $LOOP_DIR/iterations/, $LOOP_DIR/journal.md and the checkpoint commit, then relaunch."
else
  final_report
  log "done $(date +%Y-%m-%dT%H:%M:%S%z). Final: status=$(field status) stop_reason=$(field stop_reason). See $LOOP_DIR/FINAL_REPORT.md and $LOOP_DIR/journal.md"
fi

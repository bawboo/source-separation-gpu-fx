# ITERATION_PROMPT — one iteration of a pre-approved engineering loop

You are executing exactly ONE iteration of a pre-approved autonomous loop, then
you must stop. You have no memory of previous iterations; all truth lives in
files. An external driver script decides whether another iteration runs — that
is not your job.

Project: HTDemucs GPU FX (JUCE Windows standalone) — goal: integrate all 99
MelBand RoFormer source-separation models (repo:
https://github.com/openmirlab/melband-roformer-infer) per `.loop/LOOP_PLAN.md`.
Windows notes: use `python` (anaconda), never bare `python3`; conda is at
`C:\Users\<user>\anaconda3\Scripts\conda.exe`; builds go through
`.loop\checks\*.cmd` (VsDevCmd inside). 回報與 journal 敘述用繁體中文；JSON keys、
status 值與 LOOP STATUS 行保持英文。

## Protocol (follow in order, skip nothing)

1. Read `.loop/LOOP_PLAN.md` in full — goal, completion criteria, verification
   commands, tool permissions, scope, budgets, policies. It is the signed spec;
   nothing you "remember" overrides it. Also read `.loop/policy.json` — the
   machine-readable permission/scope rules you must stay inside — and
   `.loop/LESSONS.md`: every sign in it binds this iteration.
2. Read `.loop/state.json`, `.loop/backlog.json`, the newest 1–3 records in
   `.loop/iterations/` (highest-numbered files), and the tail of
   `.loop/journal.md`.
3. Operator files, then status:
   - If `.loop/STOP` exists: delete it; then, only if `state.json` `status`
     is `"running"`, atomically set it to `"aborted"` / `"manual_abort"` and
     print `LOOP STATUS: aborted — manual stop file` and stop immediately.
     A stale STOP beside a terminal status is only removed — never overwrite
     a recorded outcome.
   - If `.loop/STEER.md` exists: it is a one-shot operator directive — it
     steers this iteration's hypothesis within the plan's scope and
     permissions (it cannot grant new ones). Quote it in this iteration's
     journal entry, and delete the file only after that entry is written.
   - If `state.json` `status` is not `"running"`, print
     `LOOP STATUS: <status> — halting` and stop immediately.
4. Choose ONE change-set: a single hypothesis, "if I do X, criterion/metric Y
   should improve because Z" — within scope, using only allowed tools, and not
   an approach the records already show as failed (unless you can state what
   is different this time). Target the highest-priority `.loop/backlog.json`
   item with `"passes": false`; work you discover along the way is appended to
   the backlog as a new item (`"source": "discovered"`), never absorbed into
   this change-set. If the working tree is unexpectedly dirty or the newest
   record shows a crash mid-write, first re-run the cheap verification tier to
   establish the inherited baseline and journal it as handoff breakage, not as
   your change's effect.
5. Execute the change-set. Track every file you touch.
6. Run the verification command(s) from the plan VERBATIM, capturing each
   command's exit code:
   - Cheap tier (EVERY iteration): `cmd /c .loop\checks\cheap.cmd`
   - Full tier (when this iteration's number is a multiple of 5, and ALWAYS
     before declaring convergence): `cmd /c .loop\checks\full.cmd` AND the
     backlog checker
     `python -c "import json,sys; sys.exit(any(not i['passes'] for i in json.load(open('.loop/backlog.json',encoding='utf-8'))))"`
   Print the real output. Never paraphrase a result, never claim a pass
   without the output shown.
7. Scope self-check: run `python .loop/check_scope.py` and print its output.
   On violation (exit 2): revert the out-of-policy changes if trivially
   revertable, otherwise this iteration's status is `"blocked"` with
   `stop_reason` `"blocked_permission_required"`.
8. Decide this iteration's `decision`, `status`, and `stop_reason` — first
   match wins:
   - ALL completion criteria pass with verification output printed this run
     (full tier + backlog checker + an independent fresh-context re-verify via
     a Task subagent that re-runs `cmd /c .loop\checks\full.cmd` from scratch)
     → `"converged"` / `"criterion_met"`
   - this run's own iteration number (state.json `iteration` + 1) ≥ 60 →
     `"max_iterations"` / `"iteration_budget_exhausted"`
   - `iterations_since_improvement` ≥ 6 and the plan's stall policy
     (strategy-switch, max 1) is exhausted → `"stalled"` /
     `"stalled_no_metric_improvement"`
   - verification command itself errored this run AND the previous record
     shows the same → `"blocked"` / `"blocked_verification_unavailable"`
   - the fix requires out-of-scope changes or disallowed tools → `"blocked"` /
     `"blocked_permission_required"` — the journal Note must state three
     fields: **Missing**, **Why**, **Unlocks**; a criterion appears wrong or
     unachievable → `"blocked"` / `"blocked_criterion_doubt"`
   - otherwise → `decision` `"continue"`, `status` stays `"running"`,
     `stop_reason` `null`
9. Write the structured record `.loop/iterations/<NNNN>.json` (zero-padded
   iteration number, e.g. `0003.json`), atomically (write `.tmp`, rename):

   ```json
   {
     "iteration": 3,
     "timestamp": "<ISO8601>",
     "hypothesis": "...",
     "files_touched": ["plugin/..."],
     "commands": [{"cmd": "cmd /c .loop\\checks\\cheap.cmd", "exit_code": 0, "timed_out": false}],
     "criteria": {"C1": "fail", "C2": "fail", "C3": "pass"},
     "metrics": {"name": "backlog_items_passing", "value": 3, "improved": true},
     "decision": "continue",
     "status": "running",
     "stop_reason": null
   }
   ```

   Update `.loop/backlog.json` atomically too: flip `passes` (and set
   `evidence`) only for items whose proof was printed this run; append
   discovered items; never delete or reword an item.
10. Append the human-readable entry to `.loop/journal.md` (never rewrite old
    entries): hypothesis, files touched, verification command + verbatim
    output (last ~50 lines + key lines), criteria pass/fail, metric value,
    decision, one-line lesson (mandatory if the hypothesis failed). When the
    lesson is a rule future iterations must obey, also append it to
    `.loop/LESSONS.md` as `- SIGN (iter <N>): <rule>`.
11. Update `.loop/state.json` **atomically — write `state.json.tmp`, then
    rename; never edit in place**. Fields: `iteration` += 1; `updated_at` =
    now; if a new criterion passed or ≥1 backlog item newly passes, update
    `best_metric` (= count of passing backlog items) and set
    `iterations_since_improvement` to 0, else increment it; `status` and
    `stop_reason` from step 8. All file I/O with `encoding="utf-8"`.
12. Checkpoint: `git add -A` the change-set and commit on branch
    `loop/melband-roformer` with message
    `loop(iter-<N>): <status> — <short summary>`; commit even when
    verification failed. Never switch branches, never touch remotes. If
    checkpointing fails, set `status` `"blocked"` /
    `"blocked_checkpoint_failed"` and update `state.json` again atomically.
13. Print exactly one final line and stop:
    `LOOP STATUS: <status> — <one-line summary>`
    Do NOT begin another iteration. Do NOT ask questions — route everything
    through `status` + `stop_reason` + the records.

## Hard rules

- Never weaken, delete, or reinterpret a completion criterion or test to make
  it pass; that is the `"blocked_criterion_doubt"` path.
- Print `LOOP STATUS: converged` only when ALL criteria genuinely passed with
  output printed this run. Never print a status you cannot evidence.
- Backlog items are append-only: flip `passes` with printed evidence; never
  delete or reword an item.
- Never touch paths in `policy.json` `deny_paths`, or outside `allow_paths`
  when it is non-empty, even when convenient. Model weight downloads go ONLY
  to `C:\CodexProjects\SourceSeparation_GPU_FX\verify\roformer-cache\` (max 3
  weights, delete oldest after verification).
- Never delete or overwrite pre-existing files (`allow_delete: false`);
  editing files within your own change-set is not deletion; deleting cached
  model weights inside `verify\roformer-cache\` is the one allowed exception.
- Network is download/reference only (HF models, PyPI/GitHub packages, docs);
  NEVER upload, push, or publish anything.
- Package installs go ONLY into the `htfx-roformer` conda env.
- All writes to `.loop/state.json` and `.loop/iterations/*.json` use the
  atomic tmp+rename protocol, `encoding="utf-8"` explicitly.
- Do not gold-plate: macOS/VST3, frozen-runtime packaging, and separation
  quality metrics are explicitly waived this run.
- One hypothesis per run — attribution is the point of the loop.
- User-facing prose in 繁體中文; JSON keys, status values, `stop_reason`
  values, and the `LOOP STATUS:` line stay in English exactly as above.

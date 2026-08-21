# SDD ledger — plan: docs/superpowers/plans/2026-08-17-start-confirmation.md

Spec: docs/superpowers/specs/2026-08-17-start-confirmation-design.md
Worktree: .claude/worktrees/start-confirmation, branch feature/start-confirmation, base main @ 85de87a

## LEDGER REBUILT 2026-08-19

The original worktree was removed between sessions, taking the gitignored SDD
workspace with it — ledger, task briefs, implementer reports, diff packages, all
gone. All seven commits survived on the branch; git log is the record. This file
is reconstructed from the surviving git history and the controller's own notes.
Reports for Tasks 1-4 are NOT recoverable.

## Progress (reconstructed)

Task 1: complete — 142e2ca, review clean (spec compliant, quality approved)
Task 2: complete — 3e5ea8f + fix e03ac79, 1 fix round, re-review: all 4 findings addressed, no new breakage
Task 3: complete — 222c65c, review clean (spec compliant, quality approved)
Spec correction: 5e12487 — transport claim fixed, application-level ack for the sets mirror
Task 4: implemented dc084f9, REVIEW IN PROGRESS (base 5e12487, not 222c65c)

## Rulings made so far (carry to the final summary)

Ruling A: moved the running() change from Task 6 into Task 2 — running() defines what Confirm IS; leaving it wrong for four tasks would have made Task 4's own bench step fail for an unrelated reason. Cost if wrong: Task 6 shrinks to lamps + phase naming.
Ruling B: Task 7 must load scrConfirm explicitly on phase == Confirm, independent of the running flag, because running() excluding Confirm makes uiUpdateMain fall back to the logo screen. Cost if wrong: the confirm screen never appears; Task 7's screenshot step catches it.
Ruling C (superseded by Ruling F): the cfg.targetReps restore at session end was promoted from a note to a required step.
Ruling D: two plan steps that name no symbol (the enqueue helper, the linkUp expression) stand as written — an invented name reading as authoritative is worse than an instruction to go read. Cost if wrong: one extra implementer turn.
Ruling E: requestStart() must accept Fault as well as Idle, or panel START in Fault becomes a silent no-op once routed through it, retiring an existing fault-recovery route. Cost if wrong: clearing a fault takes one extra tap; STOP still clears it directly.
Ruling F: took the reviewer's DURABLE fix over its minimal one — SessionSM owns sessionTargetReps, cfg.targetReps stays permanently the prescription, and Ruling C's restore was deleted. The flag-and-restore alternative kept the invariant alive by convention across three exit paths and Task 5 would have inherited the same hole. Cost if wrong: larger Task 2 diff; Task 5 needs rewording.
Ruling G: a target edit extends the 30 s window (phaseStart = nowMs in setPendingTarget). Raised as Minor, promoted because stepping 10 -> 45 is 35 taps and the screen vanishing mid-edit is a defect the feature would ship with. Cost if wrong: one line to revert.
Ruling H: STOP-in-Confirm deliberately does NOT zero repCount, unlike STOP-in-Idle — cancelling a confirmation is not a reset request. Cost if wrong: the app's stop needs a second press to clear the count, which is today's behaviour anyway.
Ruling I: kept the interrupted implementer's partial session.h rather than reverting — the header side of the durable fix was complete, and it independently caught that setTarget() must write BOTH fields or a live mid-session edit stops affecting when the session ends. Cost if wrong: a fresh implementer inherited a header it did not write.
Ruling J: the sets mirror needs an application-level ack rather than depending on the unmerged feature/link-crc-ack — target() reports the pending target during Confirm, and the display resends until the echo matches. Without it a dropped or corrupted frame lets a physical START commit a different number than the screen shows, and frame loss is not rare on this rig. Cost if wrong: one line plus a resend timer; harmless redundancy if that branch later merges.
Ruling K: the reviewer's stray-session.h warning was not a real gap — tree verified clean at 222c65c; its environment snapshot predated the fix commit.
Ruling L: Task 4's review base is 5e12487, not the 222c65c my own paused-handoff note gave — 5e12487 is my spec-correction doc commit and is not Task 4's work.

## Deferred minors (for the final whole-branch review to triage)

- session.h:21 Thai phase glossary — addressed inside Task 2's dispatch.
- task-1-report misattributed the expected build warning to phaseName(); report-only.
- pendingTarget() returns a stale value outside Confirm; harmless, but Task 7 must gate reads on confirming().
- CONFIRM_TIMEOUT_MS is a header `static const`; `constexpr` would be idiomatic. Brief specified it verbatim.
- arduino-cli defaults to warnings "none" (-w), so per-task builds prove nothing about warnings — run one `--warnings all` compile in Task 12.
- sendSets/sendConfirm carry trailing comments the sibling send* declarations lack; brief-directed, cosmetic.

## Carries into later tasks

Task 7: load scrConfirm on phase == Confirm regardless of the running flag (Ruling B); gate pendingTarget() reads on confirming(); resend `sets` until the state message echoes a matching target (Ruling J).
Task 8: bench steps must cover START -> Confirm with no relay click, second START commits, STOP cancels, 30 s expiry, and link-down START starting immediately.
Task 12: one `--warnings all` compile; the manual session-only check (set default 10, confirm 12, run, verify settings still reads 10).
From Task 4's implementer, unverified: gFaultRecoveryPending lives outside SessionSM, so a second requestStart() call site would need the same faulted() check, or the flag should move into SessionSM.

## Open question for the user (asked twice, unanswered)

Is feature/link-crc-ack intended to merge to main before this feature ships? It
carries the id/ack/CRC transport this branch lacks. If it merges, Ruling J's
application-level ack becomes redundancy; if not, that ack is load-bearing.

## PAUSED 2026-08-19 (second pause) — resume here

Stopped at the user's request (usage limit). Tree clean at dc084f9. No agent running.

Task 4's review was dispatched and KILLED mid-analysis — no verdict was produced. Re-dispatch it from scratch:
  review-package docs/superpowers/plans/2026-08-17-start-confirmation.md 5e12487 HEAD
  then the task reviewer against task-4-brief.md + that diff. Task 4's implementer report is NOT recoverable; pass its claims inline (they are listed under 'Carries' below).

PARTIAL FINDING from the killed reviewer, unverified but worth chasing first:
  'gLink.poll() is at line 592, after the new snapshot at 458.' If the linkUp snapshot is taken before gLink.poll() refreshes lastRx, then linkUp is evaluated against a stale receive timestamp — one loop iteration old. On the first iteration after a link comes up, START could take the link-down branch and start immediately instead of opening the confirmation. Verify this before anything else in the re-review.

Task 4 implementer claims to verify (its report is gone; these are from its completion message):
  - dispatch order: stuck-guard -> in-Confirm->confirmStart() -> linkUp->requestStart() -> else start()
  - STOP uses else-if so a panel cancel cannot fall through to requestStop()'s Idle case and zero repCount
  - linkUp shares the dead-link safe-stop's lastRx snapshot; it REWROTE that function's sign-comparison comment claiming the sharing closed a race
  - session.h target() returns pendingTargetReps in Confirm
  - gFaultRecoveryPending (.ino-local) OR'd into the lamp fault argument so 'both lit' persists through a cancelled/expired Fault-Confirm
  - controller +512 B (more than Tasks 1-3 combined); display byte-identical; button-bench -DryRun 7/7 (that transcript does NOT exercise Confirm)

## ANSWERED 2026-08-19 — feature/link-crc-ack WILL merge before ship

Ruling J REVISED: keep the target() pending-echo (one line, and it is how the display shows what the machine will actually commit); DROP the timer-driven resend loop from Task 7 — dead complexity once retransmit exists beneath it, and the echo already makes a lost frame visible instead of silent. Cost if wrong: if this branch is bench-tested before the CRC branch lands, a dropped sets frame is visible as a mismatch rather than auto-corrected.

MERGE REQUIREMENT (highest-value item in this file — do not lose):
  sendSets and sendConfirm are bare emit() calls, matching every sender on this branch today. When feature/link-crc-ack merges, emit() gains id/ack/retransmit and every sibling sender gains loss protection. These two MUST be converted to that branch's tracked sender at merge time.
  A textual merge will NOT flag this: the new senders and the hardened ones are the same shape, so the result compiles and runs while sets and confirm alone stay fire-and-forget. confirm is the message that starts the machine moving; sets is the number it moves toward.

Task 7 carry updated: no resend timer. Render the local value immediately, show a persistent echo mismatch as unconfirmed.

## PAUSED 2026-08-19 (third pause) — resume here

Tree clean at 540aa4a. No agent running. Task 4's review has now been dispatched and killed TWICE without producing a verdict — both times it was stopped just as it began the gLink.poll() ordering check. Task 4 (dc084f9) remains IMPLEMENTED BUT UNREVIEWED.

RESUME, exactly:
  1. If the worktree is gone: git worktree add .claude/worktrees/start-confirmation feature/start-confirmation, then restore the ledger with
     cp docs/superpowers/plans/2026-08-17-start-confirmation-decisions.md .superpowers/sdd/2026-08-17-start-confirmation/progress.md
  2. task-brief <plan> 4 ; review-package <plan> 5e12487 dc084f9
  3. Dispatch the task reviewer (opus). Its FIRST check is the gLink.poll() ordering lead below. Task 4's implementer report is gone — pass its claims inline from the list above.
  4. Then Tasks 5-12. Task 5 needs rewording first: Ruling F removed the cfg.targetReps overloading it was written against.

UNRESOLVED PRIORITY LEAD (two reviewers were killed before reporting on it):
  gLink.poll() is at line 592; the new linkUp snapshot is at 458. If the snapshot precedes the poll that refreshes lastRx, linkUp is evaluated one loop iteration stale. On the first iteration after a link comes up, START would take the link-down branch and start immediately instead of opening the confirmation — 'sometimes skips the confirm screen', visible only on hardware. button-bench -DryRun cannot catch it: its canned transcript never exercises Confirm.

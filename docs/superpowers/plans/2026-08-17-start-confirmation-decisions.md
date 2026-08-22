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

## 2026-08-19 — poll-ordering lead RESOLVED by the controller, NOT a defect

Verified directly in pt-leg-controller.ino: lastRx snapshot at 458, linkUp at 459, START dispatch at 495, gLink.poll() at 592, dead-link safe-stop at 631 reusing THE SAME snapshot. The ordering the two killed reviewers flagged is real but deliberate and documented at 446-457 and 611-625:
  - one snapshot shared by the START dispatch and the safe-stop so the two cannot disagree within an iteration;
  - capturing before netLoop()/poll() guarantees lastRx <= now, which is what makes the signed rollover comparison at 631 sound. A fresh gLink.lastRxMs() read there could return a timestamp NEWER than now, because netLoop() can block ~400 ms while poll() updates lastRx in between.
  Staleness is bounded at one loop iteration against a 2000 ms threshold, and the 'link just came up' case is delayed by exactly one iteration, not indefinitely.
  Ruling: the implementer's claim that sharing the snapshot closed the race HOLDS. Do not send a reviewer hunting this as a priority; hand it the finding and let it verify the reasoning. Cost if wrong: a one-iteration-stale linkUp could route a START pressed in the same millisecond the first frame lands to the link-down branch — bounded, self-correcting next iteration.

Task 4: complete (commits 5e12487..dc084f9, review: spec compliant, quality APPROVED, 1 Important + 4 Minor)
Task 4: reviewer independently AGREED the rewritten lastRx sign-comparison comment is true of the new code — the controller's own resolution is now confirmed, not just asserted.
Task 4: Ruling: the Important finding (server 'start' bypasses Confirm exactly as correction 1 forbids for the panel) is folded into Task 5 as a required step rather than run as a Task 4 fix round — Task 5 rewrites that identical block, so a fix round here would have two implementers editing the same lines, and Task 5 needed rewording anyway after Ruling F. NOT deferred: written into the plan as Task 5 Step 3 with the code, so it cannot be forgotten. Cost if wrong: the fix lands one task later than it could have; nothing between Tasks 4 and 5 exercises the server start path.
Task 4: Ruling: a server start arriving mid-confirmation SUPERSEDES the confirmation (cancelConfirm then start) rather than committing it or being refused — the spec's 'app start needs no CYD confirmation' is about a start from Idle; arriving mid-confirmation, remote intent should win explicitly instead of racing, and the display must not be left showing a confirm screen for a session already moving. Cost if wrong: an operator mid-edit at the machine has their pending number discarded by a remote start, which is the correct precedence but will surprise.
Task 5 rewritten in the plan: adds SessionSM::setSessionTarget() (session-only, no prescription write), splits target-with-start from target-alone against the post-Ruling-F design, and carries the confirm-gating fix. setSessionTarget() must be called AFTER start(), which seeds sessionTargetReps from the prescription.
Task 4: minor (deferred): gFaultRecoveryPending duplicates SessionSM state from outside it; any second requestStart() caller must repeat the faulted() check. Reviewer suggests a legRecoveryOwed() accessor inside SessionSM.
Task 4: minor (deferred): session.cpp:156 comment now stale — says the panel button bypasses Confirm entirely; true only when the link is down.
Task 4: minor (deferred): START+STOP edges in one iteration no longer zero repCount at Idle. Rare, unremarked behaviour change, new behaviour otherwise better (no relay motion).
Task 4: minor (deferred): 'both lit' is not a complete 'leg may be raised' indicator — legPositionUnknown also starts true at boot, which shows the plain Idle pattern, so two equivalent states read differently. Comment overstates the equivalence.

Task 5: implemented, commit 3d46aff (ctrl +76 B, display byte-identical, dry run 7/7). Under review.
Task 5: CONTROLLER ANALYSIS of the implementer's flagged concern — it is NOT merely network-loss-dependent. server/app/state.py:84-116 fetch_desired_target is deliberately NOT consume-on-read: it repeats on every 1 Hz poll until the device heartbeat echoes that value back (state.py:169-170) or COMMAND_TTL_SEC expires. So after an app start with a session-only target, the target keeps being served until the controller's heartbeat reports it. Any command poll landing in that window — or any failed heartbeat POST — delivers the target with cmd != 'start', and Task 5's Step 2 branch then persists it into gSettings.targetReps as if it were a /target prescription edit. That breaks the feature's central promise through the app path.
Task 5: the echo does normally close the loop, because gSession.target() returns sessionTargetReps while running, so the heartbeat reports the session value. The exposure is the gap between the start poll and the first successful heartbeat.
Task 5: candidate fix for the reviewer to weigh, NOT yet ruled: also skip the persist branch when wantTarget equals gSession.target() — a target matching what the session is already running is the server echoing its own desire back, not a new prescription edit. Costs one comparison. The competing consideration is that the app's live stepper legitimately edits the target mid-session via /target, so the fix must not swallow a genuine mid-session edit to a value that coincides with the running one.

Task 5: review 1 — spec compliant on both deliverables, but CRITICAL: the brief-mandated 'cmd != "start"' discriminator lets a session-only target reach the stored prescription and NVS. Two triggers confirmed by the reviewer:
  (1) one dropped heartbeat POST — netHeartbeat is fire-and-forget with a 400 ms timeout and no retry queue, and lastHb is set before the call, so the ack gets exactly one chance per poll window;
  (2) buttonStartStuck() — DETERMINISTIC, no network. The refusal path returns without start(), the server has already consumed the command but keeps serving the target, and an Idle controller reports cfg.targetReps so it can never ack. Next poll persists the number. A wiring fault that refuses to move the machine permanently rewrites the prescription.
Task 5: my candidate fix (skip persist when wantTarget == gSession.target()) was PARTIAL — closes trigger 1 only; with the start refused the session is Idle and the guard never matches. It would also swallow a genuine mid-session /target edit naming the running value. Correctly not ruled on.
Task 5: Ruling: take the reviewer's latch fix — remember the value a start actually carried plus its timestamp, and treat only that exact value as an echo while the server can still be repeating it (COMMAND_TTL_SEC + margin). It keys off the real ambiguity rather than inferring intent from session state, so it covers the refused start too, and cannot swallow an edit to a different value. Cost if wrong: two file-scope statics and a bounded window; residual is an edit to the same value within 30 s of a start, which is a no-op against what the app already asked for.
Task 5: Ruling: the ROOT CAUSE is server-side — queue_start() writes the start-carried target into the SAME _desired_target slot /target uses (service.py:141-142), so the ambiguity is created before it reaches the wire. The correct remedy is a separate slot consumed with the command in fetch_command(). NOT implemented now: the plan scopes this feature to no server change, the controller fix is needed regardless (deployed servers exist), and it closes both triggers on its own. Recorded as a follow-up in the spec. Cost if wrong: the controller carries a defensive latch that a later server fix would make redundant.

Task 5: fix round 1/5 (2 addressed, 1 NEW Critical; commits 3d46aff..31fbfd1). Latch verified correct on all three named risks: it fires on the buttonStartStuck refusal path (:733 runs before the guard at :743), the 31 s window provably outlasts the server's fixed 30 s TTL (fetch_desired_target never renews _target_queued_at), the millis() comparison is wraparound-safe, and function-local statics persist across loop() correctly. Minor also cleanly fixed via startEligible captured before start().
Task 5: NEW CRITICAL introduced by the fix, and it is in the line I PRESCRIBED (inherited from the first reviewer's suggested code): 'if (wantTarget < 0) startCarried = -1;' conflates 'server stopped serving this target' with 'this poll got no answer'. netPollCommand() defaults targetOut to -1 (net.cpp:104) and overwrites only on a 200 that parses (net.cpp:113-146), so a timeout, non-200, WiFi drop or parse failure clears the latch for unrelated reasons; the next successful poll then sees the still-served value with an empty latch and persists it to NVS. Worst on trigger 2, where nothing runs so only the 30 s TTL ever clears the server slot — all ~31 polls exposed.
Task 5: Ruling: gate the reset on poll success (netServerUp()/the existing serverOk read right after netPollCommand()) rather than on wantTarget < 0 alone. Cost if wrong: a latch that outlives the server's desire by at most the 31 s window, which the window bound already covers.
Task 5: re-reviewer partially disagreed with the implementer's framing of the residual value-vs-provenance concern — the 'no-op against what the app just asked for' defence holds when the start succeeded, but not on the buttonStartStuck refusal sub-case where nothing is running. Agrees severity is still low and self-healing. Recorded, not actioned.

Task 5: fix round 2/5 (3 of 4 causes closed, 1 open; commits 31fbfd1..eabd70d). pollOk verified read immediately after netPollCommand() with no intervening serverOk writer, and the 31 s window verified independent of latch state. Report's overstated round-1 claim corrected as instructed.
Task 5: OPEN — serverOk is HTTP-status-only (net.cpp:159 runs unconditionally after the 200 block), so a 200 with an oversized (net.cpp:131-133) or unparseable (net.cpp:139-140) body sets pollOk true while wantTarget stays -1, clearing the latch. Same defect class as round 1, narrowed from four triggers to one.
Task 5: Ruling: DELETE the reset line entirely rather than narrow the predicate a third time. The 31 s window already bounds echo recognition on its own (verified by the re-reviewer: echoOfStart is anchored to startCarriedAt and expires regardless of latch state), so the reset is only an early-clear optimisation — and it is the sole source of this whole defect class. Two rounds have gone into making a predicate correct that buys nothing the timer does not. Cost if wrong: within the window, a genuine /target edit naming the exact value a start just carried is swallowed for up to 31 s instead of until the server stops serving it — an extension of the value-vs-provenance residual already accepted as low severity and self-healing.
Task 5: residual recorded, NOT actioned (structural, same server-side remedy): startCarried/startCarriedAt are RAM-only, so a controller reboot inside the server's 30 s TTL leaves a fresh device with an empty latch that can persist the still-served value on its first poll. Narrow; the durable fix remains a separate server-side slot consumed with the command.

Task 5: fix round 3/5 (1 addressed, 0 open; commits eabd70d..7326ecf, controller 44 B SMALLER — net code removed). Re-reviewer verified pollOk has no surviving reader, ordinary echo recognition still excludes the repeat from the persist branch, the window term alone expires the latch so a same-value edit after 31 s persists normally, and the replacement comment justifies the ABSENCE of a reset rather than logging the removal. It also re-read net.cpp to confirm the comment's factual claims rather than trusting them.
Task 5: complete (commits 3e7f4e5..7326ecf, 3 fix rounds, review clean)
Controller-side work is now DONE: Tasks 1-5 complete. Remaining: 6 (small), 7 (confirm screen, largest), 8 (bench), 9-10 (app), 11 (docs), 12 (flash + manual verification).

Task 6: review — spec compliant, quality APPROVED. Glyph claim independently verified (all four glyphs of ยืนยัน appear in pre-existing ui.cpp literals); no stacked vowel+tone; Step 1's 'no edit needed' confirmed correct (panelLampsSet's sole call site passes gSession.running(), and running() excludes Confirm); scope and release flags clean.
Task 6: CONTROLLER ERROR CORRECTED BY THE REVIEWER — I dispatched the review claiming the brief suggested รอยืนยัน and that the implementer dropped รอ without explanation. False. Both task-6-brief.md:24 and the master plan line 560 specify ยืนยัน verbatim; the implementer transcribed it exactly. I had confused it with the รอยืนยัน used in Task 2's dispatch for the Thai glossary. The implementer did nothing wrong and my premise was the defect.
Task 6: Ruling: the wording concern is real but belongs to the PLAN, not the diff — every sibling phaseName() return is a state (กำลังยก, ค้าง, กำลังลง, พัก, กำลังหยุด, ระบบขัดข้อง, พร้อม) while ยืนยัน alone is a bare imperative, and it will sit beside the actual confirm/cancel buttons where it reads as a third one. The controller's own code calls this phase รอยืนยัน in 30+ places. Plan corrected to รอยืนยัน and a one-string fix round dispatched, rather than deferring it to Task 7 where a wrong label could propagate into the confirm screen's own text. Cost if wrong: one word, trivially reverted.

Task 6: fix round 1/1 (1 addressed, 0 open; commits e65525c..d1aee51). Glyphs ร and อ both cited at ui.cpp:54 in the pre-existing พร้อม literal; diff confined to the single line.
Task 6: complete (commits 3df392f..d1aee51, review clean, 1 fix round for a plan-side wording defect)

Task 7: implemented 549cc20 (display +1,880 B). Review: all five carried constraints PASS and visibly understood; screen built once (no per-entry LVGL leak); every Thai glyph has verified precedent; signature deviation judged an improvement with no information lost. Two Important, seven Minor.
Task 7: Important 1 — link-down strands the screen. uiPhase holds the last received phase forever, so on link loss the screen stays up indefinitely, the countdown sticks at 0, and ตกลง sends into a dead UART with no exit but a power cycle. Violates the rule ui.h:34-35 states for this display, which every other screen honours.
Task 7: Important 2 — the unconfirmed warning fires on ~half of healthy taps. The controller emits state on a fixed 250 ms cadence, NOT on receipt of sets, so a single-tick disagreement is normal; the spec asked for a PERSISTENT mismatch. Worse, the label toggles via LV_OBJ_FLAG_HIDDEN and LVGL flex drops hidden children and their row gap, reflowing the column ~22 px and moving the ตกลง/ยกเลิก row under the operator's finger on half of all taps. Reviewer also computed the column may clip in the shown state.
Task 7: Ruling: fix both Importants and fold in Minors 3, 4, 5, 6, 7, 9 — all are in files already open and cheap. PROMOTING Minor 6 to required: gLast initialises to {8,4,4,10,10,10}, which is not just placeholder but the OLD 4/4 stroke defaults, so a START within ~3 s of display boot renders fabricated AND wrong seconds on the one screen whose entire job is stating what will run. Cost if wrong: a slightly larger fix diff on a task already approved on its constraints.
Task 7 -> carry: Minor 8, session.h:213's Thai comment promises the display resends on a mismatched echo. It deliberately does not (the resend timer was rejected). Controller file, correctly untouched here — reconcile in whichever task next opens session.h, or a reader will hunt for a resend that does not exist.

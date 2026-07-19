# Graph Report - .  (2026-07-18)

## Corpus Check
- Corpus is ~346 words - fits in a single context window. You may not need a graph.

## Summary
- 11 nodes · 12 edges · 3 communities (2 shown, 1 thin omitted)
- Extraction: 83% EXTRACTED · 17% INFERRED · 0% AMBIGUOUS · INFERRED: 2 edges (avg confidence: 0.8)
- Token cost: 41,431 input · 0 output

## Community Hubs (Navigation)
- Wireless ADB & Smoke Test
- Session & Memory Tracking
- Branch Merge Decision

## God Nodes (most connected - your core abstractions)
1. `Session 2026-07-18 — Remote control, wireless adb attempt, smoke test skipped` - 4 edges
2. `Android smoke test on physical phone` - 4 edges
3. `Wireless adb debugging attempt` - 3 edges
4. `adb pair protocol fault (couldn't read status message)` - 3 edges
5. `adb mdns service discovery (_adb-tls-connect / _adb-tls-pairing)` - 2 edges
6. `Merge decision on hold for feature/app-screens` - 2 edges
7. `Branch feature/app-screens @ a86c60d` - 2 edges
8. `Remote-controlled Claude Code session (/remote-control)` - 1 edges
9. `e2e 13/13 Playwright verification baseline` - 1 edges
10. `Memory: session-state-2026-07-11.md` - 1 edges

## Surprising Connections (you probably didn't know these)
- `Android smoke test on physical phone` --conceptually_related_to--> `Merge decision on hold for feature/app-screens`  [INFERRED]
  docs/sessions/2026-07-18-remote-session.md → docs/sessions/2026-07-18-remote-session.md  _Bridges community 0 → community 2_
- `Session 2026-07-18 — Remote control, wireless adb attempt, smoke test skipped` --references--> `Branch feature/app-screens @ a86c60d`  [EXTRACTED]
  docs/sessions/2026-07-18-remote-session.md → docs/sessions/2026-07-18-remote-session.md  _Bridges community 1 → community 2_

## Hyperedges (group relationships)
- **Wireless adb debugging attempt flow** — docs_sessions_2026_07_18_remote_session_adb_mdns_discovery, docs_sessions_2026_07_18_remote_session_wireless_adb_debugging, docs_sessions_2026_07_18_remote_session_adb_pairing_protocol_fault, docs_sessions_2026_07_18_remote_session_android_smoke_test [EXTRACTED 1.00]
- **Pre-contest open items** — docs_sessions_2026_07_18_remote_session_android_smoke_test, docs_sessions_2026_07_18_remote_session_merge_decision_hold, docs_sessions_2026_07_18_remote_session_wireless_adb_debugging [EXTRACTED 1.00]

## Communities (3 total, 1 thin omitted)

### Community 0 - "Wireless ADB & Smoke Test"
Cohesion: 0.60
Nodes (5): adb mdns service discovery (_adb-tls-connect / _adb-tls-pairing), adb pair protocol fault (couldn't read status message), Android smoke test on physical phone, e2e 13/13 Playwright verification baseline, Wireless adb debugging attempt

### Community 1 - "Session & Memory Tracking"
Cohesion: 0.50
Nodes (4): Remote-controlled Claude Code session (/remote-control), Session 2026-07-18 — Remote control, wireless adb attempt, smoke test skipped, Memory: MEMORY.md index, Memory: session-state-2026-07-11.md

## Knowledge Gaps
- **3 isolated node(s):** `Remote-controlled Claude Code session (/remote-control)`, `Memory: session-state-2026-07-11.md`, `Memory: MEMORY.md index`
  These have ≤1 connection - possible missing edges or undocumented components.
- **1 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `Android smoke test on physical phone` connect `Wireless ADB & Smoke Test` to `Branch Merge Decision`?**
  _High betweenness centrality (0.600) - this node is a cross-community bridge._
- **Why does `Merge decision on hold for feature/app-screens` connect `Branch Merge Decision` to `Wireless ADB & Smoke Test`?**
  _High betweenness centrality (0.556) - this node is a cross-community bridge._
- **Why does `Session 2026-07-18 — Remote control, wireless adb attempt, smoke test skipped` connect `Session & Memory Tracking` to `Branch Merge Decision`?**
  _High betweenness centrality (0.533) - this node is a cross-community bridge._
- **Are the 2 inferred relationships involving `Android smoke test on physical phone` (e.g. with `adb pair protocol fault (couldn't read status message)` and `Merge decision on hold for feature/app-screens`) actually correct?**
  _`Android smoke test on physical phone` has 2 INFERRED edges - model-reasoned connections that need verification._
- **What connects `Remote-controlled Claude Code session (/remote-control)`, `Memory: session-state-2026-07-11.md`, `Memory: MEMORY.md index` to the rest of the system?**
  _3 weakly-connected nodes found - possible documentation gaps or missing edges._
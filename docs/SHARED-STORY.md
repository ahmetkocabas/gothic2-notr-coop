# Shared story for Gothic II: research notes and design

Goal: both players play **one** story. When one player talks to an NPC, the other sees the
same dialog and ends up with the same quest state.

Everything under "Measured" comes from bot runs with two real game clients on one PC
(`qa/run_story.ps1`, `qa/analyze_story.py`, probes in `StoryProbe.cpp`), Steam Gold Classic
2.6 (fix), 2026-11-23. "Derived" means read from code or headers, not measured.

## 1. Where Gothic II keeps the story (measured)

| State | Where | Size in a new game |
|---|---|---|
| Script globals | Daedalus symbol table: non-const `int` variables (`MIS_*`, `Kapitel`, counters) | 2174 values |
| Dialog options | `oCInfoManager::infoList`, one `oCInfo` per option, `told` flag | 4139 infos |
| Quest log | `oCLogManager` topics (status running/success/failure) with text entries | 0 at start |
| Hero numbers | `experience_points`, `learn_points`, `level`, attributes, inventory | |
| NPCs | spawned only near the local hero; 1054 NPC instances, most not in the world | |

The story state is quiet: idle for 60 s, only 1 of 2174 globals changed (`ZSTALKBUGFIX`).

## 2. What one dialog changes (measured)

The host bot talked to Xardas (`DIA_XARDAS_TODO`, `ARTEFAKT`, `PALADIN`, `FIRSTEXIT`):

- 5 infos became `told`,
- a log topic "The Eye of Innos" with 1 entry was created,
- globals changed: `XP_AMBIENT` 0→50; while the menu was open the info conditions also set
  `BELIARWEAPNEXTLVL` 0→1 and `BELIARNEXTDAMAGECHANCE` 0→12 (condition functions have side
  effects),
- **nothing of this reached the client** (0 new told infos, no log topic, globals differ).
  At the end of every run the two copies differed in 3 globals, 5 told infos and 1 log topic.
- The client did not see the dialog: its copy of Xardas played walk and run animations
  (host-driven) instead of dialog gestures; no subtitles or speech on the client.

## 3. Distance (measured)

| Situation | Client sees | Host world |
|---|---|---|
| Client in the city market, host at the tower (~320 m) | 15 NPCs, **0 synced, 0 moved, no animation** (frozen) | **0 NPCs** around the client's position |
| Host joins the client in the city | 15 NPCs, all synced, same animations as the host (guard, walk, sweep, sit) | 15 NPCs, all broadcast |

The host only broadcasts NPCs within 45 m of **its own** hero, and the engine only spawns and
runs NPCs near the local hero. A client away from the host has nobody simulating the NPCs
around it. Shared story needs either "stay together" (tether) or area ownership (the client
simulates and publishes the NPCs near it).

## 4. Client dialogs (measured)

A client talking to a host-driven NPC (bot: `talks` in the client config) opens the dialog
camera, but the option menu never appears; the talk times out after 60 s. The NPC stays in a
walk animation in place. Findings so far:

1. `oCAIHuman_StandActions` (client only) stopped the focused NPC's animations and cleared its
   message queue **every frame**, including during a dialog, which deletes the NPC's dialog
   lines. Fixed: skipped while a dialog is open. Not enough on its own.
2. Holding the talked-to NPC locally (no host updates while talking): not enough.
3. Pausing all sync (F2) for the talk: not enough.
4. Approaching the NPC from the other side (the host's hero was standing where the client
   arrived): not enough.

   Root cause unknown. Next: log the client NPC's message queue (`GetEM()`) and AI state during
   the talk and compare with a working host talk; the sync code calls `ClearAIState`,
   `KillMessages` and `ClearEM` on host-driven NPCs (`RemoteNpc::UpdateWeaponMode`), which may
   leave the NPC unable to run `ZS_Talk`. Either way the design below routes client dialogs
   through the host, where they work.

## 5. Design: host-authoritative story with effect capture

Following [effect-capture-story-sync](https://github.com/cranebeaker/coop-anything/blob/main/playbook/effect-capture-story-sync.md):
only the host runs story scripts; what they change is sent to clients as data.

**Dialogs are held on the host.**
- A client who presses "talk" on an NPC sends an *intent* (NPC id). The host opens the dialog
  for the client's puppet (or for its own hero while the client is the "acting player") and
  sends the menu (info names and descriptions) to the client.
- The client picks; the pick goes to the host as an intent; the host runs `OnInfo` there.
- The host captures each `EV_OUTPUT` (speaker, output unit name like `DIA_Xardas_TODO_15_00`)
  and sends it; every client plays that line on its copy of the speaker (sound + subtitle),
  so both players see and hear the same dialog.
- One dialog per NPC at a time ("dialog hold"); the second player sees it as a spectator.

**Story effects after each info (and on triggers, kills, item pickups):**

| Effect | How to capture | How to apply on clients |
|---|---|---|
| Script globals | Diff the 2174 ints before/after (cheap) or hook the parser's assignment op | Write the int, no script run |
| Told infos | Diff `told` flags | `SetTold(1)` |
| Log | Diff topics and entries, or hook `Log_CreateTopic`, `Log_SetTopicStatus`, `B_LogEntry` externals | `oCLogManager` add topic/entry |
| Items, gold | Hook `CreateInvItems`, `B_GiveInvItems`, `Npc_RemoveInvItems` externals | Give to the named recipient |
| XP, LP, attributes | Hook `B_GivePlayerXP` and attribute externals, send deltas | Add delta to each player (policy) |
| NPC changes | `Npc_ExchangeRoutine`, `AI_StartState`, `Wld_InsertNpc`, guild, attitude, death | Apply to the NPC copy |
| Chapter, cutscenes, world changes | `Kapitel`, `Wld_*` externals | Apply; cutscenes run for everyone |

Rules from the playbook that apply directly: one ordered story log with sequence numbers,
apply on the main thread, never run the story script on a client, deltas for numbers, a
digest of the shared state to detect drift (the 2174 ints + told flags + log fit in a few KB).

## 6. Smallest useful first step

1. **Story snapshot sync:** after every host dialog (`OnInfoEnd`) diff globals, told flags and
   the log, and send the diff; clients apply it. No items or XP yet. This alone makes quest
   progress and the log shared for dialog-driven quests.
2. **Dialog mirroring:** send `EV_OUTPUT` lines from the host so the other player sees and hears
   them.
3. **Client-initiated dialogs through the host** (intent, menu, pick).
4. Items/XP routing, kill and pickup effects, tether or area ownership.

Each step gets a bot check in `qa/analyze_story.py`: after a host talk the two
`GothicCoopStory.json` files must not differ in globals, told infos or log topics.

## 7. Other findings

- `RemoteNpc` allocates a new `zVEC3` for every position packet and never frees the old one
  (`lastPositionFromServer = new zVEC3(...)`): a slow memory leak.
- `SafeQueue::isEmpty` reads the queue without the lock.
- A host-side talk started right after the client joined once stalled in a run animation;
  started 10 s earlier it worked in the next 4 of 4 pair runs. Unknown cause.

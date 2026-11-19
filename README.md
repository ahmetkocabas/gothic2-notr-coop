# Gothic II: Night of the Raven — co-op (GothicCoop, revived)

Play Gothic II: Night of the Raven together: players see each other, fight the same
NPCs and monsters, and share hit points and dropped items. Each player keeps their own
story and quest log.

This is a maintained copy of **[GothicCoop](https://github.com/Gothic-Workshop/GothicCoop)**,
a [Union](https://steamcommunity.com/sharedfiles/filedetails/?id=2787458679) plugin written by
**Colder** and published by Gothic-Workshop. All credit for the mod itself goes to them.
This repo adds build fixes, a crash fix, and an automated two-client test bot.
License: **AGPL-3.0** (same as upstream).

> Türkçe açıklama aşağıda.

## Status

| | |
|---|---|
| Game version | Steam "Gothic II: Gold Classic", `Gothic2.exe` 2.6 (fix) |
| Union | 1.0m (Steam Workshop) |
| Tested | Automated: two game clients on one PC, 3 passes of 21 checks plus a failing negative control. By hand: two clients on one PC, positions matched within 2 m both ways, a shared fight (sheep HP and position in sync). |
| Not tested yet | Two PCs over the internet or a VPN; client dialogs with NPCs; changing worlds. |

## What is shared

- Players: position, heading, animations, weapons and armor, hit points, talents, spells.
- NPCs and monsters near the host: the host runs them, clients mirror them. Hits from
  clients are applied on the host.
- Time of day, reviving a downed friend (F4), items dropped and picked up.

**Not shared:** quests, dialog choices, story progress, chests and doors, saves. These
come from reading the source code, not from a full playthrough.

## Install (every player)

1. Gothic II: Gold Classic on Steam (2.6 fix). No patch needed.
2. Subscribe to **Union - patch for Gothic 2** on the Steam Workshop.
3. From the [latest release](../../releases), copy
   `System\Autorun\GothicCoop.dll` to `<game>\System\Autorun\` and
   `System\GothicCoopConfig.json` to `<game>\System\`. Back up first.
4. Everyone needs the same DLL: compare `Get-FileHash` with the hash in the release notes.
5. In `GothicCoopConfig.json` set `nickname`, and `server` to the host's IP (for example
   the host's Radmin VPN address). Port `1234` UDP, the same for everyone.

## Play

1. Everyone starts a new game, **finishes the opening talk with Xardas**, and saves.
   (A client that joins during that talk gets stuck in it.)
2. Host presses **F1** (server). Others press **F2** (join).
3. F2 again pauses/resumes sync, F3 respawns friend characters, F4 revives a downed
   friend, P shows the log, O shows stats.

## Changes in this repo

- Build with Visual Studio 2022 (toolset v143): missing `#include <chrono>`.
- Crash fix: `Game_Loop` erased NPCs from `SyncNpcs` inside a range-for (undefined
  behaviour, hit whenever an NPC is more than ~67 m away). Verified by a test that moves
  the client far away and back.
- `"upnp": false` option: skip router port mapping (not needed over a VPN).
- Test bot, **off unless the config has `"autotest"`**: starts the session, logs JSON
  state to `GothicCoopTest.log`, teleports on a schedule; test-only hooks let two copies
  run on one PC. See [docs/TESTING.md](docs/TESTING.md).

## Build

Visual Studio 2022 Build Tools with the C++ workload:

```
MSBuild GothicCoop.vcxproj "/p:Configuration=G2A MT Release" /p:Platform=Win32 /p:PlatformToolset=v143 /p:PreBuildEventUseInBuild=false /p:SolutionDir=<repo>\
```

Output: `Bin\GothicCoop.dll`. The prebuilt `beforebuild.exe` step from the Union SDK
template is skipped; the build does not need it.

## Known issues

- Story and quests are per player.
- A client's dialog with a host-driven NPC can get stuck; F2 (pause sync) may help (untested).
- NPCs are shared only around the host (~45 m); stay together.

Code and documentation in this repo were written with the help of an AI assistant
(Claude); commits say so.

---

## Türkçe

Gothic II: Night of the Raven'ı arkadaşlarla birlikte oynamak için. Oyuncular birbirini
görür, aynı NPC ve canavarlarla dövüşür; can ve yere atılan eşyalar ortaktır. Görevler ve
hikâye herkesin kendisinde ilerler.

Mod **Colder**'ın yazdığı [GothicCoop](https://github.com/Gothic-Workshop/GothicCoop)'tur;
bu repo derleme düzeltmeleri, bir çökme düzeltmesi ve otomatik test botu ekler.

**Kurulum:** Steam'de Gothic II: Gold Classic (2.6 fix) → Steam Workshop'tan "Union - patch
for Gothic 2" abone ol → [son sürümdeki](../../releases) dosyaları oyun klasörüne kopyala
(önce yedek al) → `GothicCoopConfig.json`'da `nickname` ve `server` (host'un IP'si, örn.
Radmin VPN) yaz.

**Oynama:** Herkes yeni oyunda Xardas'la konuşmayı bitirip kaydeder. Host **F1**, diğerleri
**F2**. F2 tekrar = senkronu duraklat/sürdür, F3 = arkadaş karakterlerini yenile, F4 = düşen
arkadaşı dirilt.

**Bilinen sorunlar:** Görevler ortak değil; client'ın NPC konuşması takılabilir; NPC'ler
host'un ~45 m çevresinde paylaşılır, birlikte gezin.

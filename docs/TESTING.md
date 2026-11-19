# Testing

The plugin has a test bot that drives two real game clients on one PC. It is compiled in
but stays off unless `GothicCoopConfig.json` has an `"autotest"` object.

## Test copies

Never test in your real install or with your saves. Make two copies of the game folder
(without `Saves`), for example `<root>\G2A-1` (server) and `<root>\G2A-2` (client), and in
each copy:

- Install Union (copy the Workshop item's `Data` and `System` files over the copy).
- Put `Bin\GothicCoop.dll` into `System\Autorun\`.
- In `System\Gothic.ini`: `zStartupWindowed=1`, a small resolution, `playLogoVideos=0`.
- Move `_work\Data\Video\Intro.bik` and `Intro_Addon.bik` out of the copy. A new game
  plays them and the game loop does not run meanwhile, which looks like a freeze.

`Gothic2.exe -nomenu` starts a new game in NEWWORLD without the menu.

## Running

```
powershell -File qa\run_suite.ps1 -Root <root>
```

builds the DLL, runs `qa\run_pair.ps1` three times (150 s each) and one negative control
with the client on the wrong port. Windows open on screen, so run it when nobody uses the PC.

`qa\check_pair.py` compares both logs on the shared wall clock:

- both copies run to the end, worlds load, sessions start, heroes never die;
- the client measures a ping; each side sees the other player;
- position follow both ways: 90% of samples within 200 units (2 m), up to 1.5 s lag;
- each teleport reaches the other side within 2 s;
- a far client teleport (75 m up, held) drops the client's synced NPCs, and they come back.

The negative control must fail every connection check.

## Bot config

```json
"autotest": {
  "role": "server" | "client" | "manual",
  "allowMultiInstance": true,
  "startAfterMs": 5000,
  "reportEveryMs": 500,
  "teleports": [[45000, 600, 0, 0], [95000, 0, 7500, 0, 1], [110000, 0, 0, 0]]
}
```

- `server` / `client`: the bot ends the opening dialog, starts the session after
  `startAfterMs`, and does the teleports (`[ms, dx, dy, dz]`; `[ms, 0, 0, 0]` goes back;
  a fifth value `1` holds the hero there).
- `manual`: a person plays; the bot only writes the log. Compare a session with
  `python -I qa\compare_manual.py <host log> <friend log> [clock_offset_ms]`.
- `allowMultiInstance`: lets a second copy start on the same PC. The exe refuses a second
  copy through a named file mapping `Lock_Gothic II`; the plugin renames it per process.

The log (`System\GothicCoopTest.log`) also records slow frames, fatal exceptions, who
called `ExitProcess`, and a watchdog stack of the main thread if `Game_Loop` stops for 3 s.
`qa\symbolize.py` maps `Gothic2.exe+0x...` addresses to the nearest ZenGin function
(approximate).

## Results

2026-11-16: 21/21, 21/21, 21/21; negative control 8/18 (all connection checks failed, as
they must). No slow frames, watchdog reports or unexpected exits.

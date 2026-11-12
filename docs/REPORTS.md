# Reports to the project

Playtendoh can send a short report to the project each time a game ends, so the owner can see which
games run, on which PS5 models and firmware, and what fails, without anyone uploading logs by hand.
This page lists everything a report holds. The app's code is `port/app/reports.cpp` (what is kept and
scrubbed), `port/app/report_send.cpp` (sending) and `tools/log-intake` (where it goes).

## When

- **Only with your yes.** Playtendoh asks once, on its first start (or the first start of 6.0), and
  Settings > Online > *Send reports* changes it at any time. Turning it off deletes the reports
  waiting to be sent and the install ID at once.
- **Never while you play.** As a game starts, the launcher notes it; when the launcher is back, that
  becomes a report in the data folder's `reports/outbox/`, and the outbox is sent a few seconds later,
  on its own, while the launcher is idle. Starting a game stops a send.
- A report that cannot be sent waits: at most 20 reports, 512 KB and 14 days, the oldest dropped first.
- A build from this repository sends to the project's intake. One built with `PLAYTENDOH_REPORT_URL` set
  empty asks nothing and sends nothing.

## What a report holds (schema 1)

| Field | Example | What it is |
|---|---|---|
| `schema` | `1` | this table's version |
| `install` | `3f9c…` (32 hex digits) | a random number the app makes when you say yes, to count consoles. It is not your console's ID, and a new one is made if you turn reports off and on again |
| `app`, `commit` | `6.0.0`, `76f7fd0` | Playtendoh's version and the commit it was built from (which pins every emulator's revision) |
| `side`, `emulator` | `switch`, `eden` | the system and the emulator that ran the game: `cemu`, `azahar`, `melonds`, `eden`, `eglantine`, `porpoise`, a libretro core (`mgba`, `mesen`...) or a system's own program (`n64`, `mupen64plus`...) |
| `titleId`, `name` | `0100f2c0115b6000`, `The Legend of Zelda: Tears of the Kingdom` | the game, as the library names it (80 characters at most) |
| `started` | `1762700000` | when it started, in seconds |
| `result` | `ended`, `failed`, `crashed`, `closed` | left from its menu; did not start; crashed; or the app or console was closed under it |
| `seconds` | `2520` | how long it ran, when it ended from its menu |
| `error` | `The game's keys are missing` | why it failed or crashed, scrubbed as the log is (300 characters at most) |
| `settings` | `{"nxResolution": 2, "nxFrameGeneration": 1}` | the game's settings that change how it runs, as numbers. Not volume, language, region or controls |
| `firmware`, `model` | `11.60`, `pro` | the PS5's firmware, and `base`, `slim`, `pro` or `unknown` |
| `jit`, `hen` | `true`, `true` | whether the app got JIT memory and reached `/data` |
| `log` | | the last 120 lines (16 KB at most) of Playtendoh's own boot log for that session, scrubbed |
| `logs` | `[{"file": "core.log", "text": "…"}]` | the emulator's own logs of that session, for those that keep logs of their own (Cemu, Azahar, Eden, Lichii, Porpoise and the systems' own programs): at most three, a crash's first, each its last lines (8 KB at most), scrubbed as the boot log. Only files the emulator wrote while the game ran, and only their names, never their folders |

## What a report never holds

Your PSN name, online ID or account ID; your console's serial number; IP or MAC addresses; your PS5
users' names; your RetroAchievements or SteamGridDB account, password, token or key; Switch room names,
passwords and nicknames; Wi-Fi names; network shares' addresses and logins; e-mail addresses; folder
names (every path is cut to `<path>/file.ext`); your saves; anything from a game's memory; your game
list.

The app scrubs the log and the error before a report is written (`ps5reports::Scrub`): the names it
knows (your PS5 users', your RetroAchievements user, room names and nicknames) wherever they are;
IPv4, IPv6 and MAC addresses; e-mail addresses; URLs and paths; what follows words such as `user`,
`token`, `password`, `ssid`, `serial`, `nickname` and `room`; and runs of 32 hex digits or more. The
intake scrubs the patterns again. `tools/tests/test-reports.sh` makes reports from a log full of
made-up personal data and fails if any of it is left.

## Where it goes

To the project's intake, a small service the owner runs (`tools/log-intake`). It keeps the reports
in a private database for 180 days, keeps nothing about the sender (its network address is used only
to limit how many reports it takes), and publishes one thing: a table with one row a game (how many
consoles and sessions, how they ended, the firmware and models seen), with no install ID and no log.
That table is on the project's site, [premohq.github.io/PlayTendoh/compatibility.html](https://premohq.github.io/PlayTendoh/compatibility.html),
rebuilt every hour. With the owner's own statuses (`tools/compat-list.json`) it also makes `status.json` there
(`tools/compat_feed.py`): Playable, Issues or Not playable for each game, which the app fetches as it
starts and shows on each game (Settings > Launcher > How games run turns the badges off). A game only the
reports know gets a status once two of its sessions ended from its menu, did not start or crashed: four in
five ended well, Playable; none did, Not playable; else Issues. The list's own say comes first.

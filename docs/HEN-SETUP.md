# Setting up your HEN for PS5CEMU-HAR

PS5CEMU-HAR works with any HEN. This page explains what the app needs, how it gets it, and the setup
for each HEN. After any change, check **Settings > Diagnostics**: its first lines say what the app
got.

## What the app needs

| Need | Why | How the app gets it |
| --- | --- | --- |
| `/data` | Settings, saves, games, caches and logs all live in `/data/ps5cemu` | The HEN's app jailbreak, or the bundled `sandbox-elevator.elf` run through elfldr |
| Executable memory | Cemu's and Azahar's recompilers write and run code; without it, Wii U games fall back to the much slower interpreter | The HEN's JIT memory when the HEN grants it, otherwise executable direct memory the app makes itself, which needs no HEN |
| USB and extended drives | Only if your games are there | The bundled `sandbox-elevator.elf` through elfldr, when a game folder or drive is refused |

At start, the app does this, in order:

1. It asks the HEN to jailbreak it, by writing `{"PID":n}` to `/download0/etahen_jailbreak`, the
   app jailbreak request etaHEN and OnionHEN both answer. The HEN only answers for title IDs in its
   allowlist, so `PPSA99360` has to be listed there.
2. It tries the HEN's JIT memory. If that isn't granted, it makes executable direct memory itself.
3. If `/data` is still out of reach, it asks elfldr (port 9021) to run `sandbox-elevator.elf`,
   which opens the filesystem. It does the same later for a game folder or drive that's refused.

## etaHEN (tested)

1. Load etaHEN.
2. Add `PPSA99360` to etaHEN's app jailbreak list, and turn on its app jailbreak option (in
   etaHEN's advanced or debug settings, depending on the version).
3. Start PS5CEMU-HAR. Diagnostics should say `HEN: ok`, `JIT available (the HEN's)` and
   `/data reachable`.

## OnionHEN

OnionHEN answers the same request as etaHEN, but its allowlist is a line in its `config.ini`:

1. Open OnionHEN's `config.ini` and find the `[app_jailbreak]` section.
2. Set `enabled=true`. Setting `debug_notifications=true` is worth it too: OnionHEN then shows a
   notification each time it jailbreaks an app, so you can see it worked.
3. Add `PPSA99360` to the **end** of `exact_title_ids`, with **no comma after it**:

   ```ini
   [app_jailbreak]
   enabled=true
   debug_notifications=true
   exact_title_ids=ITEM00001,NPXS39041,PKGI13337,PKGI12345,TOOL00001,PPSA99360
   ```

   OnionHEN drops the **whole list** if any entry is empty or isn't exactly 9 capital letters and
   digits. A trailing comma (`...,PPSA99360,`), a space-only entry or a lowercase ID all count, and
   then no app is jailbroken
   ([#12](https://github.com/premohq/PS5CEMU-HAR/issues/12)). It allows 20 entries at most.
4. Reload OnionHEN (or restart the console and load it again), so it reads the new list.
5. Start PS5CEMU-HAR. With the debug notifications on, OnionHEN says it jailbroke the app.
   Diagnostics should say `HEN: ok` and `/data reachable`.

## Any other HEN, or no app jailbreak

With a HEN that has no app jailbreak, or with only kstuff and an ELF loader, the app looks after
itself:

1. Load your HEN and make sure an ELF loader is listening on port 9021 (elfldr; etaHEN and OnionHEN
   setups usually have one).
2. Start PS5CEMU-HAR. It runs its bundled `sandbox-elevator.elf` through elfldr for `/data`, and the
   recompilers use executable direct memory.
3. Diagnostics should say `JIT available (executable direct memory, no HEN needed)` and
   `/data reachable`.

If no ELF loader is running, the app can't reach `/data` and says so at start: load one, then start
the app again.

## Games on USB or extended storage

Some HENs open `/data` but not the drives (seen on 13.x,
[#16](https://github.com/premohq/PS5CEMU-HAR/issues/16)). When a game folder or a plugged-in drive
is refused, the app runs `sandbox-elevator.elf` through elfldr to open it. Diagnostics then says
`... opened by the elevation helper`. This also needs an ELF loader on port 9021.

## Reading Diagnostics

| Diagnostics says | Meaning |
| --- | --- |
| `HEN: ok (HEN took the request after N ms ...)` | The HEN jailbroke the app |
| `HEN: no (no HEN took the request ...)` | No HEN answered: PPSA99360 isn't in its allowlist (check the comma for OnionHEN), or the HEN has no app jailbreak. The app carries on without one |
| `JIT available (the HEN's)` | The recompilers use the HEN's JIT memory |
| `JIT available (executable direct memory, no HEN needed)` | The recompilers use memory the app made itself: full speed, no HEN needed |
| `JIT unavailable (interpreter only)` | Neither worked: Wii U games run very slowly. Please report it with your firmware, HEN and `boot.log` |
| `/data reachable` | All good |
| `/data unreachable (elevation helper: N)` | Neither the HEN nor elfldr opened `/data`: load an ELF loader on port 9021, or put PPSA99360 in your HEN's allowlist |

When you report a problem, say which HEN and version you use, and attach the logs from **Settings >
Diagnostics > Copy logs to USB**.

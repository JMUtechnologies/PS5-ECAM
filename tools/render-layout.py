#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write the launcher's layouts (RmlUi documents), with nothing but the standard library.

    render-layout.py OUTPUT_UI

  start.rml   the start screen: the screen split down the middle, Cemu on the left over the Wii U
              Homebrew Launcher's bubbles, Azahar on the right over the 3DS one's waves, made yellow
  main.rml    Cemu's launcher: home, library, graphic packs, settings, about
  azahar.rml  Azahar's: the same screens, in its gold (tools/recolour-ui.py's yellow theme), for 3DS
              games, with what a 3DS has in place of the Wii U's graphic packs and players

The two launchers are one layout (ProsperoEden's, with its element ids and classes, so its
stylesheet applies, and port/frontend/ui/ps5cemu.rcss's additions), written for each emulator: its
artwork's folders, stylesheets, names and words. Their repeated rows are written out here, as are
the controller hints.
"""

import os
import re
import sys

WIIU = {
    "id": "wiiu",
    "chrome": "chrome", "icons": "icons", "suffix": "",
    "brand": "PS5 CEMU", "brand_icon": "icons/ps5cemu-72.tga", "cover": "icons/ps5cemu.tga",
    "system": "WII U", "footnote": "Cemu, the Wii U emulator, on PlayStation 5",
    "library_copy": "Every Wii U game in your game folder", "settings_copy": "How Cemu runs on your PS5",
    # planned, with their places in the layout: the video page's next row, and a game's tiles
    "video_soon": None,
    "online_soon": None,
    "features": [
        ("GamePad speaker", "GamePad speaker: the sounds games play on the GamePad, from the DualSense's speaker."),
        ("Manage data", "Manage data: see, delete, back up and restore each game's updates, DLC and saves."),
        ("Game list", "Game list: which Wii U games run well on the PS5, from players' reports."),
    ],
    "tiles_soon": [("Game options", "square")],  # its settings, updates, DLC and saves
    "detail": [("TITLE ID", "game-detail-format"), ("VERSION", "game-detail-size"), ("DLC", "game-detail-dlc")],
    "packs": True,
    "settings": [
        ("Video", "VIDEO", "Upscaling filter, 120 Hz, overlay"),
        ("Audio", "AUDIO", "Game volume, music, menu sounds"),
        ("Controls", "CONTROLS", "Controllers, buttons, motion, vibration"),
        ("Game files", "GAME FILES", "The folder for your Wii U games"),
        ("Install updates and DLC", "INSTALL", "Updates and DLC into the Wii U storage"),
        ("Diagnostics", "DIAGNOSTICS", "Jailbreak, JIT and log files"),
        # planned: in their places already, marked as coming
        ("Online and updates", "ONLINE", "Box art, graphic pack and app updates"),
    ],
    "files_copy": "Choose the folder that holds your Wii U games",
    "controls_copy": "Cemu's controller settings, for each player's DualSense",
    "credits": ("Powered by Cemu", "All credit for the Wii U emulator goes to the Cemu team and its contributors.", "cemu.info",
                "Mihawk-99 and mpereiraesaa (RADV on the PS5), BlackBearReloaded (ProsperoEden's launcher, the app "
                "boilerplate), Swordpdf (PS5SX2), John Tornblom (payload SDK), Dimok (the Homebrew Launcher), the graphic pack authors.",
                "An unofficial port. Not affiliated with the Cemu team, Nintendo or Sony."),
    "setup": [("GAMES", "about-games-path", "/data/ps5cemu/games"), ("KEYS", "about-keys-path", "/data/ps5cemu/keys.txt"),
              ("SAVES", "about-mlc-path", "/data/ps5cemu/mlc01")],
    "setup_note": "Dump your games from your own Wii U (.wua, .wud, .wux, or a folder with code, content and meta).",
}

N3DS = {
    "id": "3ds",
    "chrome": "chrome-3ds", "icons": "icons-3ds", "suffix": "-3ds",
    "brand": "PS5 AZAHAR", "brand_icon": "icons/azahar-72.tga", "cover": "icons/azahar.tga",
    "system": "NINTENDO 3DS", "footnote": "Azahar, the 3DS emulator, on PlayStation 5",
    "library_copy": "Your 3DS games, installed ones included", "settings_copy": "How Azahar runs on your PS5",
    "video_soon": None,  # Border is planned in its own settings category
    "online_soon": None,
    "features": [
        ("Microphone", "Microphone: the DualSense's microphone as the 3DS's, for the games that listen."),
        ("Camera", "Camera: the PS5 HD Camera as the 3DS cameras, for QR codes and the games that use them."),
        ("Home Menu", "Home Menu: start the 3DS Home Menu, once Artic Setup has copied your console's files."),
    ],
    "tiles_soon": [("Game options", "square")],  # its settings, saves and DLC (cheats are in the in-game menu)
    "detail": [("TITLE ID", "game-detail-format"), ("PUBLISHER", "game-detail-size"), ("FORMAT", "game-detail-dlc")],
    "packs": False,
    "settings": [
        ("Video", "VIDEO", "Internal resolution, screen layout, textures"),
        ("Audio", "AUDIO", "Game volume, music, menu sounds"),
        ("Controls", "CONTROLS", "Buttons, circle pad, motion"),
        ("Game files", "GAME FILES", "The folder for your 3DS games"),
        ("Install CIA files", "INSTALL", "CIA files into the 3DS storage"),
        ("Diagnostics", "DIAGNOSTICS", "Jailbreak, JIT and log files"),
        # planned: in their places already, marked as coming
        ("Borders", "BORDERS", "Artwork around the screens"),
        ("Camera and microphone", "CAMERA", "PS5 HD Camera, DualSense microphone", True),
        ("System", "SYSTEM", "Region, language and the Home Menu"),
        ("Online and updates", "ONLINE", "Box art downloads and app updates"),
    ],
    "files_copy": "Choose the folder that holds your 3DS games",
    "controls_copy": "Azahar's controls, on the DualSense",
    "credits": ("Powered by Azahar", "All credit for the 3DS emulator goes to the Azahar team and its contributors, who carry on Citra's work.",
                "azahar-emu.org",
                "Mihawk-99 (PS5_Azahar, PS5_Dynarmic, RADV on the PS5), merryhime (dynarmic), BlackBearReloaded (ProsperoEden's "
                "launcher, the app boilerplate), fincs and smealum (the 3DS Homebrew Launcher), John Tornblom (payload SDK).",
                "An unofficial port. Not affiliated with the Azahar team, Nintendo or Sony."),
    "setup": [("GAMES", "about-games-path", "/data/ps5cemu/azahar/games"), ("DATA", "about-keys-path", "/data/ps5cemu/azahar"),
              ("SAVES", "about-mlc-path", "/data/ps5cemu/azahar/sdmc")],
    "setup_note": "Dump your games from your own 3DS, decrypted: .3ds or .cci, .cxi, .cia or .3dsx.",
}


def chrome(side, kind, w, h):
    return (f'<img class="{kind}-chrome base-chrome" src="{side["chrome"]}/{kind}-normal.tga" width="{w}" height="{h}" alt=""/>'
            f'<img class="{kind}-chrome focused-chrome" src="{side["chrome"]}/{kind}-focused.tga" width="{w}" height="{h}" alt=""/>')


def rows(side, prefix, count, inner, extra_class=''):
    row = chrome(side, 'library-row', 760, 78)
    return '\n'.join(f'            <span id="{prefix}-row-{i}" class="library-row library-row-{i}{extra_class}">{row}{inner(i)}</span>'
                     for i in range(count))


def hint(side, icon, text, text_id=None, second=None):
    tid = f' id="{text_id}"' if text_id else ''
    folder = side["icons"]
    sec = f'<img class="hint-second" src="{folder}/{second}-mono.tga" width="26" height="26" alt=""/>' if second else ''
    cls = 'hint hint-pair' if second else 'hint'
    return (f'<span class="{cls}"><img src="{folder}/{icon}-mono.tga" width="26" height="26" alt=""/>{sec}'
            f'<span{tid} class="hint-text">{text}</span></span>')


def footer(*hints):
    return f'        <footer class="hints">{"".join(hints)}</footer>'


def head(side):
    return f'''<rml>
  <!-- PS5CEMU-HAR's launcher (tools/render-layout.py). Layout, artwork, fonts and stylesheet are
       ProsperoEden's (headless/prosperoeden/ui, GPL-3.0-or-later, by BlackBearReloaded), adapted,
       recoloured and reshaped (tools/recolour-ui.py); ps5cemu.rcss adds what is new. Element ids
       and classes follow ProsperoEden's so its styles apply. The background is drawn under the page. -->
  <head>
    <title>PS5CEMU-HAR</title>
    <link type="text/rcss" href="styles/app{side["suffix"]}.rcss" />
    <link type="text/rcss" href="styles/ps5cemu{side["suffix"]}.rcss" />
  </head>
  <body>
    <div id="app-shell">'''


TAIL = '''    </div>
  </body>
</rml>
'''


def launcher(side):
    S = side
    c = S["chrome"]
    PANEL = f'<img class="library-panel-chrome" src="{c}/dialog-panel.tga" width="820" height="720" alt=""/>'
    MODAL = f'<img class="dialog-panel-chrome" src="{c}/modal-panel.tga" width="820" height="720" alt=""/>'
    DLG = chrome(S, 'dialog-row', 736, 94)
    LIB = chrome(S, 'library-row', 760, 78)
    h = lambda *args, **kw: hint(S, *args, **kw)
    out = [head(S)]
    add = out.append
    add(f'''      <header id="header">
        <img id="brand-icon" src="{S["brand_icon"]}" width="72" height="72" alt=""/>
        <span id="brand-name">{S["brand"]}</span>
        <span id="brand-version">{S["system"]}  /  PS5CEMU-HAR</span>
      </header>
      <nav id="menu">
        <button id="load-rom" class="nav-item"><img class="nav-focus" src="{c}/nav-focused.tga" width="136" height="64" alt=""/><span>Library</span></button>
        <button id="settings" class="nav-item"><img class="nav-focus" src="{c}/nav-focused.tga" width="136" height="64" alt=""/><span>Settings</span></button>
        <button id="help-about" class="nav-item"><img class="nav-focus" src="{c}/nav-focused.tga" width="136" height="64" alt=""/><span>About</span></button>
      </nav>

      <main id="last-played-card">
        <img class="hero-chrome" src="{c}/cover-frame.tga" width="360" height="360" alt=""/>
        <img id="last-played-cover" src="{S["cover"]}" width="336" height="336" alt=""/>
        <span class="hero-kicker">LAST PLAYED</span>
        <span id="last-played-title">Nothing played yet</span>
        <span id="last-played-caption">Start a game from the library and it shows up here.</span>
        <button id="continue-game" class="hero-button"><img class="hero-button-chrome base-chrome" src="{c}/hero-button.tga" width="272" height="72" alt=""/><img class="hero-button-chrome focused-chrome" src="{c}/hero-button-focused.tga" width="272" height="72" alt=""/><img class="hero-glyph" src="{S["icons"]}/cross-mono.tga" width="26" height="26" alt=""/><span id="continue-copy">Launch game</span></button>''')
    # the hero's second button: a game's graphic packs on the Wii U's side, Artic Base (a game from
    # a 3DS on the network) on the 3DS's
    add(f'''        <button id="hero-options" class="hero-button"><img class="hero-button-chrome base-chrome" src="{c}/hero-button.tga" width="272" height="72" alt=""/><img class="hero-button-chrome focused-chrome" src="{c}/hero-button-focused.tga" width="272" height="72" alt=""/><img class="hero-glyph" src="{S["icons"]}/triangle-mono.tga" width="26" height="26" alt=""/><span>{"Graphic packs" if S["packs"] else "Artic Base"}</span></button>''')
    add(f'''        <span class="hero-footnote">{S["footnote"]}</span>
      </main>

      <section id="recent-section">
        <span class="section-label">RECENT GAMES</span>
        <button id="view-all"><img class="view-all-focus" src="{c}/view-all-focused.tga" width="304" height="40" alt=""/><span>FULL LIBRARY</span></button>
        <span id="recent-empty">The games you play show up here.</span>''')
    for i in range(4):
        add(f'        <button id="recent-{i}" class="recent-tile"><img class="recent-chrome base-chrome" src="{c}/recent-square-normal.tga" width="200" height="250" alt=""/>'
            f'<img class="recent-chrome focused-chrome" src="{c}/recent-square-focused.tga" width="200" height="250" alt=""/>'
            f'<img id="recent-cover-{i}" class="recent-cover" src="{S["cover"]}" width="96" height="96" alt=""/><span id="recent-title-{i}" class="recent-title"></span></button>')
    home_hints = [h('cross', 'Select'), h('triangle', 'Graphic packs' if S["packs"] else 'Artic Base'), h('dpad', 'Navigate'), h('circle', 'Change emulator')]
    add('      </section>')
    # the roadmap's features, in their places on the home screen until they are made: launcher.cpp
    # moves the focus through them and shows the one in focus's caption (data-caption)
    add('''      <section id="features-section">
        <span class="section-label">COMING SOON</span>''')
    for i, (name, caption) in enumerate(S["features"]):
        add(f'        <button id="feature-{i}" class="feature-tile" data-caption="{caption}">'
            f'<img class="feature-chrome base-chrome" src="{c}/feature-chip-normal.tga" width="312" height="64" alt=""/>'
            f'<img class="feature-chrome focused-chrome" src="{c}/feature-chip-focused.tga" width="312" height="64" alt=""/>'
            f'<span class="feature-name">{name}</span><span class="feature-tag">SOON</span></button>')
    add(f'''        <span id="feature-caption">Planned for later versions: choose one to see what it will do.</span>
      </section>

      <div id="startup-status" class="quiet"></div>
      <footer id="footer" class="hints">{"".join(home_hints)}<span id="system-status">Looking for games</span></footer>

      <!-- Library -->
      <div id="rom-dialog" class="library-screen">
        <span class="library-shade"></span>
        <span class="library-title">Library</span>
        <span class="library-copy">{S["library_copy"]}</span>
        <section class="library-list-panel">
          {PANEL}
          <span class="library-panel-kicker">GAMES</span>
          <div id="rom-list-viewport"><div id="rom-list-track">
{rows(S, 'rom', 7, lambda i: f'<img id="rom-icon-{i}" class="rom-row-icon" src="{S["cover"]}" width="56" height="56" alt=""/><span id="rom-name-{i}" class="library-row-name rom-row-name"></span><span id="rom-format-{i}" class="library-row-meta"></span>')}
          </div></div>
          <span id="library-empty">No games found. Put them in the game files folder.</span>
          <span id="rom-scrollbar"><img class="scrollbar-track" src="{c}/scrollbar-track.tga" width="10" height="606" alt=""/><img id="rom-scrollbar-thumb" src="{c}/scrollbar-thumb.tga" width="10" height="110" alt=""/></span>
          <span id="library-position">0 OF 0</span>
        </section>
        <aside class="game-detail-panel">
          {PANEL}
          <span class="detail-kicker">ABOUT THIS GAME</span>
          <span id="game-detail-title">No game selected</span>
          <img id="game-cover" src="{S["cover"]}" width="288" height="288" alt=""/><span id="cover-caption"></span>''')
    for i, (label, value_id) in enumerate(S["detail"]):
        add(f'          <span class="game-detail-label game-detail-line-{i}">{label}</span><span id="{value_id}" class="game-detail-value game-detail-line-{i}">-</span>')
    add('''          <span class="game-path-label">FILES</span><span id="game-detail-path" class="game-path">-</span>''')
    # a game's tiles: its graphic packs on the Wii U's side, then what is planned (tiles_soon)
    tiles, tile_hints = [], []
    if S["packs"]:
        tiles.append(f'<span id="game-packs-setting" class="game-tile game-tile-0"><img src="{c}/tile-normal.tga" width="360" height="94" alt=""/>'
                     f'<span class="game-mode-label">Graphic packs</span><span id="game-packs-value" class="game-tile-value">-</span></span>')
        tile_hints.append(h('triangle', 'Graphic packs'))
    for name, button in S["tiles_soon"]:
        tiles.append(f'<span class="game-tile game-tile-{len(tiles)} planned"><img src="{c}/tile-normal.tga" width="360" height="94" alt=""/>'
                     f'<span class="game-mode-label">{name}</span><span class="game-tile-value">Coming soon</span></span>')
        tile_hints.append(h(button, f'{name} (soon)'))
    add('          ' + '\n          '.join(tiles))
    add(f'          <span id="game-tile-hints" class="tile-hints">{"".join(tile_hints)}</span>')
    library_hints = [h('cross', 'Play'), h('circle', 'Back'), h('updown', 'Browse games')] + ([h('triangle', 'Graphic packs')] if S["packs"] else []) + [h('l1', 'Page', second='r1')]
    add(f'''        </aside>
{footer(*library_hints)}
      </div>
''')
    if S["packs"]:
        add(f'''      <!-- Graphic packs of one game, laid out as Cemu's Graphic Packs window: the packs, and each
           one's presets, a dropdown for each of its categories -->
      <div id="packs-dialog" class="library-screen">
        <span class="library-shade"></span>
        <span class="library-title">Graphic packs</span>
        <span id="packs-game" class="library-copy"></span>
        <section class="library-list-panel">
          {PANEL}
          <span class="library-panel-kicker">COMMUNITY PACKS</span>
          <div id="packs-list-viewport">
{rows(S, 'pack', 7, lambda i: f'<span id="pack-name-{i}" class="library-row-name pack-row-name"></span><span id="pack-state-{i}" class="library-row-meta pack-row-state"></span>')}
          </div>
          <span id="packs-empty">No graphic packs for this game.</span>
          <span id="packs-position">0 OF 0</span>
        </section>
        <aside class="game-detail-panel">
          {PANEL}
          <span id="pack-detail-kicker" class="detail-kicker">GRAPHIC PACK</span>
          <span id="pack-detail-title"></span>
          <span id="pack-detail-description"></span>
          <span class="about-divider pack-divider"></span>
          <span id="presets-kicker" class="detail-kicker presets-kicker">PRESETS</span>
          <span id="presets-empty">This pack has no presets: it is only on or off.</span>
          <div id="presets-viewport">
{rows(S, 'preset', 4, lambda i: f'<span id="preset-label-{i}" class="preset-label"></span><span id="preset-value-{i}" class="preset-value"></span><span class="preset-arrow"></span>', ' preset-row')}
          </div>
          <span id="presets-position"></span>
        </aside>
{footer(h('cross', 'On / off', 'packs-hint-0'), h('circle', 'Back', 'packs-hint-1'), h('updown', 'Browse packs', 'packs-hint-2'), h('leftright', 'Presets', 'packs-hint-3'))}
      </div>
''')
    add(f'''      <!-- Settings -->
      <div id="settings-dialog" class="settings-screen">
        <span class="library-shade"></span>
        <span class="library-title">Settings</span><span class="library-copy">{S["settings_copy"]}</span>
        <section class="settings-list-panel">
        {PANEL}
        <span class="library-panel-kicker">OPTIONS</span>''')
    # every category, the planned ones (True) marked and in their places already; launcher.cpp
    # scrolls them as it does the library's rows
    add('        <div id="settings-viewport" class="list-viewport">')
    for i, (name, _, _, *planned) in enumerate(S["settings"]):
        mark = ' planned' if planned else ''
        add(f'          <span id="settings-row-{i}" class="library-row{mark}" style="top: {i * 88}px;">{LIB}'
            f'<span class="library-row-name">{name}</span><span class="library-row-meta">{"SOON" if planned else ""}</span></span>')
    add(f'''        </div>
        <span id="settings-position" class="list-position">1 OF {len(S["settings"])}</span>
        </section>
        <aside class="settings-detail-panel">{PANEL}
          <span class="detail-kicker">OVERVIEW</span>
          <span class="settings-detail-title">At a glance</span>
          <span class="settings-detail-copy">These apply to every game. In a game, touchpad + Options changes some of them.</span>''')
    for i, (_, label, value, *planned) in enumerate(S["settings"]):
        mark = ' planned' if planned else ''
        top = f' style="top: {236 + i * 40}px;"'
        add(f'          <span class="settings-detail-label{mark}"{top}>{label}</span>'
            f'<span class="settings-detail-value{mark}"{top}>{"Soon: " if planned else ""}{value}</span>')
    add(f'''        </aside>
{footer(h('cross', 'Select'), h('circle', 'Back'), h('updown', 'Browse settings'))}
      </div>

      <!-- Settings > Controls: the players -->
      <div id="controls-dialog" class="library-screen">
        <span class="library-shade"></span>
        <span class="library-title">Controls</span>
        <span class="library-copy">{S["controls_copy"]}</span>
        <section class="library-list-panel">
          {PANEL}
          <span class="library-panel-kicker">PLAYERS</span>
          <div class="list-viewport">
{rows(S, 'control', 4, lambda i: f'<span id="control-name-{i}" class="library-row-name setting-name">Player {i + 1}</span><span id="control-value-{i}" class="setting-value"></span>')}
          </div>
        </section>
        <aside class="game-detail-panel">
          {PANEL}
          <span id="control-kicker" class="detail-kicker">PLAYER 1</span>
          <span id="control-title" class="detail-title"></span>''')
    for i in range(3):
        add(f'          <span id="control-label-{i}" class="files-label files-line-{i}"></span><span id="control-detail-{i}" class="files-value files-line-{i} ready"></span>')
    add(f'''          <span class="about-divider files-divider-0"></span>
          <span id="control-help" class="detail-help">Touchpad: a cursor on the GamePad's screen; click to touch.<br/>Touchpad click + Options: the in-game menu.<br/>Touchpad click + L1: the TV or the GamePad as the main screen.<br/>Touchpad click + R1: the other screen in a corner.</span>
        </aside>
{footer(h('cross', 'Settings'), h('circle', 'Back'), h('updown', 'Browse players'))}
      </div>

      <!-- Settings > Controls > a player -->
      <div id="player-dialog" class="library-screen">
        <span class="library-shade"></span>
        <span id="player-title" class="library-title">Player 1</span>
        <span id="player-copy" class="library-copy"></span>
        <section class="library-list-panel">
          {PANEL}
          <span class="library-panel-kicker">SETTINGS</span>
          <div class="list-viewport">
{rows(S, 'player', 7, lambda i: f'<span id="player-name-{i}" class="library-row-name setting-name"></span><span id="player-value-{i}" class="setting-value"></span>')}
          </div>
        </section>
        <aside class="game-detail-panel">
          {PANEL}
          <span class="detail-kicker">THIS SETTING</span>
          <span id="player-detail-title" class="detail-title"></span>
          <span id="player-help" class="detail-help detail-help-high"></span>
        </aside>
{footer(h('cross', 'Choose', 'player-hint-0'), h('leftright', 'Change'), h('circle', 'Back'), h('updown', 'Browse'))}
      </div>

      <!-- Settings > Controls > buttons -->
      <div id="mapping-dialog" class="library-screen">
        <span class="library-shade"></span>
        <span class="library-title">Buttons</span>
        <span id="mapping-copy" class="library-copy"></span>
        <section class="library-list-panel">
          {PANEL}
          <span id="mapping-kicker" class="library-panel-kicker"></span>
          <div class="list-viewport">
{rows(S, 'map', 7, lambda i: f'<span id="map-name-{i}" class="library-row-name setting-name"></span><span id="map-value-{i}" class="setting-value"></span>')}
          </div>
          <span id="mapping-position" class="list-position">0 OF 0</span>
        </section>
        <aside class="game-detail-panel">
          {PANEL}
          <span class="detail-kicker">THIS BUTTON</span>
          <span id="map-title" class="detail-title"></span>
          <span class="files-label files-line-0">DUALSENSE</span><span id="map-input" class="files-value files-line-0 ready"></span>
          <span class="about-divider files-divider-0"></span>
          <span id="map-message" class="detail-help"></span>
        </aside>
{footer(h('cross', 'Assign'), h('square', 'Clear'), h('circle', 'Back'), h('updown', 'Browse buttons'), h('l1', 'Page', second='r1'))}
      </div>

      <!-- Settings > Game files, and Settings > Install: the file browser -->
      <div id="files-dialog" class="library-screen">
        <span class="library-shade"></span>
        <span id="files-title" class="library-title">Game files</span>
        <span id="files-copy" class="library-copy">{S["files_copy"]}</span>
        <section class="library-list-panel">
          {PANEL}
          <span class="library-panel-kicker">FOLDERS</span>
          <span id="files-path"></span>
          <div id="files-list-viewport">''')
    for i in range(6):
        add(f'            <span id="files-row-{i}" class="library-row library-row-{i} files-row">{LIB}'
            f'<img class="files-icon files-icon-folder" src="{S["icons"]}/folder.tga" width="40" height="32" alt=""/>'
            f'<img class="files-icon files-icon-up" src="{S["icons"]}/folder-up.tga" width="40" height="32" alt=""/>'
            f'<span id="files-name-{i}" class="library-row-name files-row-name"></span><span id="files-meta-{i}" class="library-row-meta files-row-meta"></span></span>')
    add(f'''          </div>
          <span id="files-empty">No folders here.</span>
          <span id="files-position">0 OF 0</span>
        </section>
        <aside class="game-detail-panel">
          {PANEL}
          <span id="files-kicker" class="detail-kicker">THIS FOLDER</span>
          <span id="files-current"></span>''')
    for i in range(3):
        add(f'          <span id="files-label-{i}" class="files-label files-line-{i}"></span><span id="files-value-{i}" class="files-value files-line-{i}"></span>')
    add(f'''          <span class="about-divider files-divider-0"></span>
          <span id="files-message"></span>
        </aside>
{footer(h('cross', 'Open'), h('circle', 'Back'), h('triangle', 'Use this folder', 'files-hint-use'), h('l1', 'Page', second='r1'))}
      </div>

      <!-- Settings > Video -->
      <div id="video-dialog" class="dialog"><div class="dialog-panel">
        {MODAL}
        <span class="dialog-title">Video</span>
        <span class="dialog-copy">Applies to the next game you start.</span>''')
    for i in range(4):
        add(f'        <span id="video-row-{i}" class="dialog-row dialog-row-{i}">{DLG}<span id="video-label-{i}" class="dialog-row-label"></span><span id="video-value-{i}" class="dialog-row-value"></span></span>')
    if S["video_soon"]:
        add(f'        <span class="dialog-row dialog-row-4 planned">{DLG}<span class="dialog-row-label">{S["video_soon"]}</span><span class="dialog-row-value">Soon</span></span>')
    credit = S["credits"]
    add(f'''        <div class="dialog-hints">{h('updown', 'Select')}{h('leftright', 'Change')}{h('circle', 'Back')}</div>
      </div></div>

      <!-- Settings > Audio -->
      <div id="audio-dialog" class="dialog"><div class="dialog-panel">
        {MODAL}
        <span class="dialog-title">Audio</span>
        <span class="dialog-copy">Your games' sound, and the launcher's own music and menu sounds.</span>''')
    # the game's volume is the emulator's; the music (PS5CEMU-HAR's own, tools/render-sounds.py) and
    # the menu's sounds are the launcher's, on both sides
    for i, (label, value_id, value) in enumerate((("Game volume", "audio-volume", "100%"), ("Launcher music", "audio-music", "Shop theme"),
                                                  ("Music volume", "audio-music-volume", "50%"), ("Menu sounds", "audio-menu-sounds", "On"))):
        add(f'        <span id="audio-row-{i}" class="dialog-row dialog-row-{i}">{DLG}<span class="dialog-row-label">{label}</span><span id="{value_id}" class="dialog-row-value">{value}</span></span>')
    add(f'''        <div class="dialog-hints">{h('updown', 'Select')}{h('leftright', 'Change')}{h('circle', 'Back')}</div>
      </div></div>

      <!-- Settings > Diagnostics -->
      <div id="diagnostics-dialog" class="dialog"><div class="dialog-panel">
        {MODAL}
        <span class="dialog-title">Diagnostics</span>
        <span class="dialog-copy">Attach the log files when you report a problem.</span>
        <div id="setup-details" class="settings-details diagnostics-details"></div>
        <span id="diag-row-0" class="dialog-row dialog-row-0">{DLG}<span class="dialog-row-label">Copy logs to USB</span><span id="diag-value-0" class="dialog-row-value"></span></span>
        <span id="diag-row-1" class="dialog-row dialog-row-1">{DLG}<span id="diag-label-1" class="dialog-row-label">Clear shader caches</span><span id="diag-value-1" class="dialog-row-value"></span></span>
        <div class="dialog-hints">{h('updown', 'Select')}{h('cross', 'Choose')}{h('circle', 'Back')}</div>
      </div></div>

      <!-- Settings > System (the 3DS's) -->
      <div id="system-dialog" class="dialog"><div class="dialog-panel">
        {MODAL}
        <span class="dialog-title">System</span>
        <span class="dialog-copy">The emulated 3DS. Applies to the next game you start.</span>
        <span id="system-row-0" class="dialog-row dialog-row-0">{DLG}<span class="dialog-row-label">Region</span><span id="system-region" class="dialog-row-value">Automatic</span></span>
        <span id="system-row-1" class="dialog-row dialog-row-1">{DLG}<span class="dialog-row-label">Language</span><span id="system-language" class="dialog-row-value">Automatic</span></span>
        <span class="dialog-row dialog-row-2 planned">{DLG}<span class="dialog-row-label">Home Menu</span><span class="dialog-row-value">Soon</span></span>
        <div class="dialog-hints">{h('updown', 'Select')}{h('leftright', 'Change')}{h('circle', 'Back')}</div>
      </div></div>

      <!-- Settings > Borders (the 3DS's) -->
      <div id="borders-dialog" class="dialog"><div class="dialog-panel">
        {MODAL}
        <span class="dialog-title">Borders</span>
        <span class="dialog-copy">Artwork around the 3DS screens, never over them. It follows every layout, and the in-game menu changes it too.</span>
        <span id="borders-row-0" class="dialog-row dialog-row-0">{DLG}<span class="dialog-row-label">Border</span><span id="borders-theme" class="dialog-row-value">None</span></span>
        <div class="dialog-hints">{h('leftright', 'Change')}{h('circle', 'Back')}</div>
      </div></div>

      <!-- Settings > Online and updates -->
      <div id="online-dialog" class="dialog"><div class="dialog-panel">
        {MODAL}
        <span class="dialog-title">Online and updates</span>
        <span class="dialog-copy">What the app fetches from the internet. It also tells you when a newer PS5CEMU-HAR is out.</span>
        <span id="online-row-0" class="dialog-row dialog-row-0">{DLG}<span class="dialog-row-label">Box art from GameTDB</span><span id="online-boxart" class="dialog-row-value">On</span></span>
        <div class="dialog-hints">{h('leftright', 'Change')}{h('circle', 'Back')}</div>
      </div></div>

      <!-- About -->
      <div id="about-dialog" class="library-screen about-screen">
        <span class="library-shade"></span>
        <span class="library-title">About {S["brand"]}</span>
        <span class="library-copy">Credits and setup</span>
        <section class="about-credit-panel">
          {PANEL}
          <span class="detail-kicker">PROJECT CREDITS</span>
          <span class="about-lead about-eden-lead">{credit[0]}</span>
          <span class="about-body about-eden">{credit[1]}</span>
          <span class="about-link about-eden-link">{credit[2]}</span>
          <span class="about-divider about-divider-vulkan"></span>
          <span class="about-subhead about-vulkan-head">THANKS</span>
          <span class="about-body about-vulkan">{credit[3]}</span>
          <span class="about-divider about-divider-port"></span>
          <span class="about-subhead about-port-head">PS5 EDITION</span>
          <span class="about-body about-port">{credit[4]}</span>
        </section>
        <aside class="about-setup-panel">
          {PANEL}
          <span class="detail-kicker">GETTING STARTED</span>
          <span class="about-lead">Supply your own files</span>''')
    for (label, value_id, value), cls in zip(S["setup"], ("about-keys", "about-firmware", "about-games")):
        add(f'          <span class="about-setup-label {cls}">{label}</span><span id="{value_id}" class="about-setup-path {cls}">{value}</span>')
    add(f'''          <span class="about-divider about-setup-divider"></span>
          <span class="about-note">{S["setup_note"]}</span>
        </aside>
{footer(h('circle', 'Back'))}
      </div>

      <!-- A dropdown's choices, over the right-hand panel -->
      <div id="picker">
        <div class="picker-panel">
          {MODAL}
          <span id="picker-kicker" class="detail-kicker"></span>
          <span id="picker-title" class="detail-title"></span>
          <div id="picker-viewport">
{rows(S, 'picker', 6, lambda i: f'<span id="picker-name-{i}" class="library-row-name picker-name"></span><span id="picker-mark-{i}" class="library-row-meta picker-mark"></span>')}
          </div>
          <div class="picker-hints">{h('cross', 'Choose')}{h('circle', 'Cancel')}</div>
          <span id="picker-position"></span>
        </div>
      </div>

      <!-- Starting a game -->
      <div id="loading-screen" class="library-screen">
        <span class="library-shade"></span>
        <img id="loading-cover" src="{S["cover"]}" width="288" height="288" alt=""/>
        <span id="loading-title"></span>
        <span id="loading-caption">Starting</span>
      </div>''')
    # a thin bar across the top of every screen, the time in its middle (ps5cemu.rcss keeps it on top)
    add('      <div id="top-bar"><span id="menu-clock">--:--</span></div>')
    out.append(TAIL)
    # each page's title with PS5CEMU-HAR's accent bar beside it
    return re.sub(r'(<span (?:id="[^"]+" )?class="library-title")',
                  rf'<img class="title-bar" src="{c}/title-bar.tga" width="6" height="80" alt=""/>\1', '\n'.join(out))


def start():
    """The two emulators side by side, each in its own colours."""
    out = [head(WIIU)]
    add = out.append
    add('''      <header id="start-header">
        <img id="start-brand-icon" src="icons/har-72.tga" width="72" height="72" alt=""/>
        <span id="start-brand-name">PS5CEMU-HAR</span>
        <span id="start-brand-copy">Cemu and Azahar  /  Wii U and 3DS games on PlayStation 5</span>
      </header>
      <span id="start-divider"></span>''')
    for side, name, system, art, left in ((WIIU, "Cemu", "Wii U", "start-wiiu", 100), (N3DS, "Azahar", "Nintendo 3DS", "start-3ds", 1000)):
        c = side["chrome"]
        add(f'''      <section id="start-{side["id"]}" class="start-side start-{side["id"]}">
        <img class="start-chrome base-chrome" src="{c}/start-panel.tga" width="820" height="720" alt=""/>
        <img class="start-chrome focused-chrome" src="{c}/start-panel-focused.tga" width="820" height="720" alt=""/>
        <img class="start-art" src="icons/{art}.tga" width="440" height="440" alt=""/>
        <span class="start-name">{name}</span>
        <span class="start-system">{system.upper()}</span>
        <span id="start-status-{side["id"]}" class="start-status"></span>
        <span class="start-button"><img class="hero-button-chrome base-chrome" src="{c}/hero-button.tga" width="272" height="72" alt=""/><img class="hero-button-chrome focused-chrome" src="{c}/hero-button-focused.tga" width="272" height="72" alt=""/><span>Start {name}</span></span>
      </section>''')
    add(f'''      <footer id="start-footer" class="hints">{hint(WIIU, 'cross', 'Start')}{hint(WIIU, 'leftright', 'Choose')}</footer>''')
    # a thin bar across the top of every screen, the time in its middle (ps5cemu.rcss keeps it on top)
    add('      <div id="top-bar"><span id="menu-clock">--:--</span></div>')
    out.append(TAIL)
    return '\n'.join(out)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, text in (("start.rml", start()), ("main.rml", launcher(WIIU)), ("azahar.rml", launcher(N3DS))):
        with open(os.path.join(out, name), "w", newline="\n") as file:
            file.write(text)


if __name__ == "__main__":
    main()

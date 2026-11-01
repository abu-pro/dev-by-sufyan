# Dev by Sufyan

<img src="brand/devbysufyan-logo.png" alt="Dev by Sufyan logo" width="120">

A Dev-C++-style code editor for **C, C++ and Python** with live **Rooms** built in — create a room, share the 4-digit code, and code together in real time.

**Made by Abu Sufyan.** Built on the Red Panda C++ IDE (see [Credits](#credits)).

**Current version: v1.5.0**

## Download

| File | What it is | Size |
|---|---|---|
| [DevBySufyan-Setup.exe](https://github.com/abu-pro/dev-by-sufyan/releases/download/v1.5.0/DevBySufyan-Setup.exe) | Windows installer (Start Menu shortcut + uninstaller) | 164.5 MB |
| [DevBySufyan-Portable.zip](https://github.com/abu-pro/dev-by-sufyan/releases/download/v1.5.0/DevBySufyan-Portable.zip) | Portable folder — unzip and run, no install | 271.9 MB |

- **Website:** https://devbysufyan.vercel.app
- **Android app:** the phone app is at v1.3.0 — get `DevBySufyan.apk` from the [v1.3.0 release](https://github.com/abu-pro/dev-by-sufyan/releases/tag/v1.3.0). It shares the same live rooms as the PC app.
- The PC installer includes a MinGW GCC compiler, so **Compile & Run (F9)** works right after installing — nothing else to set up.
- Windows may show a SmartScreen "unknown publisher" warning because the app is not code-signed yet: choose **More info → Run anyway**.

## Rooms — code together live

- The creator makes a room and gets a **4-digit code**; everyone else joins with that code and picks their own display name.
- **Everyone can view everyone's code**, synced live as they type.
- The **admin** (room creator) can view *and edit* everyone's code, set the member limit (up to 60), restrict a member's editing, and kick members. The admin's own code is **view-only** for everyone else.
- WhatsApp-style **chat**, member colours, live typing — and if you leave and rejoin with the same code, you get **your old seat, name and code back**.
- **Live cursors:** see each member's caret move through their code, with their name flag on the line.
- **Admin line pointing:** the admin can point a member to an exact line — their screen jumps to it, highlighted.
- **Side by Side:** put a member's live code next to yours.
- **Run All & Compare (admin):** run every member's program on one shared input and compare all the outputs in one table.
- **Live ping indicator:** the Rooms panel shows a coloured dot + milliseconds — your live connection speed to the room server (green = good, yellow = slow, red = very slow or no reply).

## Features

- **Home screen** — a real Start tab like MS Word: New / Open, your recent files, and a rotating anime welcome with five themes (Sukuna, Zenitsu, Naruto × Sasuke, Luffy Gear 5, Gojo) whose colours carry through the whole app.
- **Auto-save + crash recovery** — files save themselves every 30 seconds; drafts (even untitled) are kept, and after a crash the app offers to restore them.
- **Compiler errors in easy words** — double-click an error and get a plain-English explanation of what it means and how to fix it — 107 common errors covered, for GCC/MinGW C/C++ and Python. It is a **built-in offline dictionary — no AI, no internet needed**, and the app stays completely free.
- **Online update** — when a new version is released on GitHub, the app tells you on the Home screen at startup and you can download the update right from there — from v1.5.0 the update downloads and installs from inside the app, no browser needed; Help → Check for Updates checks any time.
- **Code photo** — save your code as a clean PNG picture (your theme and font, 1x/2x) for sharing.
- **Flowchart** — turn simple C++ code (sequence, input/output, if/else, loops, return) into a flowchart picture and save it as PNG.
- Everything from the Red Panda editor underneath: fast C/C++/Assembly editing, code completion, folding, debugging with watches, dark themes and high-DPI support.

## Free and offline

Dev by Sufyan is **free**, and its smart features work **offline** — no accounts, no AI services, no subscriptions.

## Version history

- **v1.5.0** — easy-words error dictionary more than doubled (107 entries, now including the common Python errors), live ping indicator in Rooms, room member limit up to 60, updates download + install from inside the app
- **v1.4.0** — the nine-features release: auto-save + recovery, easy-words error explanations, online update, live cursors, admin line pointing, Side by Side, Run All & Compare, code photo, flowchart
- **v1.3.2** — Home became a real permanent Start tab with Recent Files; compact Rooms menus
- **v1.3.1** — startup welcome fixed (full-screen anime art with matching accents), room seat restore on rejoin, transparent logo
- **v1.3.0** — anime themes + welcome screen, tabbed Rooms panel, per-member edit restrictions, Android chat improvements
- **v1.2.0** — bundled MinGW compiler, WhatsApp-style chat, connection keepalive + resume
- **v1.0** — first release: Red Panda IDE with live Rooms, rebranded as Dev by Sufyan

## Credits

- **Made by Abu Sufyan** — Rooms design and integration, branding, themes, and the features above.
- Based on **Red Panda C++** by Roy Qu and contributors — https://github.com/royqh1979/RedPanda-CPP
- Rooms run on a small live server shared by the PC, web and Android versions.

## License

GNU GPL v3 — see [LICENSE](LICENSE) in this repo. The original authors and the GPL are preserved, as the license requires.

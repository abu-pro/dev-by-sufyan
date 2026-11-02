# Dev by Sufyan

<img src="brand/devbysufyan-logo.png" alt="Dev by Sufyan logo" width="120">

A Dev-C++-style code editor for **C, C++ and Python** with live **Rooms** built in — create a room, share the 4-digit code, and code together in real time.

**Made by Abu Sufyan.** Built on the Red Panda C++ IDE (see [Credits](#credits)).

**Current version: v1.6.0**

## Download

| File | What it is | Size |
|---|---|---|
| [DevBySufyan-Setup.exe](https://github.com/abu-pro/dev-by-sufyan/releases/download/v1.6.0/DevBySufyan-Setup.exe) | Windows installer (Start Menu shortcut + uninstaller) | 167.3 MB |
| [DevBySufyan-Portable.zip](https://github.com/abu-pro/dev-by-sufyan/releases/download/v1.6.0/DevBySufyan-Portable.zip) | Portable folder — unzip and run, no install | 274.7 MB |

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
- **Live ping indicator:** the Rooms panel shows a coloured dot + milliseconds — your live connection speed to the room server (green = good, yellow = slow, red = very slow or no reply). The web app shows the same indicator in its room view.

## Features

- **Home screen** — a real Start tab like MS Word: New / Open, **Program Templates** (Multiplication Table, Factorial, Prime Check, Simple Calculator, Sum & Average — in C++, C and Python), your recent files, and a rotating anime welcome with five themes (Sukuna, Zenitsu, Naruto × Sasuke, Luffy Gear 5, Gojo) whose colours carry through the whole app. With one of the three big themes below active, Home goes **full-screen** — wallpaper edge to edge, glass tiles over it, and the editor panels return the moment you open a file.
- **Auto-save + crash recovery** — files save themselves every 30 seconds; drafts (even untitled) are kept, and after a crash the app offers to restore them.
- **Compiler errors in easy words — and Fix Lagao one-click fixes** — double-click an error and get a plain-English explanation of what it means and how to fix it; when the repair is certain (missing `;`, `)`, `}`, a Python `:`, a missing `#include`, the compiler's own "did you mean"), a **Fix it for me** button applies it for you (Ctrl+Z undoes it), and **Fix & Recheck** recompiles and tells you honestly whether that error is gone. **210 errors covered**, for GCC/MinGW C/C++ and Python — and Help → **Error Dictionary** lets you search and browse them all without compiling. It is a **built-in offline dictionary — no AI, no internet needed**, and the app stays completely free.
- **Online update** — when a new version is released on GitHub, the app tells you on the Home screen at startup and you can download the update right from there; Help → Check for Updates checks any time. **Rebuilt in v1.6.0:** downloads go to a permanent folder and **resume where they stopped** if the connection drops or stalls (a watchdog restarts a stuck download automatically from the last byte), the finished size is verified before anything runs, and when the download completes a panel shows the installer's exact saved path with an **Open Installer** button — so the install is always one click away.
- **Three full anime themes + GTA-style loading screen** — **Demon Slayer**, **Dragon Ball** and **My Hero Academia** (Themes menu): each brings its own colours, curated wallpapers behind the whole app (the editor stays readable), a full-screen loading screen with theme artwork and the *Dev by Sufyan* wordmark in GTA-VI style recoloured in the theme's own colours, and **Customize Home** — pick which wallpaper your Home shows, per theme. Faces are never covered by tiles or effects.
- **Signature special effects** — every main character gets a glowing signature streak that lives in its place and keeps shimmering: Zenitsu's lightning line along the top, Obanai's serpent over the Rooms box, Tanjiro's water and Rengoku's flames, Goku's energy lines and Kamehameha, Deku's Full-Cowling wisps, Bakugo's bursts, Todoroki's frost and fire — and more. One toggle in the Themes menu turns them off.
- **Smooth animations + nicer fonts** — crossfades between Home and the editor, artwork fades on theme switches, glowing Home tiles, sliding panels — all on by default (Themes → Animations is the off switch). Bundled fonts: **Inter** for the interface and **JetBrains Mono** for code; if you picked your own font before, it is kept.
- **Code photo** — save your code as a clean PNG picture (your theme and font, 1x/2x) for sharing.
- **Flowchart** — turn simple C++ code (sequence, input/output, if/else, loops, return) into a flowchart picture and save it as PNG.
- Everything from the Red Panda editor underneath: fast C/C++/Assembly editing, code completion, folding, debugging with watches, dark themes and high-DPI support.

## Free and offline

Dev by Sufyan is **free**, and its smart features work **offline** — no accounts, no AI services, no subscriptions.

## Version history

- **v1.6.0** — updater rebuilt (resumable downloads with a stall watchdog, verified size, always-visible installer path + Open Installer), three full themes (Demon Slayer / Dragon Ball / My Hero Academia) with wallpapers behind the app + Customize Home, GTA-style theme loading screen with a theme-coloured GTA-VI wordmark, full-screen Home, signature effects for the whole main cast of each anime, full-quality animations, bundled Inter + JetBrains Mono fonts

- **v1.5.1** — Fix Lagao one-click error fixes + Fix & Recheck recompile verdict, error dictionary doubled to 210 entries with a searchable Dictionary window, Program Templates on the Home page, compact not-in-a-room Rooms panel, ping indicator in the web app

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
- Bundled fonts: **Inter** and **JetBrains Mono**, both under the SIL Open Font License.
- Rooms run on a small live server shared by the PC, web and Android versions.

## License

GNU GPL v3 — see [LICENSE](LICENSE) in this repo. The original authors and the GPL are preserved, as the license requires.

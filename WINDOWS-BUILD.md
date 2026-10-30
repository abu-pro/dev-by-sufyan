# Building "Dev by Sufyan" on Windows

This is the Red Panda C++ IDE (GPL, by Roy Qu and contributors) with the
**Rooms** feature integrated and the app rebranded as **Dev by Sufyan**.
Building it produces **`DevBySufyan.exe`**.

The source zip (`DevBySufyan-RedPanda-source.zip`) is the full modified
source tree. Unzip it somewhere without spaces in the path, e.g.
`C:\DevBySufyan-src`.

You need **Qt 6.8 or newer** for a Windows build (the project requires
it), plus a C++ compiler kit. The easiest route needs **nothing
installed on your PC at all**: GitHub Actions builds a ready-made
installer for you (**Route 0**). If you prefer building locally, use
**Qt Creator + the MSVC kit** (Route A). Route B is the upstream
packaging route that produces a distributable package.

---

## Route 0 — GitHub Actions (easiest: nothing to install on your PC)

The source contains a ready-made workflow
(`.github/workflows/devbysufyan-windows.yml`) that builds
`DevBySufyan.exe` on a Windows cloud machine, packages it with its Qt
libraries, and wraps it in an installer, **DevBySufyan-Setup.exe**.

1. **Create a GitHub repo.** Sign in at https://github.com → *New
   repository* → give it any name (e.g. `dev-by-sufyan`) → *Create*.
   (A public repo gets free Actions minutes.)
2. **Push/upload this source.** The repo must contain the *contents* of
   the `DevBySufyan-source` folder at its top level — i.e.
   `CMakeLists.txt` and `.github` sit in the repo root, not inside a
   subfolder. Either use the repo's *uploading an existing file* link
   and drag everything in, or use Git:
   `git init && git add . && git commit -m "Dev by Sufyan" && git remote add origin <repo-url> && git push -u origin main`
3. **Let it build.** Pushing to `main` starts the workflow
   automatically; you can also start it by hand: repo → **Actions** tab
   → *Dev by Sufyan — Windows build* → *Run workflow*. Wait for the run
   to go green (the first run downloads Qt, so it takes a while).
4. **Download the installer.** Open the finished run → **Artifacts** →
   download **DevBySufyan-Setup** → unzip it → run
   **DevBySufyan-Setup.exe** and follow the wizard. The same run also
   offers a **DevBySufyan-Portable** artifact (the app folder, no
   installation needed).

> Note: the workflow is prepared but has not had its first real run
> yet — if a step fails, the run's log shows exactly which one, and the
> fix is usually a small edit to the workflow file.

---

## Route A — Qt Creator + MSVC (recommended local build)

### 1. Install the tools

1. **Visual Studio 2022 Build Tools** (free):
   https://visualstudio.microsoft.com/downloads/ → "Build Tools for
   Visual Studio 2022". In the installer tick **Desktop development
   with C++** (this gives the MSVC compiler + Windows SDK).
2. **Qt Online Installer** (free, needs a free Qt account):
   https://www.qt.io/download-qt-installer
   During installation select:
   - **Qt → Qt 6.8.x** (any 6.8+ patch version) → **MSVC 2022 64-bit**
   - Under the same Qt version, the add-on modules **Qt SVG** and
     **Qt WebSockets** (Rooms needs WebSockets — do not skip it)
   - **Developer and Designer Tools** → Qt Creator, CMake, Ninja
     (usually pre-selected)

### 2. Open and build

1. Start **Qt Creator** → *File → Open File or Project…* → select the
   **`CMakeLists.txt` in the top folder** of the unzipped source
   (not the one inside `RedPandaIDE/`).
2. When asked for a kit, choose **Desktop Qt 6.8.x MSVC2022 64-bit**,
   build configuration **Release**.
3. Click **Configure Project**, then **Build** (hammer icon, or
   Ctrl+B). First build takes roughly 10–20 minutes.
4. The executable lands at:
   `build\Desktop_Qt_6_8_x_MSVC2022_64bit-Release\RedPandaIDE\DevBySufyan.exe`
   (the exact build-folder name depends on your Qt version).

### 3. Run it / make it portable

- To run from Qt Creator: press the green **Run** button.
- To copy it elsewhere, the Qt DLLs must travel with it. Open a
  command prompt in the folder containing `DevBySufyan.exe` and run:

  ```bat
  C:\Qt\6.8.x\msvc2022_64\bin\windeployqt.exe DevBySufyan.exe
  ```

  (adjust the Qt path to your install). After that the whole folder can
  be copied anywhere.

### 4. Compiler for your own C/C++ programs

A self-built IDE does **not** bundle a MinGW compiler (the official
Red Panda downloads do). The IDE needs one the first time you compile
a program:

- The Qt installer can add **MinGW** (under *Developer and Designer
  Tools → MinGW 64-bit*) — its `g++.exe` works fine, or install MSYS2
  MinGW (`pacman -S mingw-w64-ucrt-x86_64-gcc`).
- In Dev by Sufyan: *Tools → Preferences → Compiler Sets* → let it
  auto-detect, or add the folder containing `g++.exe` manually.

---

## Route B — Upstream packaging scripts (distributable package)

These are the project's own scripts (see `BUILD.md` in the source):

- **MSVC portable package** — PowerShell (Core), from the source root:

  ```powershell
  ./packages/msvc/build.ps1 -QtDir C:/Qt/6.8.3/msvc2022_64
  ```

  Requires Visual Studio 2022 + Qt 6.8+ as in Route A. Output lands in
  `dist/`.
- **MSYS2 package** — install MSYS2, then inside MSYS2 follow
  `packages/msys/` (PKGBUILD-based, `./packages/msys/buildpkg.sh`).

> Upstream warning: don't build a *portable* app with MinGW-w64 shared
> Qt — the app's own DLLs can shadow the toolchain DLLs of programs you
> compile. Route A (MSVC) avoids this.

---

## Notes

- **Rooms** works out of the box: the app connects to the live room
  server (`wss://dev-by-sufyan.vercel.app/ws`) — no server setup, no
  account. Open the **Rooms** panel (left dock, or the *Rooms* menu),
  enter your name, and create or join a room with the 4-digit code.
- License: GNU GPL v3 — see `LICENSE` in the source. The About dialog
  (Help → About) shows "Dev by Sufyan — Made by Abu Sufyan, based on
  Red Panda C++, by Roy Qu and contributors".
- The Linux build of this exact source was verified in the project
  sandbox (Qt 5.15, `FORCE_QT5` path): it compiles and the resulting
  binary starts. The Rooms protocol was additionally verified
  end-to-end against the live server with a headless harness
  (create/join/edit/deny/settings/kick — all passed).

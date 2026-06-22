# ChromaOnion

An onion-skin preview **effect** for Adobe After Effects (macOS).

Like the built-in **Echo** effect, ChromaOnion overlays surrounding frames — but
it is built for *checking animation*, not for trails:

- **Independent before/after ranges** — choose how many frames to overlay on each
  side of the current time.
- **Tint** — a single slider that continuously cross-fades between two looks:
  - **0 (Opacity)** — the current frame is the opaque base, ghost frames overlaid
    at reduced opacity (fades by distance).
  - **100 (Chroma)** — frames are tinted along a **red (past) → green (now) → blue
    (future)** sweep and combined **additively**, with per-channel masks normalized
    so that where all frames agree the original color is reconstructed. Motion shows
    up as colored fringes (e.g. 1 before + current + 1 after = exact R/G/B split).
- **Edge Detect** — overlay high-contrast edge detection of the surrounding frames.
  The current frame is left untouched and the neighbor edges are drawn on top.
- Works on any layer as a SmartFX effect (8-, 16- and 32-bpc float).

Status: **v0.1** — works on macOS (Apple Silicon / Intel, universal binary).

## Build

Requires:

- Xcode command line tools (`clang++`, `Rez`, `ResMerger`)
- The **Adobe After Effects SDK** (not bundled — it is Adobe-licensed and cannot be
  redistributed). Download from the
  [Adobe Developer Console](https://developer.adobe.com/console/) and place it at
  `./AE_SDK` (so headers live at `AE_SDK/Examples/Headers`). Override the location
  with `make SDK=/path/to/sdk`.

```sh
make            # build build/ChromaOnion.plugin (universal)
make install    # copy it into the AE plug-ins folder
make clean
```

`make install` defaults to `/Applications/Adobe After Effects 2026/Plug-ins`.
Override for another version:

```sh
make install INSTALL_DIR="/Applications/Adobe After Effects 2025/Plug-ins"
```

Restart After Effects; the effect appears under **Effect ▸ Utility ▸ ChromaOnion**.

## Parameters

| Parameter | Meaning |
|---|---|
| Frames Before / After | How many frames to overlay on each side (0–30, default 1). |
| Tint | 0 = Opacity look, 100 = Chroma (additive rainbow); cross-fades between them. Default 100. |
| Onion Opacity | Opacity of the ghost frames (affects the Opacity end; cancels out at full Chroma). Default 100. |
| Fade By Distance | Farther frames fade out (on by default). |
| Edge Detect | Overlay high-contrast edges of the surrounding frames; current frame left as-is. |

## How it works

ChromaOnion is a SmartFX effect. In `SMART_PRE_RENDER` it checks out the input
layer at `current_time ± n · frame` (one checkout per frame, unioning the result
rects); in `SMART_RENDER` it composites each frame — "source over" in Opacity mode,
or an additive channel-masked blend in Chroma mode — at 8/16/32-bpc, with optional
edge detection. Being SmartFX is what enables 32-bpc float support.
See `src/ChromaOnion.cpp`.

## Layout

```
src/                source + PiPL resource (.r)
mac/                Info.plist
Makefile            universal build / install
AE_SDK/             Adobe SDK — gitignored, supply your own
build/              build output — gitignored
```

## License

MIT — see [LICENSE](LICENSE). The Adobe After Effects SDK is **not** included and is
subject to Adobe's own license.

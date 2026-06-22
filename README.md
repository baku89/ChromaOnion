# ChromaOnion

An onion-skin preview **effect** for Adobe After Effects (macOS).

Like the built-in **Echo** effect, ChromaOnion overlays surrounding frames — but
it is built for *checking animation*, not for trails:

- **Independent before/after ranges** — choose how many frames to overlay on each
  side of the current time.
- **Color modes**
  - **Opacity** — plain reduced-opacity ghosting, with optional fade by distance.
  - **Chroma** — frames are tinted along a **red (past) → green (now) → blue
    (future)** sweep and combined **additively**, with per-channel masks normalized
    so that where all frames agree the original color is reconstructed. Motion shows
    up as colored fringes (e.g. 1 before + current + 1 after = exact R/G/B split).
- **Edge Detect** — overlay edge detection so silhouettes/lines stay readable.
  Combinable with either color mode.
- Works on any layer as a normal effect (8- and 16-bpc).

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
| Frames Before / After | How many frames to overlay on each side (0–30). |
| Color Mode | **Opacity** (current frame as base, ghosts overlaid) or **Chroma** (additive red→green→blue). |
| Onion Opacity | Opacity of the ghost frames (Opacity mode; cancels out in Chroma). |
| Fade By Distance | Farther frames fade out (on by default). |
| Edge Detect | Overlay edge detection instead of solid frames. |

## How it works

ChromaOnion is a classic (non-Smart) effect that sets `PF_OutFlag_WIDE_TIME_INPUT`
and checks out its input layer at `current_time ± n · frame` via `PF_CHECKOUT_PARAM`,
then composites each frame — "source over" in Opacity mode, or an additive
channel-masked blend in Chroma mode — with optional edge detection.
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

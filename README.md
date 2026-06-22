# ChromaOnion

An onion-skin preview **effect** for Adobe After Effects (macOS).

Like the built-in **Echo** effect, ChromaOnion overlays surrounding frames — but
it is built for *checking animation*, not for trails:

- **Independent before/after ranges** — choose how many frames to overlay on each
  side of the current time, plus a frame step.
- **Color modes**
  - **Opacity** — plain reduced-opacity ghosting, with optional fade by distance.
  - **Chroma (Rainbow)** — past frames are tinted toward **red**, future frames
    toward **blue** (a rainbow sweep through the range), so motion direction reads
    as color.
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
| Frame Step | Spacing between sampled frames (1 = every frame). |
| Color Mode | Opacity, or Chroma (rainbow past→red / future→blue). |
| Onion Opacity | Overall opacity of the ghost frames. |
| Fade By Distance | Farther frames fade out. |
| Tint Amount | Strength of the chroma tint. |
| Edge Detect | Overlay edge detection instead of solid frames. |
| Show Current Frame | Draw the real current frame on top at full opacity. |

## How it works

ChromaOnion is a classic (non-Smart) effect that sets `PF_OutFlag_WIDE_TIME_INPUT`
and checks out its input layer at `current_time ± n · frame` via `PF_CHECKOUT_PARAM`,
then composites each frame ("source over") with per-frame weight, tint, and optional
edge detection. See `src/ChromaOnion.cpp`.

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

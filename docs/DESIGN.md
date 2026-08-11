# EdgeSnap design

Windows/macOS-style edge snapping for AmigaOS 4.x and MorphOS, in C89,
with no system patches. Brainstormed 2026-08-10.

## Deliverable shape

Third parties cannot extend Intuition, but the standard extension pattern
is a shared library in `LIBS:` plus a commodity:

- **`edgesnap.library`** - the engine (input handler, engine task, snap
  registry) and the public API for third parties (programmatic snap,
  per-window opt-out, state queries).
- **Commodity controller** - Exchange presence, hotkeys, prefs loading; a
  thin executable that opens the library and starts the engine.
- **Prefs** - `ENVARC:` file + tooltypes first; native GUIs later
  (ReAction on OS4, MUI on MorphOS).

## Architectural rule #1: input handler != logic

The only clean, portable way to see events before Intuition is a
commodities `CxCustom` object: the commodities input handler chain runs at
priority 56, above Intuition's 50, on both OSes. But that code runs in the
input.device task: calling Intuition from there (windows, ChangeWindowBox,
even LockIBase) is the classic deadlock recipe. So:

```
input.device -> CxCustom handler -> counters + Signal() -> engine task
                (copy event facts only)                    (all the logic)
```

## Drag detection (no patches)

`IDCMP_CHANGEWINDOW` only reaches the owning app, and `SetFunction()` on
Intuition is out (fragile, unsupported on OS4, incompatible with
interfaces). Heuristic instead:

1. On LMB-down note the candidate (active window after the click).
2. While LMB is held, compare mouse deltas with the window's
   LeftEdge/TopEdge under `LockIBase()`: if the window follows the
   pointer, Intuition is dragging it.
3. Zones trigger on the **pointer position** (like Windows), not on window
   edges - also compatible with OS4.1 offscreen dragging.
4. On release inside a zone: snap. A configurable qualifier (e.g. Alt)
   bypasses.

## Snap engine

- Geometry: side edges = halves, corners = quarters, top = maximize (all
  configurable). Usable area = screen minus `BarHeight` minus per-side
  configurable margins (for AmiDock / Ambient panels; there is no work
  area API on Amiga systems).
- One `ChangeWindowBox()` (atomic move+resize, V36+, async). Honor
  Min/Max window limits; clamped rects stay anchored to the zone's outer
  edge. Non-resizable windows: move only, or skip (configurable).
- Snap registry: pre-snap geometry per window, restored when the window is
  dragged out of its zone (Windows behavior). Window pointers are NEVER
  trusted: re-validate by walking the screen/window lists under
  `LockIBase()` before every touch; under the lock only read, release it
  before calling Intuition. If the app moves/resizes the window itself,
  its snap state decays.

## Zone preview (Aero-style)

Borderless translucent window on the target geometry while dragging in a
zone, moved with ChangeWindowBox, placed just under the dragged window
with `MoveWindowInFrontOf()`. OS4: `WA_Opaqueness` (needs compositing).
MorphOS: verify the equivalent tag in the SDK. Mandatory fallback for
non-composited screens: XOR rubber-band frame on the screen rastport.

## The divider ("internal cursor" after snapping)

Option A (v1): when two windows are snapped to adjacent zones, open a
6-8 px borderless **gutter window** over the seam. It is ours: it gets
IDCMP, we set a resize pointer on it (`SetWindowPointer()`), dragging it
live-resizes both windows of the pair. No event stealing. Depth handling:
stay-on-top plus auto-hide when other windows overlap the seam. Could be
rendered as a small Windows-11-style pill.

Option B: virtual gutter in the input handler that swallows LMB over the
seam. No extra window, but stealing clicks is fragile and changing other
apps' pointers is muddy ground.

The gutter also normalizes border-resize behavior, which differs between
OSes and apps (classic Intuition resizes only via the size gadget).

## Public API sketch (C89)

```c
LONG  ESnap_SnapWindow(struct Window *win, ULONG zone);
LONG  ESnap_Unsnap(struct Window *win);
ULONG ESnap_QueryWindow(struct Window *win);
LONG  ESnap_ExcludeWindow(struct Window *win, BOOL on);
LONG  ESnap_SetOptions(struct TagItem *tags);
LONG  ESnap_Enable(BOOL on);
```

Tag-based `_TagList` cores with per-platform varargs glue.

## Packaging

Shared C89 core (host-testable, zero Amiga includes) + per-OS library
skeleton: OS4 uses the interface system (`struct Interface`), MorphOS the
classic LVO jump table with ABI gates. Both SDKs ship library examples.

```
core/       pure C89: zones, state machine, registry (host-tested)
amiga/      engine task, CxCustom, preview, gutter (shared by both OSes)
os4/        library skeleton + varargs glue + ReAction prefs
morphos/    library skeleton + gates + MUI prefs
commodity/  Exchange controller
```

## Risks / edge cases

- Prior art: check Aminet (`util/cdity`), OS4Depot, MorphOS-Files before
  building; check recent MorphOS release notes for built-in overlap, and
  screen-edge actions (screen switching) that may conflict with edge
  zones.
- Multiple / draggable screens: operate on the screen containing the
  pointer; public screens only by default (never games).
- Virtual screens with autoscroll: edge zones get weird - out of scope for
  v1, documented.
- Apps hostile to external resizing (fixed size increments, terminals):
  imperfect result accepted; the API opt-out is the escape valve.
- Race with Intuition's own drop handling on release (spike question): if
  real, commit the snap deferred via timer.device.

## Spike findings

- 2026-08-11, OS4 QEMU (pegasos2/sm501): **phase 0 validated** - the drag
  heuristic fires reliably and the snapped geometry sticks after release
  (no race with Intuition's drop handling observed).
- Zone preview implemented as an outline frame (four thin borderless
  non-activating windows, DrawInfo FILLPEN): no compositing available on
  the QEMU sm501, and the translucent-window tags differ between the two
  OSes anyway. Translucent fill stays a compositing-only upgrade for real
  hardware.

## Roadmap

- **Phase 0 - spike** (this repo, `spike/`): validate drag detection and
  foreign ChangeWindowBox on both OSes. Feasibility gate.
- **Phase 1 - hotkey snapping**: registry + restore, configurable keys.
  Immediate value, zero heuristics.
- **Phase 2 - drag-to-edge** with preview (opacity + XOR fallback).
- **Phase 3 - gutter/divider** resize.
- **Phase 4 - public library** with stable API, native prefs GUIs,
  locale catalogs, release on OS4Depot / MorphOS-Files / Aminet.

## Open decisions

1. Library name (`edgesnap.library` vs `snapzone.library`).
2. Ship Phase 1 (hotkeys only) as a first public release?
3. Divider resizes the snapped pair (recommended) or single window only?
4. License and hosting (MIT on GitHub is the de facto scene standard).

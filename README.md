# EdgeSnap

Windows/macOS-style window snapping for AmigaOS 4.x and MorphOS: drag a
window against a screen edge or corner to tile it to a half or quarter of
the screen, with a divider to resize the tiled pair afterwards.

Written in C89. No system patches: a commodity input handler plus public
Intuition calls only. See [docs/DESIGN.md](docs/DESIGN.md) for the full
design and roadmap.

## Status: Phase 0 (feasibility spike)

`spike/edgesnap_spike.c` is a single-file commodity that must validate the
two load-bearing assumptions on real systems before any library work:

1. drag detection by correlating pointer movement with the active window's
   position under `LockIBase()` (no IDCMP access to foreign windows, no
   `SetFunction()` patches);
2. `ChangeWindowBox()` on a foreign window right after drag release.

What the spike does today:

- drag a window's title bar so the **pointer** touches a screen edge or
  corner: an outline frame previews where the window will land (four thin
  borderless windows - no compositing needed); release: the window snaps
  to half / quarter / usable-maximum;
- hotkeys (no drag heuristics involved):
  `ctrl alt cursor left/right/up` = snap left / right / maximize,
  `ctrl alt cursor down` = restore pre-snap geometry;
- prints every decision to stdout: run it from a Shell, watch it think.

## Reading guide

Suggested order for studying the code:

1. [docs/DESIGN.md](docs/DESIGN.md) - the whole architecture, why a
   commodity + library, the input-handler rule, the roadmap, and the
   spike findings so far.
2. [core/zones.h](core/zones.h) / [core/zones.c](core/zones.c) - the pure
   C89 zone geometry, with [core/zones_test.c](core/zones_test.c) as its
   executable specification.
3. [spike/edgesnap_spike.c](spike/edgesnap_spike.c) top to bottom - the
   sections mirror the architecture: library bases and the OS4/MorphOS
   type differences; the shared handler/task state and the CxCustom
   action (with the MorphOS 68k-ABI gate); window snapshotting under
   LockIBase; the restore table; snapping; the outline preview; the drag
   state machine; the commodity plumbing in main().

Key invariants to keep in mind while reading: the CxCustom action runs in
input.device context and only bumps counters + Signal()s; every Intuition
call lives in the main task; under LockIBase we only read, and a stored
struct Window * is never used without re-validation via the screen lists.

## Layout

- `core/` - pure C89, zero Amiga dependencies, unit-tested on the host
  (zone geometry today; state machine and snap registry will follow)
- `spike/` - the Phase 0 commodity (OS4 + MorphOS from one source)
- `Makefile.host` / `Makefile.os4` / `Makefile.morphos` - one lane each
- `scripts/` - container build wrappers

## Build

Host tests (any C compiler):

```sh
make -f Makefile.host test
```

AmigaOS 4 and MorphOS (need `colima start`; toolchains live in
`../os4-cross` and `../morphos-cross`):

```sh
scripts/build-os4.sh
scripts/build-morphos.sh
```

Outputs: `build/os4/EdgeSnapSpike`, `build/morphos/EdgeSnapSpike`.

## Testing on the target systems

QEMU OS4 VM (the telegram-amiga one on `/Volumes/EXT`): build an ISO and
hot-swap it into the running VM's CD drive, no network needed:

```sh
scripts/make-test-iso.sh     # unique 16-char volume label each time
scripts/os4-cd.sh insert     # newest ISO into the running VM
scripts/os4-cd.sh eject
```

Then in an OS4 Shell (the ISO carries no Amiga protection bits, so the
`Protect +e` is required):

```text
Copy ES_#?:EdgeSnapSpike RAM:
Protect RAM:EdgeSnapSpike +e
RAM:EdgeSnapSpike
```

Real hardware: http.server + wget as usual, same `Protect +e` after.

Try: drag windows to edges/corners, the hotkeys, Exchange (disable /
enable / remove), Ctrl-C to quit. Things to observe and report back into
the design doc:

- does drag detection fire reliably? false positives with apps that move
  their own windows?
- after an edge-drop, does the snapped geometry stick, or does Intuition's
  own drop handling overwrite it? (If it loses the race, the library phase
  needs a deferred commit via timer.device.)
- behavior with non-resizable windows, MUI windows, shells with size
  increments.

## Platform notes

- The CxCustom input handler runs in the input.device context: it only
  bumps counters and `Signal()`s the main task. All Intuition calls happen
  in the main task. Under `LockIBase()` we only read; the lock is dropped
  before any Intuition call.
- MorphOS: commodities custom actions are called through the 68k ABI, so
  the handler is wrapped in an `EmulLibEntry` gate; `-noixemul` is
  mandatory; the PPC stack is sized via `__stack` (the shell `Stack`
  command only sizes the 68k stack).
- AmigaOS 4: built with `__USE_INLINE__` and explicit `GetInterface()` for
  intuition and commodities.

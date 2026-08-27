# EdgeSnap design

**Author:** Michele Dipace <michele.dipace@kaffeine.net>
**License:** MIT

Windows/macOS-style edge snapping for AmigaOS 4.x and MorphOS, in C89,
with no system patches. The design target is an upstream-quality shared
library that can be proposed for adoption by both operating systems. This is
a project goal, not a claim that either system has already adopted it.

## Architectural mission

`edgesnap.library` is the product boundary. The commodity and any future
preferences GUI are clients of the library and reference frontends. They must
not contain a second, private snapping engine.

The library must make the useful behavior available to other software while
remaining safe to run as a normal third-party component:

- no `SetFunction()` patches or replacement of system vectors;
- no assumptions that an application cooperates through IDCMP;
- no Intuition calls from input.device context;
- no undocumented dependence on one OS's private window-manager behavior;
- no public API commitment until ownership, errors, versioning, and ABI are
  written down and tested on both targets.

The word **adoptable** means "small, documented, native, conservative, and
reviewable by OS maintainers". It does not mean that EdgeSnap may modify or
silently replace Workbench, Ambient, Intuition, or the system window manager.

## Deliverable shape

Third parties cannot extend Intuition, but the standard extension pattern
is a shared library in `LIBS:` plus a commodity:

- **`edgesnap.library`** - the engine (input handler, engine task, snap
  registry) and the public API for third parties (programmatic snap,
  per-window opt-out, state queries). Its public surface must remain small and
  versioned; platform-specific implementation details stay private.
- **Commodity controller** - Exchange presence, hotkeys, prefs loading; a
  thin executable that opens the library and starts the engine.
- **Prefs** - `ENV(ARC):EdgeSnap.prefs` + Shell arguments first, native
  GUIs later (ReAction on OS4, MUI on MorphOS). One KEY=VALUE parser in
  the portable core (`core/config.c`) serves every source, because on
  Amiga they share a shape: env-file lines, tooltypes and Shell
  arguments. Vocabulary: ZONES, EDGEPX, CORNERDIV, DRAGMINPX, PREVIEW,
  PANELDETECT, PANELMARGIN, MARGIN{LEFT,TOP,RIGHT,BOTTOM}, BYPASSQUAL.
  Rules that are contract, not convenience: an unknown key
  (ES_ERR_UNSUPPORTED) is told apart from a bad value (ES_ERR_BAD_ARGS);
  a rejected line leaves the setting untouched and never aborts the
  load, so a truncated prefs file cannot leave the user without
  snapping; a typo inside a list value (ZONES=left,rihgt) fails the
  whole value instead of silently disabling half the zones.

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
  configurable). Usable area = screen minus `BarHeight` minus dock/panel
  strips minus per-side configurable margins (there is no work-area API
  on Amiga systems).
- Dock awareness (macOS-style, added 2026-08-26, field-tuned on real
  MorphOS the same day): edge strips reserved by panel-like windows are
  auto-subtracted from the usable area. Detection is split per the
  architecture: the glue filters (no drag bar/size gadget/backdrop, not
  ours - borderless deliberately NOT required), the host-tested core
  policy classifies. A panel is a thin (<= dimension/4), long-enough
  (>= 15% of the edge) strip living in the OUTER BAND of the screen
  (gap from its nearest edge <= dimension/4): docks float and users
  raise them, so anywhere in the outer band counts, and the reservation
  runs from the screen edge to the panel's far side, gap included,
  plus ES_PANEL_MARGIN_PX (8) of breathing room so snapped windows
  never sit glued to the dock. Deepest panel per edge wins.
  **Validated**: floating and raised bottom-center dock on real MorphOS.
  **Open validation tasks**: real AmiDock shapes on OS4; auto-hiding
  docks (the area should breathe with them). Diagnostic: the spike's
  ctrl alt d window dump prints every window with its policy verdict.
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

This is a direction sketch, not a frozen ABI. Before the first public library
release, document for every entry point:

- library/API version and capability discovery;
- caller ownership and lifetime of windows, tags, options, and returned data;
- error values and the distinction between unsupported, rejected, and stale
  windows;
- reentrancy and task/context restrictions;
- behavior when an application changes geometry independently;
- binary compatibility rules for future structure and tag extensions.

Prefer `_TagList` cores with per-platform varargs glue. Do not expose internal
engine structs or make callers depend on private OS implementation details.

The current draft of this contract lives in `include/edgesnap.h`
(platform-facing, struct Window * signatures) and
`include/edgesnap_types.h` (portable constants: zones, errors,
capabilities - the single source of truth shared with the host-tested
core).

## Packaging

Shared C89 core (host-testable, zero Amiga includes) + per-OS library
skeleton: OS4 uses the interface system (`struct Interface`), MorphOS the
classic LVO jump table with ABI gates. Both SDKs ship library examples. The
same semantic API must be exposed on both systems even where the ABI glue is
different.

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
- 2026-08-26, real MorphOS hardware: **phase 0 validated on the second
  target** - drag detection, snapping AND the outline preview all work.
  The MorphOS-specific EmulLibEntry gate for the CxCustom action is
  thereby proven correct.
- Zone preview implemented as an outline frame. MorphOS: four thin
  borderless non-activating windows (validated on real hardware). OS4:
  the window approach failed in the field and the frame is drawn as XOR
  (COMPLEMENT) rectangles straight on the screen's RastPort - the same
  technique Intuition uses for its own drag feedback - with the public
  screen kept locked between draw and erase. Validated closed-loop on
  OS4 QEMU (monitor-driven drags + screendumps). Translucent fill stays
  a compositing-only upgrade for real hardware.
- **Two OS4 field lessons (2026-08-26, closed-loop debugging session)**
  that are binding for the library phase:
  1. *The engine must never block on I/O.* The OS4 console handler
     freezes writers while their window is being dragged: a printf to
     the shell the spike was launched from blocked the engine task
     mid-drag (stale zones, no preview, and the original FILLPEN
     "invisible frame" report was largely this). Engine-path logging now
     goes through an in-memory buffer flushed only when no drag is in
     flight.
  2. *Opening windows during an OS4 ghost-drag is unreliable* -
     OpenWindow can stall until release. Hence the XOR frame on OS4; a
     capability probe can pick the nicer window/alpha frame where safe.
  Debugging note: a half-dead previous instance left commodities in a
  state where a fresh CxBroker() hung pre-banner; a clean reboot cleared
  it. The banner now prints its build date/time so the running binary
  is never ambiguous again.
- **Restart hang - root cause found and fixed (2026-08-26)**. The
  commodity set `g_shared.engine_task` at startup and NEVER cleared it.
  Our CxCustom action runs in input.device context, so an event already
  in flight through it while the tree was being deleted would
  `Signal()` a task that was dying - and on OS4 the code containing the
  action is unloaded when the process exits, so a late call could jump
  into freed memory. Corrupted Exec/commodities state is exactly what
  the symptom looked like from outside: the mouse kept working, but
  every later `CxBroker()` hung, so no commodity could start again
  until a reboot.

  The fix is a **shutdown protocol**, and the library inherits it:
  1. deactivate the broker, so no new event enters our tree;
  2. disarm the action and drop the task pointer WHILE STILL ALIVE, so
     an in-flight call becomes a no-op and can never signal a dying
     task (the action reads the pointer once into a local, and bails
     out early on a volatile `armed` flag);
  3. `Delay(2)` - let an in-flight call return before our code can be
     unloaded;
  4. only then delete the objects, drain and delete the port, free the
     signal and close the libraries.

  Verified in-VM on OS4 with the fixed build: 8 plain start/stop cycles,
  then a kill **during a drag with the preview frame on screen** (fired
  by a delayed `Break` from a script, see below) - clean "shutting
  down", no leftover frame, and the next instance started immediately.
  A duplicate instance is still refused properly. Honest caveat: the
  original wedge was never reproduced on demand, so this is a defect
  fixed by inspection and stress-tested, not a defect caught in the act.

- **Field lesson - you cannot Ctrl-C during a drag.** The OS4 console
  freezes while its own window is being dragged, so a Ctrl-C typed then
  is simply lost: the program keeps running and snaps on release. To
  test (or to quit) mid-drag, the signal must come from outside the
  console: `Break <cli> C` from a script, or Exchange. Useful shape for
  automated tests:
  `Echo >RAM:k "Wait 9"` + `Echo >>RAM:k "Break <cli> C"` +
  `Run >NIL: Execute RAM:k`, then start the drag.

## The divider: status

Implemented across the three layers, and honest about what is proven:

- `core/gutter.c` finds the seam between two snapped windows and works
  out what dragging it does to both. Host-tested (7 scenarios: vertical
  and horizontal seams, clamping so neither window can be squeezed
  away, pairs that only touch by accident, a lone window).
- The library exposes it as API generation 2: `ESnap_QueryDivider` and
  `ESnap_MoveDivider`, with `ES_CAP_GUTTER` now in the capability mask.
- The commodity puts a thin window on the seam with a resize pointer,
  and reports drags to the library - it never resizes anything itself.

**Proven in-VM on OS4**: the library detects the pair and reports the
strip (`divider rc=0 present=1 956,33 8x959`), and the handle opens and
is drawn where it should be (measured: 8 columns of accent colour at
x=956..963). **Not yet confirmed**: that dragging the handle resizes
both windows. The path is implemented and the geometry is host-tested,
but the confirmation needs a hand on the mouse - the scripted pointer
kept missing an 8-pixel strip, and QEMU's monitor drops the occasional
mouse delta, so the automation could not settle it either way.

Two fixes that came out of reading the event code afterwards, both in
the build to try: the pointer position is taken from the SCREEN rather
than from window-relative message coordinates (the handle moves under
the pointer as the seam follows, so a relative reading measures against
a position that has already changed), and the handle is no longer
re-fronted on every pixel of the drag - only when the drag ends.

## Deployment: always-on without user intervention

A commodity has to run, but the user must never start it. The install
puts `edgesnap.library` in `LIBS:`, the commodity in `C:`, and a
`Run >NIL: C:EdgeSnap` line in `S:User-Startup`; from the next boot the
behaviour is simply part of the system, with Exchange as its control
panel.

`S:User-Startup` was chosen over `SYS:WBStartup` after the OS4 test
machine turned out to have **no WBStartup drawer at all** (`LIST:
"SYS:WBStartup": non e' un dispositivo del FileSystem`), so nothing
there would ever be scanned. User-Startup needs no icon and works
everywhere. The Workbench route stays supported for those who prefer
it: `assets/EdgeSnap.info` (generated by `scripts/make-icon.py`)
carries `DONOTWAIT` plus the settings as tooltypes, and the commodity
reads them through icon.library. Whether that hand-built icon is
accepted by Workbench is still unverified - the User-Startup path is
what has been proven.

A Workbench start also means no console: the commodity detects
`argc == 0`, takes its settings from the tooltypes, and goes silent -
printing into a non-existent console is at best pointless.

**Verified on AmigaOS 4 (VM, 2026-08-27)**: install, reboot, and the
first window dragged to an edge snapped, with nobody starting anything.

## One installer for every target (planned)

Today each system gets its own `Install-EdgeSnap`, an AmigaDOS script.
A release should instead ship ONE package that recognises the machine
it was double-clicked on, proposes the right build, and lets the user
override that choice - the way established Amiga packages do it.

**How they are actually made** (researched 2026-08-27):

- The de-facto standard is the **Commodore Installer**: a LISP-like
  script called `Install`, with an `Install.info` whose default tool is
  `Installer`. It is present on AmigaOS 3.x/4.x and MorphOS, and AROS
  ships **InstallerLG**, an open reimplementation of the same language.
  One script therefore serves every target.
- A real example, read off a shipped package (TankMouse's driver
  install), shows the shape and the two functions that matter to us:

  ```
  (copylib (prompt "...") (help "...") (source "TankMouse.driver")
           (confirm) (dest "C:"))
  (startup ("TankMouse")
     (command "IF EXISTS C:TankMouse.driver\n  RUN <>NIL: C:...\nENDIF\n")
     (prompt "...") (help "..."))
  ```

  `(copylib)` compares versions before overwriting - which is what a
  library install should do - and `(startup)` is the supported way to
  add a line to `S:User-Startup`: the Installer maintains its own
  marked block, so re-installing replaces it instead of appending a
  second copy. Our hand-rolled `Echo >>S:User-Startup` is the crude
  version of exactly this.
- AmigaOS 4 additionally has a modern **Python-based Installation
  Utility** (NewPage/AddPackage/RunInstaller). It is nicer, but it is
  OS4-only, so it cannot be the single installer for both systems.
- Installer V44 added `@INSTALLER-VERSION`, back/retrace navigation,
  media and `(reboot)`. Scripts should test for a MINIMUM version, the
  way one tests a library, never for equality.

**Plan for EdgeSnap's package**: one `Install` script that

1. detects the system, offers the detected choice pre-selected in an
   `(askchoice)`, and lets the user pick another - detection assists,
   it never dictates;
2. installs the matching `edgesnap.library` with `(copylib)` so an
   older copy is never silently overwritten by a newer one, or the
   reverse;
3. installs the commodity and registers the startup through
   `(startup)`, so a second install does not add a second line;
4. offers the WBStartup route as an alternative for those who prefer
   it, and writes `ENVARC:EdgeSnap.prefs` from the choices made.

**To verify before writing it** (do not guess these): the exact
predicate for each system - `(exists "MOSSYS:")` for MorphOS,
exec.library version >= 53 for AmigaOS 4, something equivalent for
AROS - and which `(database)` keys the Installer on each target
actually answers. Both VMs can answer this directly; the answers
belong in this document, not in the script's comments.

## Roadmap

- **Phase 0 - spike** (this repo, `spike/`): validate drag detection and
  foreign ChangeWindowBox on both OSes. Feasibility gate.
- **Phase 1 - library kernel**: shared state machine, snap registry, restore,
  capability/error model, and the first host-tested public-header contract.
  *Status 2026-08-26: kernel landed in `core/` (engine.c = the validated
  spike behavior as a pure state machine, registry.c = stale-safe restore),
  host-tested under `-std=c89 -pedantic -Werror`; portable constants in
  `include/edgesnap_types.h`; draft platform contract in
  `include/edgesnap.h`. The spike still runs its own inline logic and gets
  ported onto the kernel in phase 2.*
- **Phase 2 - reference commodity**: hotkey snapping and preferences through
  the library, with no duplicated snap logic.
  *Status 2026-08-26: DONE. The commodity is ported onto the kernel
  (commodity/edgesnap_cx.c): it feeds ESEngine window facts sampled
  under LockIBase and executes the emitted actions; drag and hotkeys
  share one snap path over the kernel registry. Preferences land through
  core/config.c from ENV(ARC):EdgeSnap.prefs and Shell arguments
  (arguments override the file), and the startup banner echoes what is
  actually in force. Verified in-VM on OS4: argument overrides, file
  loading, and a configurable dock margin reaching the geometry
  (insets b=88 -> b=100 with PANELMARGIN=20). Remaining for a release:
  Workbench tooltypes (needs icon.library + WBStartup handling) and the
  native prefs GUIs, both scheduled with packaging.*
- **Phase 3 - drag-to-edge**: live preview with opacity where available and
  an outline fallback everywhere else.
- **Phase 4 - native integration**: platform library skeletons, ABI checks,
  documentation, locale catalogs, and target-system compatibility testing.
  *Status 2026-08-26: the library BODY exists (`library/edgesnap_body.c`):
  one semaphore-protected implementation of the public API, including
  window validation, the dock-aware usable area, the snap registry and
  the interactive path. Frontends contribute raw input facts and draw
  the preview frame; the body reports what happened so the frontend can
  log it, because a library never prints. The commodity links it
  directly and was verified in-VM on OS4 (drag detected -> zone ->
  snap). Next: the OS4 ELF/interface skeleton, then the MorphOS LVO
  skeleton with gates, then the commodity opens the library instead of
  linking it.*
- **Phase 5a - one unified installer**: a single Commodore-Installer
  script that detects the system, lets the user choose, and installs
  the right build (see the section above). Replaces the per-system
  AmigaDOS scripts used during development.
- **Phase 5 - upstream proposal**: polished examples, migration/API notes,
  maintainer review package, and releases through the appropriate OS4Depot,
  MorphOS, and Aminet channels. Official adoption remains a maintainer
  decision, not a project assumption.

## Open decisions

1. Confirm the final library name (`edgesnap.library` is the current
   working name).
2. Define opaque/public window references versus direct `struct Window *`
   parameters before freezing the ABI.
   *Proposal (2026-08-26, pending ratification): keep `struct Window *`
   in the public signatures with validate-per-call semantics. It is the
   native idiom of every Intuition-facing API on both systems - exactly
   what "adoptable by OS maintainers" asks for - and an opaque handle
   would only add a resolve step for callers without removing the need
   to re-validate against the live window lists on every call. The two
   real hazards are handled explicitly: vanished windows return
   ES_ERR_STALE (validation under LockIBase, no dereference outside it),
   and pointer reuse is defused by the registry's restore rule (restore
   acts only if the window still sits on its snap geometry, else the
   state is dropped with ES_ERR_CHANGED - a mishap is bounded to
   "nothing happens", never "a stranger window moves"). The portable
   core is reference-agnostic (`void *` identity tokens), so flipping to
   opaque handles before the ABI freeze would touch only the platform
   facade if the review process demands it. Full contract text in
   `include/edgesnap.h`.*
3. Divider resizes the snapped pair (recommended) or single window only?
4. License and hosting (MIT on GitHub is the de facto scene standard).

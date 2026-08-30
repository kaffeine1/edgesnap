# EdgeSnap design

**Author:** Michele Dipace <michele.dipace@kaffeine.net>
**License:** MIT

Windows/macOS-style edge snapping for AmigaOS 4.x and MorphOS, in C89,
with no system patches. The design target is an upstream-quality shared
library that can be proposed for adoption by both operating systems. This is
a project goal, not a claim that either system has already adopted it.

## What came before

Window snapping is not new on the Amiga, and the closest prior art is
**GoSnap**, which solves the classic AmigaOS 3.x case and solves it
well. EdgeSnap is not an attempt to replace it or to do better in that
world: on 3.x, GoSnap is the answer. **ZapperNG** and **WinAction**
belong in the same conversation, arranging windows from hotkeys.

EdgeSnap starts from a different place - AmigaOS 4.x and MorphOS, with
their compositing, docks, ReAction and MUI - and makes a different bet,
in three ways.

**The gesture is the drag.** You throw a window at an edge and it
fills that half or quarter, with a frame showing where it will land
before you let go - the thing Windows and macOS users reach for
without thinking. The hotkeys are still there for people who prefer
them, and they take the same road through the library.

**The seam is live.** Once two windows share an edge, that edge can be
dragged, and both are resized together. A pair of tiled windows stops
being two independent placements and becomes a layout.

**The behaviour is a library, not a program.** `edgesnap.library` is
the deliverable; the commodity is a client of it, and any other
program can ask for a window to be placed the same way. A hotkey tool
cannot be reused by anything else - this can.

Where EdgeSnap should be honest: none of the above makes it better for
someone who is happy pressing a key. It is more code, more state, and
more that can go wrong in a drag than in a keystroke.

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

## The preview frame on AmigaOS 4

Two facts about that system decide how the frame is drawn, and both
were measured rather than assumed.

**While Intuition drags a window, another program's window operations
do not take effect until the button is released.** Opening a window,
moving one, bringing one to front - all of it waits. So the four-bar
frame MorphOS uses cannot work here: measured on 2026-08-29, the bars
are placed correctly (the log says where) and stay invisible for the
whole drag, even over empty desktop where nothing could cover them.
The same four bars appear instantly when the frame is put up outside a
drag, which is what makes this conclusive rather than a guess.

So on AmigaOS 4 the frame is drawn straight onto the screen's
RastPort, which bypasses layers.

**And the colour has to be chosen before the drag starts.** The first
version drew in COMPLEMENT, the way Intuition draws its own feedback:
no colour to choose, works over anything. It also inverts whatever is
underneath, so over a picture backdrop the frame came out a different
colour on every pixel - reported from the field as "the colour is not
uniform" - and it cannot be repaired when something paints over it,
because drawing it a second time erases it. That was the other half of
the same report: "the frame breaks up".

The frame now saves the pixels under its four strips, fills them with
one solid accent colour, and puts the pixels back when it goes. A
solid fill can be repeated, so the frame is refilled on every engine
step and anything that painted over it is covered again at once.

The catch, and it cost an afternoon: **ObtainBestPen fails during a
drag**. The fallback was FILLPEN, which on the OS4 theme is a grey
almost identical to the Workbench background, so the frame was drawn
and was invisible - indistinguishable from not being drawn at all, and
it sent the investigation after the wrong suspect twice. The screen and
the pen are taken once, at startup, and kept.

What this still cannot do: if the content under a strip changed while
the frame was up, the pixels put back are the old ones. In practice the
snap that follows repaints everything, and a drag that ends in no zone
never had the frame up over the moving window - verified both ways.

## The divider: status

Implemented across the three layers, and now confirmed end to end.

- `core/gutter.c` finds the seam between two snapped windows and works
  out what dragging it does to both. Host-tested (7 scenarios: vertical
  and horizontal seams, clamping so neither window can be squeezed
  away, pairs that only touch by accident, a lone window).
- The library exposes it as API generation 2: `ESnap_QueryDivider` and
  `ESnap_MoveDivider`, with `ES_CAP_GUTTER` in the capability mask.
- The commodity puts an invisible strip on the seam and reports drags
  to the library - it never resizes anything itself.

**Proven in-VM on AmigaOS 4**: two windows snapped left and right, the
seam found (`divider rc=0 present=1 956,33 8x959`), the strip grabbed,
and dragging it 240 px moved the seam to 1071 with both windows
resized - a 56/44 split from a 50/50 one. Releasing re-asks and the
strip follows. Three things had to be got right first, and each was a
real defect found by driving the VM from the host:

**A query must not destroy what it cannot verify.** `QueryDivider`
checked the remembered pair against the live windows and *forgot* the
entries when they did not match. But `ChangeWindowBox()` places a
window asynchronously, so a query made right after a snap sees the
window still at its old size - a mismatch that lasts a few
milliseconds. The pair was deleted on that transient, permanently, and
the seam then never appeared at all (`present=0`, always). Now only a
window that is *gone* is forgotten; a geometry mismatch just means "no
seam right now", re-checked next time. The commodity also waits a fifth
of a second after a snap before asking, so the common case does not
even see the mismatch.

**The strip must never draw.** A visible bar between two tiled windows
is not what macOS or Windows show, and worse, it does not clean up
after itself: painting the strip and then closing the window left a
blue line behind, because the console windows underneath do not repaint
what our layer had covered. That is exactly the "line that stays and
never goes away" reported from MorphOS. The strip is now opened with
`WA_BackFill, LAYERS_NOBACKFILL` and nothing is ever rendered into it,
so what shows through is the two window edges that were already there.
The feedback during a drag is the windows resizing live.

**The pointer is a property of the ACTIVE window.** `WA_PointerType`
(V53) with `POINTERTYPE_EASTWESTRESIZE` gives the double arrow, and
MorphOS has the same pointers under axis names
(`POINTERTYPE_HORIZONTALRESIZE`). But Intuition shows the pointer of
the window that is active, not the one under the mouse: hovering the
seam shows the ordinary arrow, and the double arrow appears the moment
the strip is grabbed. There is no way around that without stealing
focus on hover, which would send the user's typing to an invisible
8-pixel window.

A warning for anyone testing this: a bad pointer experiment poisons the
machine until it reboots. While probing whether `WA_PointerType` worked,
the pointer went invisible **system-wide** and stayed invisible across
restarts of the program - which made the next three experiments read as
failures. It was the VM, not the code. Reboot before concluding
anything about pointers.

## The package: one icon to double-click

The release is a drawer with an **Install icon**. Double-clicking it
runs `installer/Install` under the Commodore Installer, which is what
users of these systems expect: a real installation GUI, in the system's
own language, with the novice/intermediate/expert modes it provides for
free. Verified end-to-end on AmigaOS 4 (VM): double-click, "EdgeSnap is
installed", and the files land where they belong.

The script recognises the system (`(exists "MOSSYS:")` = MorphOS) and
pre-selects the matching build in an `(askchoice)` - detection assists,
the user decides. It then uses `(copylib)` for the library, `(copyfiles)`
for the commodity, and `(startup)` for the boot line, which maintains
its own `;BEGIN EdgeSnap` / `;END EdgeSnap` block so re-installing
replaces the line instead of adding a second one.

Two things learned by watching it run:

- **Rock Ridge is not optional.** Built as plain ISO9660 the names come
  out mangled to uppercase 8.3 - and `EdgeSnap.prefs`, with two dots,
  disappeared from the disc entirely. The ISO scripts now use
  `mkisofs -r -J`.
- **`(copylib)` keeps a library of EQUAL version**, so a rebuilt
  library with the same version.revision is silently not installed. The
  revision must be bumped for every release that is meant to replace an
  earlier one.
- Everything visible in the drawer needs an `.info`, or Workbench shows
  an empty window; `scripts/make-icon.py` now emits tool icons and
  project icons (a script needs a project icon whose default tool is
  the program that runs it - `Installer` here).
- **A file copied off a CD arrives read-only.** CDFS hands out its own
  protection bits - `----r-e-`, no write and no delete - and both
  `(copyfiles)` and `(copylib)` preserve them, so the installation
  worked exactly once and then refused for ever with *"Unable to delete
  a file or drawer -- it was delete protected"*. The script now clears
  the protection of what is already installed before replacing it, and
  sets `+rwed` on everything it installs.

The script uses paths relative to itself, so from a Shell it wants the
package as the current directory: `CD <volume>:` then
`Installer Install`. Double-clicking the icon does that for you.

### Why it does not reboot, and what it does instead

The obvious way to make an update take effect is to restart the machine,
and the Installer language appears to offer `(reboot)`. It does not work
here: the AmigaOS 4 Installer is **Hyperion's 53.12**, but it reports
`@installer-version` = 0x2B0001, i.e. **language level 43.1**, and
executing `(reboot)` fails with *"Interpreter: Executing non-function"*.
A version guard cannot help - the version it reports is the one without
the function. So an installer on this system cannot restart the machine
at all.

That turned out to be the better answer anyway, because a reboot was
only ever a way to shake the previous version out of memory. What
actually blocks an update is the **running commodity**, which holds the
old `edgesnap.library` open so that replacing the file changes nothing
until something expunges it. The installer therefore ends by doing that
work itself:

    (if (= #update 1) ( (run "C:EdgeSnap QUIT") (run "C:Wait >NIL: 2") ))
    (run "C:Avail >NIL: FLUSH")
    ... (askbool "Start it now?") -> (run "Run >NIL: C:EdgeSnap")

so the user neither reboots nor ever meets the words "Avail FLUSH".

`QUIT` is a verb, not a setting. It works through the commodity system's
own mechanism: the broker registers `NBU_UNIQUE | NBU_NOTIFY`, so a
second instance is refused and the first is told (`CXCMD_UNIQUE`) that
somebody tried. The tempting reading of that message - "a second launch
stops the first" - is a trap, and the test machine demonstrated it
immediately: its `S:User-Startup` had a duplicate start line left over
from an older install, the two instances cancelled each other out, and
nothing was running after boot. The notification says only *that*
somebody knocked, never *why*.

So the asking instance leaves a note first - `ENV:EdgeSnap.quit` - and
the running one stops only if it finds it (and deletes it). A plain
second launch is refused as before and the running instance carries on.
Verified on AmigaOS 4:

    C:EdgeSnap        -> CxBroker failed (2) - already running
    C:EdgeSnap QUIT   -> asked the running EdgeSnap to quit
    C:EdgeSnap QUIT   -> EdgeSnap was not running

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

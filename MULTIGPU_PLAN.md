# Multi-GPU / multi-monitor simultaneous rendering for Haiku app_server

Repo: `/boot/home/Progjets/haiku`, branch `MultiMonitor_WS` (confirmed empty of all prior
`MultiGPU`-branch work — this is a from-scratch implementation on this branch).

## PROGRESS CHECKPOINT (2026-11-27, paused for a real-hardware boot test)

**Done and build-verified** (`jam <target>` clean, from
`/boot/home/Progjets/haiku/generated.x86_64`):

- **Phase 0 — multi-GPU device enumeration**: `ScreenManager::_ScanDrivers()`
  (`src/servers/app/ScreenManager.cpp`) now enumerates every real `/dev/graphics/*`
  device via a new `AccelerantHWInterface::CollectDevicePaths()` static helper and
  opens one `AccelerantHWInterface` per device (new constructor overload taking a
  pinned `devicePath`, `fPinnedDevicePath` member), falling back to the original
  no-arg scan+vesa/framebuffer behavior only when zero real devices are found —
  single-GPU behavior is unchanged in every case. Files: `ScreenManager.cpp`,
  `AccelerantHWInterface.h/.cpp`.
- **Phase 1 — boot-primary detection, ported from the old `MultiGPU` branch** (commits
  `d76a96a43c`, `8405e465c9`, `bf8cb12e54`, `7ec46a0372`, `1d739c1d2f`, `4839bce145`,
  `922eb1d19f`): new shared headers `headers/private/graphics/common/
  boot_primary_ownership.h` + `boot_primary_hook.h` (hook `B_IS_BOOT_PRIMARY_PIRATI`,
  private/vendor range — kept as the original branch had it, per explicit
  instruction, not reinvented as a standard hook), wired through kernel ioctl +
  accelerant hook + `AccelerantHWInterface::IsBootPrimary(bool*)` for **10 of the 11
  in-scope drivers**: `ati`, `sm750`, `3dfx`, `s3`, `trident`, `kyro`, `xgi`,
  `intel_extreme`, `radeon_hd`, `intel_arc`. `vesa` deliberately skipped (no
  per-device `pci_info`, same as the original project).
- **Phase 1 — `GetPciLocation`**: `HWInterface::GetPciLocation()` (default
  `B_NOT_SUPPORTED`, `HWInterface.h`) + `AccelerantHWInterface` override
  (`AccelerantHWInterface.cpp`). Turned out to need **no new kernel ioctl at all** —
  ported from `MultiGPU` commit `545516dd39`/`9b06ab1175`: pure best-effort string
  parsing of the last 6 hex chars of the existing `GetDriverPath()` devfs path
  (`DEVICE_FORMAT "%04x_%04x_%02x%02x%02x"` convention already used by most drivers).

**Known divergence from the old `MultiGPU` branch, intentional, no action needed**:
that branch's founding commit (`545516dd39`) implemented Phase 0 by adding a
`devicePath` parameter to the existing `HWInterface::Initialize()`/
`AccelerantHWInterface::Initialize()` signature (touching the shared base class,
hence every `HWInterface` subclass: `RemoteHWInterface`, `ViewHWInterface`,
`BitmapHWInterface`...). This plan's implementation instead added a second,
dedicated pinned-device-path constructor to `AccelerantHWInterface` only — more
surgical, doesn't touch `HWInterface.h`'s `Initialize()` at all, already
build-verified. If re-reading `MultiGPU` code for later phases, remember it calls
`Initialize(devicePath)`, not a pinned constructor — don't try to reconcile the two,
just keep this branch's own shape.

- **Phase 2 — persistent BDF-keyed GPU order + EDID lifecycle**: done and
  build-verified (`jam app_server` clean). `Desktop` (`Desktop.h`/`.cpp`) gained
  `fActiveGpuScreens` + `_UpdateActiveGpuScreens()` (ported from `MultiGPU` commits
  `545516dd39` + `485d9af063` + `d76a96a43c`'s Desktop.cpp half: primary always
  EDID-exempt, secondaries gated on EDID/"unset" override, 3-tier primary
  determination — configured order → auto-detected boot primary via Phase 1's
  `IsBootPrimary()` → discovery index 0), `AllActiveGpusHaveHardwareCursor()`,
  `GpuConfigurationChanged()`, and two new public entry points for the still-unbuilt
  preflet to call later: `SetGpuOrder()`/`SetGpuEdidUnset()`. Persistence is a **new
  dedicated file** `~/config/settings/system/app_server/gpu_order` (flattened
  `BMessage`, `DesktopSettings.cpp`-style idiom) — deliberately NOT the existing flat
  `Screen_data` file, unlike what `MultiGPU` did (it converted `Screen_data` itself to
  BMessage format, a breaking change for existing settings; this plan avoids that).
  `Desktop::Init()`'s boot-time compositor-restore and `Desktop::SetCompositor2DEnabled()`'s
  live toggle both now gate on `AllActiveGpusHaveHardwareCursor()` instead of the
  single-screen `HWInterface()->HasSetCursorBitmap()` check — single-GPU behavior is
  identical (AND of one term). Bonus fixes bundled in (small, low-risk, same files):
  `Desktop::SetBrightness()` now routes to the specific screen `id` refers to instead
  of always the one `HWInterface()` happens to return; `AppServer.cpp`'s `'sedi'`
  (SetEDID) handler now calls `GpuConfigurationChanged()` on every `Desktop` after a
  successful set, so a newly-loaded EDID becomes effective without a reboot.
  **Deliberately excluded** from the ported `MultiGPU` code: the
  `_SetCurrentWorkspaceConfiguration()` override that called a `VirtualScreen::
  SetActiveScreen()`-style hack to repoint a single shared active screen after every
  workspace switch — that IS the flawed single-active-screen model this whole plan
  exists to replace (see Context above and report §6); Phase 3 replaces it properly
  instead of reintroducing it now.
  **Deferred to Phase 5** (no consumer yet, so untestable now): generalizing
  `AppServer.cpp`'s `'hsbm'`/`'h2da'`/`'gedc'`/`'sedc'`/`'hcai'` capability queries off
  their `ScreenAt(0)` hardcoding — that protocol only matters once the preflet UI
  exists to call it; `AllActiveGpusHaveHardwareCursor()`'s live enforcement (above)
  does not depend on it at all.

- **Phase 3 — per-screen live rendering (the architectural core)**: implemented and
  build-verified (`jam app_server` clean through every incremental step). This is
  the part the old project never got right and is the highest-risk, least
  hardware-tested piece of the whole plan so far — read the "Known gaps" note below
  before trusting it blindly. What's in:
  - `Screen` gained `DisplayedWorkspace()`/`SetDisplayedWorkspace()` and its own
    `BackgroundRegion()` (`Screen.h`/`.cpp`) — moved off `Desktop` entirely (deleted
    `Desktop::fBackgroundRegion`/`fScreenRegion`), so each screen now tracks its own
    "what's not covered by a window" staleness cache and clipping-seed frame instead
    of one shared Desktop-wide pair.
  - New public `Desktop::ScreenForWorkspace(int32 workspace)` — the row formula
    (row = workspace / columns, screen = row % ActiveGpuScreens().CountItems()),
    replacing the old broken `Desktop::_DetermineScreenFor(BRect)` stub (deleted
    entirely — it was geometry-based and, on reflection, geometry is meaningless for
    disambiguating between independent GPUs' disjoint coordinate spaces; the correct
    screen is always knowable directly from which workspace's window list a window
    came from, no geometry needed). `_UpdateActiveGpuScreens()` now also initializes
    each active screen's `DisplayedWorkspace()` to the first workspace of the row it
    owns.
  - `ServerWindow::MakeWindow()`: new windows bind to `ScreenForWorkspace(CurrentWorkspace())`'s
    `HWInterface()` instead of the single Desktop-wide `HWInterface()` — a window
    opens where the user's focus currently is, exactly as confirmed wanted. Never
    rebound after this except the existing, unrelated 2D-compositor backing-store
    swap — which itself got a matching fix: `Window::FreeBackingStore()` used to
    restore to the single Desktop-wide `HWInterface()`, now restores to the window's
    own `Screen()` (falling back to `ScreenForWorkspace(fCurrentWorkspace)` in the
    brief window before a fresh window's first clipping pass sets `Screen()`).
  - `Desktop::_RebuildClippingForAllWindows`, `_TriggerWindowRedrawing`,
    `_SetBackground`, `CompositeDirtyRegion`, `MarkDirty` all gained a trailing
    `int32 workspace = -1` (or `Screen* screen = NULL`) parameter — default behavior
    for every pre-existing call site is byte-identical (resolves to
    `ScreenForWorkspace(fCurrentWorkspace)`, the same single screen as before with
    one active GPU). The mechanism that actually makes an **unfocused** screen keep
    redrawing live: `Window::MarkDirty()` and every direct
    `fDesktop->CompositeDirtyRegion(...)`/`fDesktop->MarkDirty(...)` call in
    `Window.cpp`/`ServerWindow.cpp` now explicitly pass the triggering window's own
    `CurrentWorkspace()` instead of relying on the default — so a window
    self-invalidating on a screen that doesn't have focus still gets redrawn and
    composited against *its own* screen's `HWInterface()`, not whichever screen is
    globally focused.
  - `Desktop::_ScreenChanged(Screen* screen)` (mode-change handling): now resolves
    `screen->DisplayedWorkspace()` and threads it through the three calls above,
    instead of implicitly operating on `fCurrentWorkspace` regardless of which
    screen's mode actually changed.
  - `Desktop::_SetWorkspace()`: the report-§6 fix. The "windows leaving the old
    workspace" bookkeeping loop (deactivate, mark dirty, clear current-workspace
    membership) now only runs when `ScreenForWorkspace(previousIndex) ==
    ScreenForWorkspace(index)` — i.e. switching *within* one screen's own row. A
    cross-screen focus move (jumping to a different GPU's row) now leaves the
    screen being left **completely untouched** — still fully live, just no longer
    focused — instead of wrongly deactivating/hiding its windows. The target
    screen's `DisplayedWorkspace()` is updated to `index` unconditionally.

  **Known gaps / deliberately not attempted** (flagged honestly, not hidden):
  - The "show windows entering the new workspace" half of `_SetWorkspace` still uses
    `window->InWorkspace(previousIndex)` to decide "was this window already
    visible" — correct when switching within one screen, and harmless-but-wasteful
    when jumping cross-screen to a workspace that screen had already shown moments
    before (every window gets treated as "newly appearing" and its anchor position
    re-applied, even if nothing actually changed). Not a correctness bug, just a
    possible minor flicker/inefficiency on a fairly rare interaction pattern — noted
    for Phase 6 to assess on real hardware, not fixed preemptively.
  - `VirtualScreen::SetConfiguration()` itself (unchanged, per the plan's original
    decision to bypass rather than fix `VirtualScreen`) still touches every
    discovered screen's hardware unconditionally on every workspace switch,
    including ones entirely within one screen's own rows — the real-hardware-
    disruptive `GetMonitorInfo()`/mode-set calls the report flagged in its own §6.
    This is a performance/disruption concern, not (as far as reasoned through)
    a correctness one — left for a later pass.
  - **This is exactly the subsystem where the old `MultiGPU` branch got stuck on
    real hardware** (background corruption, ghosting, missing decorators — never
    root-caused) and the ATI-secondary-never-shows-a-signal mystery. None of that
    is pre-solved by this rewrite just because the architecture is now more
    correct in principle — Phase 6's real-hardware test of Phase 3 specifically
    (both screens genuinely live at once, dragging/resizing on one while the other
    keeps updating, Tracker/Deskbar/ActivityMonitor sanity-checked on both) is not
    optional polish, it's the actual verification this entire phase has been
    waiting for.

**Not started yet**: Phase 4 (compositor all-GPUs gate for the preflet checkbox UI
specifically — the live enforcement already exists since Phase 2), Phase 5 (preflet
UI — also where the `AppServer.cpp` capability-query generalization belongs), Phase 6
(hardware verification loop — now the critical next step, see above).

**Right now**: Phase 0-3 are implemented and build-verified (compiles clean) but
**Phase 3 has had zero real-hardware testing** — unlike Phases 0-2, which are modest,
mostly-additive changes, Phase 3 rewrites the core redraw/clipping/compositing path
and is exactly the area with historical precedent for subtle, hard-to-diagnose real-
hardware-only bugs. Strongly recommend a real-hardware boot test (Phase 6, Phase-3
section above) before any further work, rather than continuing to Phase 4/5 blind.

**Pre-test fix (2026-11-27)**: the boot-splash code the user had added to 9 of the
10 in-scope drivers (`ati`, `s3`, `trident`, `kyro`, `xgi`, `intel_extreme`,
`intel_arc`, `radeon_hd`, `matrox`; `sm750` is a separate, lower-risk case covered
below) drew using `FRAME_BUFFER_BOOT_INFO`'s geometry with no check that the current
device was actually the one that geometry describes. Harmless under Phase 0's
predecessor (only one accelerant ever initialized), but Phase 0 now runs every
driver's `InitDevice()`, so a non-owning secondary could write up to the *primary's*
width×height into its own, differently-sized mapped VRAM BAR -- an out-of-bounds
kernel write, the same bug class already found 3 times in the old `MultiGPU` branch
(report §1a). Fixed by gating every `draw_logo`/`draw_*_logo()` call with
`boot_framebuffer_belongs_to_this_device()` (already available from Phase 1's
`boot_primary_ownership.h`; added the include + the missing
`UsePrivateHeaders [ FDirName graphics common ]` to `matrox`'s kernel Jamfile too,
even though `matrox` has no `IS_BOOT_PRIMARY` ioctl by design -- the ownership check
itself is header-only and needs no ioctl). Also applied to `sm750`: its own
boot-info-derived "preferred mode"/`real_framebuffer_size` logic is now skipped
(falls through to the same path already used when `bi == NULL`) unless
`sm750` is actually the boot primary -- today's hardware is unaffected (SM750 *is*
primary right now) but this makes the driver correct if that ever changes. All 10
build-verified clean.

**Known architectural gap, found 2026-11-27, NOT resolved — `B_ALL_WORKSPACES`
windows (BAlert, modal dialogs via `B_MODAL_ALL_WINDOW_FEEL`/`B_FLOATING_ALL_WINDOW_FEEL`
— an everyday pattern, not a corner case) break two of Phase 3's assumptions**:

1. **Rendering depends on the compositor actually being on.** With the compositor
   enabled, a window draws into a GPU-agnostic CPU-memory bitmap
   (`fBackingBitmap`/`fBackingHWInterface`), and `CompositeDirtyRegion()` just blits
   that bitmap onto whichever screen's real framebuffer a given compositing pass
   targets — this part is fine and should work correctly across screens. **Without**
   the compositor (direct-to-framebuffer — not hypothetical: this is this fork's own
   documented state on the user's primary SM750 hardware, 2D acceleration disabled,
   and the compositor is opt-in besides), a window's content only ever actually gets
   drawn by its ONE creation-time-bound `DrawingEngine`/`HWInterface`. A
   `B_ALL_WORKSPACES` window created on screen A would correctly reserve its space in
   screen B's own clipping accounting (other windows won't draw under it) but never
   actually render real content there — it stays blank/stale on every screen except
   the one it was created on.
2. **`Window` has one shared `VisibleRegion()`/clipping state, not one per screen.**
   Every other window in this design is only ever touched by exactly one screen's
   clipping pass (it belongs to exactly one workspace, hence exactly one screen) —
   that invariant is what makes Phase 3's per-screen generalization safe. A
   `B_ALL_WORKSPACES` window breaks it: it's a member of *every* workspace's window
   list, so it participates in whichever screen's clipping pass happens to run,
   whenever that happens to be triggered (independently per screen, at different
   times). Each pass overwrites the window's one shared clipping/visible-region
   state via `SetClipping()`/`SetScreen()`. Whichever screen's pass ran *last* wins;
   any other screen's later redraw/composite decisions for that window use
   whatever stale clipping state that last pass left behind, until that other
   screen's own clipping happens to run again. This is a real, unresolved
   correctness gap, not a hypothetical one — it was reasoned through from the actual
   code (`Window::SetClipping`/`SetScreen` are plain single-value setters, no
   per-screen storage), not tested, since it needs two screens with an alert
   showing to actually exercise. **Not fixed in this plan** — doing so properly
   likely means giving `Window` per-screen clipping/visible-region storage (a real,
   separate design task), which is bigger than anything else in Phase 3 and
   deliberately out of scope for this pass. Flag for Phase 6 testing (trigger a
   `BAlert` while two screens are both live and see what actually happens on the
   non-creating screen) and for a dedicated follow-up design before relying on
   `B_ALL_WORKSPACES` windows in a multi-GPU session.

**Required follow-up, requested explicitly by the user and not yet implemented**:
since a window's `DrawingEngine` is bound to one GPU for its whole lifetime and is
deliberately never rebound (see Phase 3 above — rebinding deadlocked real hardware
in the old project), **nothing today stops a window from being reassigned to a
workspace that belongs to a *different* screen than the one it's bound to** (e.g.
via `BWindow::SetWorkspaces()`, dragging it to another workspace in the Workspaces
app, or the "send window to workspace" style actions) — doing so would produce
exactly the "reserved-but-blank/stale" failure mode described above for
`B_ALL_WORKSPACES`, just for an ordinary window moved across a GPU boundary instead
of a window that's nominally on every screen. **This must be blocked**: whatever
code path changes a window's workspace membership needs to reject (or clamp) a
reassignment that would put a normal, single-workspace window onto a different
screen than the one its engine is bound to. Not yet located/implemented — needs its
own pass: find `Desktop`'s window-workspace-reassignment entry point(s) (likely
`SetWindowWorkspaces`/`_ChangeWindowWorkspaces`-style calls, triggered by
`AS_SET_WINDOW_WORKSPACES`-class messages or the Workspaces app) and add the guard
there, using `Window::Screen()` (stable, set at creation) compared against
`ScreenForWorkspace()` of the requested target workspace.

---

## Context

The user's fork currently supports exactly one graphics card. The goal is to drive
**N independent monitors, each on its own graphics card**, with each card's monitor
showing a *contiguous row of workspaces* assigned to it — and, critically, every
assigned screen must stay **live and redrawing in real time simultaneously**: watching
a video on GPU A's monitor/workspace while actively using a browser on GPU B's
monitor/workspace, at the same time, is the entire point.

A previous attempt (branch `MultiGPU`, not present on this branch, documented in
`/boot/home/MULTIGPU_PROJECT_REPORT.md`) got GPU discovery, boot-primary detection,
BDF-keyed persistence, and the Screen preflet UI working and real-hardware-verified —
but built its rendering model around "exactly one screen is ever actively
composited/driven at a time, workspace switches just repoint which one" — the wrong
foundation, rejected by the user once tested, and never fully fixed afterward (a
real-hardware rendering-corruption bug was found but never root-caused before the
project paused). Since none of that branch's code exists on `MultiMonitor_WS`, this
plan re-derives the good ideas but designs the rendering core correctly from day one
instead of patching a single-active-screen model after the fact.

Three rounds of codebase exploration on the actual current state of `MultiMonitor_WS`
(not assumptions from the old report) found:

- **`ScreenManager::_ScanDrivers()`** (`src/servers/app/ScreenManager.cpp:192-213`) is
  hardcoded to construct exactly one `AccelerantHWInterface` and stop — multi-GPU
  support doesn't exist at any layer today, not even device enumeration.
- **`AccelerantHWInterface::Initialize()`** (`AccelerantHWInterface.cpp:356-422`)
  already scans `/dev/graphics/` **by device index** (`_OpenGraphicsDevice(i)`,
  `_RecursiveScan` counting entries) — it just always starts at index 0 and stops at
  the first one that opens successfully. This is a very exploitable existing seam.
- **`VirtualScreen`** holds a *list* of `Screen*` (`fScreenList`) but
  `AddScreen()`/`SetConfiguration()` **overwrite** a single cached
  `fHWInterface`/`fDrawingEngine` on every call (`VirtualScreen.h:31-35` has an
  explicit `// TODO: can we have a multiplexing HWInterface as well?`), and
  `Desktop::HWInterface()`/`GetDrawingEngine()` just forward to that single cache.
  This is the deep reason the old attempt's "one active screen" bug existed — it's
  baked into `VirtualScreen` itself, not something introduced by that branch.
- **`Desktop::_DetermineScreenFor()`** (`Desktop.cpp:4021-4028`) is stubbed:
  `// TODO: choose the screen depending on where most of the area is; return
  fVirtualScreen.ScreenAt(0);` — no window is ever really routed to "its" screen today.
- **`Desktop::_SetBackground`/`fBackgroundRegion`/`fScreenRegion`** assume one shared
  region and one `DrawingEngine` for the whole desktop (`Desktop.cpp:4084-4106`).
- A **fully working, production-quality software-cursor fallback already exists**
  per-`HWInterface` instance (`HWInterface::_DrawCursor`/`fCursorAreaBackup`,
  `AccelerantHWInterface::_DrawCursor` override) — it's what already runs today
  whenever an accelerant lacks `B_SET_CURSOR_BITMAP`/`B_SET_CURSOR_SHAPE` or the user
  disables hardware cursor. Nothing new needed here, just correct routing per screen.
- `B_ALL_WORKSPACES` windows need **no special casing** — they're already simply
  members of every workspace's window list (`Window::InWorkspace`), so they'll
  naturally render on whichever screen(s) currently display any workspace, once
  per-screen rendering exists.
- `Screen_data` (compositor/cursor/2D-accel settings) is a **flat positional binary
  file**, not a BMessage — the better existing idiom for new structured, keyed data
  (BDF → GPU order, BDF → EDID override) is `DesktopSettings.cpp`'s
  `BMessage::Flatten()/Unflatten()` pattern (see its `"workspaces"` file: top-level
  int fields + an array of sub-`BMessage`s), kept under
  `~/config/settings/system/app_server/`.
- The private app_server↔preflet query protocol already exists and is multi-screen-
  shaped in spirit: raw 4-char `what` codes (`'gedi'`/`'sedi'`/`'gedc'`/`'sedc'`/
  `'hsbm'`/`'h2da'`/`'hcai'`) handled synchronously in `AppServer::MessageReceived()`
  (`AppServer.cpp:253+`), sent via `BMessenger("application/x-vnd.haiku-app_server")`
  from `ScreenWindow.cpp`. `'gedi'`/`'sedi'` already take a `"screen"` index. The
  capability queries (`'hsbm'` etc.) are hardcoded to `gScreenManager->ScreenAt(0)`
  today and need generalizing to loop over every active screen.
- The EDID hook pair (`B_GET_EDID_INFO`/`B_SET_EDID_INFO`, ordinary optional members
  of the public `Accelerant.h` hook enum, *not* a vendor-private hook) is the clean,
  fully-worked template for driver→accelerant→`AccelerantHWInterface`→app_server
  wiring: ioctl in the kernel driver, `get_accelerant_hook()` case, wrapper method on
  `AccelerantHWInterface`, `B_NOT_SUPPORTED`/`false` default on the base `HWInterface`.
- Every kernel driver already carries a full `pci_info` (bus/device/function) in its
  per-device cookie struct (populated from `pci_module_info::get_nth_pci_info()` at
  `init_driver()` time) — the data a BDF-identity hook needs is already sitting there,
  just not exposed through any hook today.

**User decisions locked in for this plan:**
- The boot-primary ioctl/hook and its supporting shared header are to be **ported
  directly from the old `MultiGPU` branch** (commits `8405e465c9`,
  `d76a96a43c`/`3e0c9d5cef`, then the per-driver-family rollout commits
  `7ec46a0372`, `1d739c1d2f`, `4839bce145`, `922eb1d19f`, `36e0289fd8`, `250bc3e55a`,
  `76d7d4c1ed`, `16d21a6449`, `d4df61e81c`, plus the hardware fixes `3275d915d3`,
  `314442a33b`), **not reimplemented from scratch** — but rolled out only to the 11
  drivers that already have both EDID and cursor hooks today: `3dfx`, `ati`,
  `intel_arc`, `intel_extreme`, `kyro`, `radeon_hd`, `s3`, `sm750`, `trident`, `vesa`,
  `xgi`. The other 6 (`ast`, `et6x00`, `matrox`, `neomagic`, `nvidia`, `via`) are
  explicitly out of scope for now.
- The global 2D-compositor checkbox keeps its existing gate **exactly as designed**:
  enabled only if **every active GPU** reports hardware cursor active (logical AND
  across all active screens, generalizing today's single-screen check) — this is
  load-bearing for how this specific AGG-based compositor avoids leaving cursor-drag
  trails (no true double buffering; a software cursor would smear into the backbuffer).
  This is unrelated to, and does not pre-solve, the old project's unresolved
  rendering-corruption bug — that remains open and must be root-caused on real
  hardware during Phase 3/6, not assumed fixed.
- With exactly one GPU detected, every single-GPU code path must produce **identical,
  byte-for-byte behavior** to today — every new per-screen loop must degenerate
  trivially to today's single-iteration behavior (mirrors the existing
  `kWindowAlphaCompositingFlag` opt-in discipline already used for the compositor).
- No public API (`BScreen`, libbe) changes — everything new is a private
  app_server↔preflet `BMessenger` protocol, exactly like the existing EDID/cursor
  queries, to avoid any shared-library vtable ABI risk.

---

## Phase 0 — Enumerate all GPUs (the actual root blocker)

Make `ScreenManager` open one `AccelerantHWInterface` per physical graphics device
instead of one, period.

- `AccelerantHWInterface`: add a `deviceNumber` the instance is pinned to (constructor
  parameter, stored in a new member), and change its open loop
  (`AccelerantHWInterface.cpp:356-422`) to try *only* that device number via the
  existing `_OpenGraphicsDevice(deviceNumber)`/`_RecursiveScan` machinery, instead of
  iterating `i = 0, 1, 2, …` itself. Keep the vesa/framebuffer fallback behavior but
  only for `deviceNumber == 0` when nothing else was found (preserves today's exact
  behavior on hardware with no real driver).
- `ScreenManager::_ScanDrivers()` (`ScreenManager.cpp:192-213`): replace the
  single-shot `while (initDrivers)` with a loop over `deviceNumber = 0, 1, 2, …`,
  constructing `new AccelerantHWInterface(deviceNumber)` and calling
  `_AddHWInterface()` for each, stopping when `_RecursiveScan` reports the count is
  exhausted (no device at that index) — `_AddHWInterface` already tolerates and
  cleans up a screen whose `Initialize()` fails, so a device that can't actually
  drive a display (no monitor attached) just doesn't produce a `Screen`.
- This phase alone is testable in isolation on the user's real dual-card hardware:
  after it, `gScreenManager->CountScreens()` should report 2, each with its own
  `Screen`/`HWInterface`/`DrawingEngine` — even though nothing downstream uses the
  second one correctly yet (Desktop/VirtualScreen still only expose one). Confirms
  device-level discovery works before touching any rendering logic.

## Phase 1 — Boot-primary detection + BDF identity (ported from `MultiGPU`)

- Copy `headers/private/graphics/common/boot_primary_ownership.h` (the
  `boot_framebuffer_belongs_to_this_device()` check) verbatim from the `MultiGPU`
  branch via the commit range above.
- Add a new **standard optional accelerant hook** (follow the `B_GET_EDID_INFO`
  template exactly — a real enum member in `headers/os/add-ons/graphics/Accelerant.h`,
  not the unused `B_ACCELERANT_PRIVATE_START` vendor-private range the old branch
  used) for "am I the boot primary", wired through the 11 in-scope drivers' kernel
  `driver.cpp` (ioctl using `boot_framebuffer_belongs_to_this_device(&di.pciInfo)`,
  `pciInfo` is already in every driver's per-device cookie) → `hooks.cpp`/
  `accelerant.cpp` → `AccelerantHWInterface` wrapper method (mirror
  `GetEDID`/`SupportsGetEDID`) → `HWInterface` base default (`B_UNSUPPORTED`).
- Add a **new** `GetPciLocation(uint8* bus, uint8* device, uint8* function)` hook
  (nothing like it exists today — the only current proxy is parsing an
  accelerant-specific devfs path string) on the same 11 drivers, since BDF-keyed
  persistence (Phase 2) needs it and each driver already has the raw fields.
- `Desktop.cpp`: port `auto_detected_primary_bdf()`/the primary-decision fallback
  chain (configured order → auto-detected boot primary → discovery-order index 0)
  from report §1b/§2, adapted to query every screen via the new hooks instead of the
  old branch's exact code (which doesn't exist on this branch to copy verbatim).

## Phase 2 — Persistent GPU order + per-GPU EDID lifecycle

- New settings file (e.g. `~/config/settings/system/app_server/gpu_order`), following
  `DesktopSettings.cpp`'s `BMessage::Flatten()/Unflatten()` idiom exactly (its
  `"workspaces"` file is the direct template): top-level ordered BDF string list +
  one sub-`BMessage` per GPU (BDF key, `edid_unset` bool override, etc.) — **do not**
  extend the flat positional `Screen_data` file, which has no keying and is already
  read redundantly in three places.
- Port `Desktop::_UpdateActiveGpuScreens()`'s logic (report §2/§3: BDF-based ordering
  with discovery-order fallback, primary always treated as in-use, secondaries gated
  on EDID presence, row-assignment formula `_ScreenForWorkspace()`) — re-expressed
  against this branch's real `Screen`/`ScreenManager` API, not copied verbatim.
- Generalize the `'hsbm'`/`'h2da'`/`'gedc'`/`'sedc'`/`'hcai'` handlers in
  `AppServer.cpp` (currently all hardcoded to `gScreenManager->ScreenAt(0)`) to loop
  over every active screen — needed both for the compositor's all-GPUs-AND gate and
  for the preflet's per-GPU tabs.

## Phase 3 — Per-screen live rendering (the architectural core)

This is what the old attempt never got right — give rendering state genuine per-screen
scope instead of patching the single shared cache:

- `Screen`: add a `DisplayedWorkspace()`/`SetDisplayedWorkspace()` member — which
  workspace *this* screen is currently showing, independent of any global "current"
  concept. Initialized by the row-assignment formula, changed only when the target
  workspace is in this screen's own row.
- `Desktop::_DetermineScreenFor()`: replace the `ScreenAt(0)` stub with the real
  row-formula lookup based on the window's workspace.
- `ServerWindow::MakeWindow()` (`ServerWindow.cpp:4557-4569`): bind the new window's
  `DrawingEngine` to *its target screen's* `HWInterface` (via the row formula applied
  to the window's initial workspace) instead of unconditionally
  `fDesktop->HWInterface()`. Per the old project's hard lesson (a rebind-after-
  creation attempt deadlocked real hardware, commit `dd0975283d`/revert
  `53266497b6`): **never** rebind a window's engine to a different *real* `HWInterface`
  after construction — only the existing backing-store-bitmap swap (compositor
  on/off) is safe, and stays untouched.
- `Desktop::_SetWorkspace`/`_SetCurrentWorkspaceConfiguration`: gate the hide/redraw
  loop on whether the previous and new workspace share the same `Screen` (via
  `_ScreenForWorkspace()`) — a workspace switch on one screen must never touch
  windows/background belonging to any other screen's own displayed workspace.
- `Desktop::MarkDirty`/`_TriggerWindowRedrawing`/`_RebuildClippingForAllWindows`/
  `_SetBackground`/`CompositeDirtyRegion`: generalize from "operate once against
  `fCurrentWorkspace` and the single shared `fBackgroundRegion`/`fScreenRegion`" to
  "loop over every active `Screen`, each against its own displayed workspace's window
  list, its own `HWInterface`/`DrawingEngine`, and its own background-region cache"
  (move `fBackgroundRegion` ownership from `Desktop` to `Screen`, one instance per
  screen). With exactly one active screen, this loop runs its body exactly once,
  identically to today.

## Phase 4 — Compositor all-GPUs gate

- Generalize the `f2DCompositorCheckbox` enable logic (currently
  `hasSetCursorBitmap && fHardwareCursorCheckbox->Value()` for one screen,
  `ScreenWindow.cpp:596-605`) to require the condition true for **every** active
  screen, using the generalized capability query from Phase 2. Single-GPU behavior
  unchanged (the AND of one term is that term).
- `Desktop::SetCompositor2DEnabled()` stays a flag flip + per-window backing-store
  alloc/free (`Desktop.cpp:2275-2298`) — no change needed to its own body, since by
  the time it's called the gate has already guaranteed every active screen qualifies.

## Phase 5 — Screen preflet UI

Rebuild per report §5's structure (workable "quasi perfetta" split, confirmed by the
user previously): a "Workspaces" tab (rows/columns spinners, monitor-grid preview,
GPU-order `BListView` with Move Up/Down, the 2D-compositor checkbox) + one tab per
screen — the primary keeps `ScreenWindow`'s full existing control set unchanged, every
other screen gets a new, simpler `GPUSettingsView` ("apply immediately": resolution,
brightness, hardware cursor, 2D accel/debug, EDID dump/load/unset — reusing this
branch's *already-working* EDID dump/load code in `ScreenWindow.cpp:1507-1649` as the
direct base for that section). All per-screen facts for the UI (device name, BDF,
EDID presence, which screen is primary) come from the private BMessenger protocol,
following `'gedi'`/`'sedi'`'s existing request/reply shape — never shared in-process
state, since preflet and app_server are separate teams.

## Phase 6 — Real-hardware verification loop

Using the user's SM750 (primary) + ATI Rage XL (secondary, behind a TI PCIe→PCI
bridge) pair, in small build-verify-test increments (the discipline that caught the
old project's worst regressions cleanly):
- Phase 0 alone: confirm 2 screens discovered, no regression on single-screen boot.
- Phase 1-2: confirm correct primary auto-detection and persistent BDF-keyed order
  survives reboot and discovery-order shuffles.
- Phase 3: confirm **both monitors show live, independently-updating content at the
  same time** — the actual goal — and specifically re-attempt to root-cause the old
  project's two open mysteries before declaring this phase done: (a) why the ATI
  secondary never produced a signal in the previous attempt despite being
  software-eligible, (b) whether the ghosting/decorator-loss/wrong-workspace-
  background symptoms reproduce here too. Both are still-open risks this plan does
  not pre-solve.
- Phase 4: confirm the compositor only enables when both cards report hardware
  cursor, and disables cleanly (no cursor-drag trail regression) when either doesn't.

---

## Critical files

- `src/servers/app/ScreenManager.cpp` / `.h` — device enumeration (Phase 0)
- `src/servers/app/drawing/interface/local/AccelerantHWInterface.cpp` / `.h` —
  per-device open scope, new hooks (Phase 0, 1)
- `src/servers/app/drawing/VirtualScreen.cpp` / `.h` — stays a layout/frame
  aggregator; its single-`HWInterface` cache is bypassed by routing through `Screen`
  directly rather than finally "fixing" its TODO
- `src/servers/app/Screen.cpp` / `.h` — `DisplayedWorkspace()`, per-screen
  `fBackgroundRegion` (Phase 3)
- `src/servers/app/Desktop.cpp` / `.h` — `_DetermineScreenFor`, `_SetWorkspace`,
  `_UpdateActiveGpuScreens`, `MarkDirty`/`_SetBackground`/`CompositeDirtyRegion`,
  auto-primary detection (Phase 1-4)
- `src/servers/app/ServerWindow.cpp` — `MakeWindow` engine binding (Phase 3)
- `src/servers/app/AppServer.cpp` — private query protocol generalization (Phase 2, 4)
- `src/servers/app/DesktopSettings.cpp` — template for the new GPU-order settings file
  (Phase 2)
- `headers/private/graphics/common/` — ported `boot_primary_ownership.h` (Phase 1)
- `headers/os/add-ons/graphics/Accelerant.h` — new standard optional hooks (Phase 1)
- Per-driver `driver.cpp`/`hooks.cpp`/`accelerant.cpp` for the 11 in-scope drivers
  (Phase 1) — `ati` and `sm750` first (real test hardware), rest mechanically after
- `src/preferences/screen/ScreenWindow.cpp`, new `GPUSettingsView.cpp`/`.h`,
  `ScreenSettings.cpp` (Phase 5)

## Verification

- Build-verify each phase with the real x86_64 cross-toolchain (`jam`,
  `generated.x86_64/`) before moving to the next — never just syntax-check.
- Real-hardware boot-test on the SM750+ATI pair after every phase per the Phase 6
  breakdown above; never request a flash/reboot cycle that's diagnostics-only without
  a real fix attempt bundled in, per established practice.
- Regression-check ordinary single-GPU boot (any machine with one card) after every
  phase — this must never change behavior when `CountScreens() == 1`.
- Before Phase 3 is considered done, explicitly re-test Tracker/Deskbar/ActivityMonitor
  on both screens, not just the one feature being exercised — shared
  Desktop/Window compositing code regressed unrelated apps once before on this fork.

---

## First real-hardware test (2026-11-27)

Both cards activate — Phase 0 confirmed working (`ati: device_open()` is now called
for the secondary device at all, which never happened before). The ATI Rage XL
secondary failed `device_open()` with `B_UNSUPPORTED` ("Video chip clock type 0 not
supported"). Root cause: **exactly** the bug already found and fixed in the old
`MultiGPU` branch (commit `314442a33b`), not ported in Phase 1 because it's a
hardware-compat fix, not a multi-GPU architecture change —
`Mach64_GetBiosParameters()` always read the ISA legacy shadow at 0xC0000 for clock
parameters, which only ever contains the boot-primary's (SM750's) ROM; for any other
card it reads the wrong BIOS, wrong clock table, wrong `clockType`. Ported the fix:
prefer the card's own PCI expansion ROM BAR (`rom_base_pci`) when assigned, falling
back to the ISA shadow only if absent/invalid (0x55/0xAA signature check). Build
verified.

**Realistic expectation, not just hope**: in the old project, even *after* this
exact same fix, the ATI secondary's monitor never produced any signal at all — a
mystery never root-caused (see `MULTIGPU_PROJECT_REPORT.md`'s closing section). This
fix should get past the `device_open()` failure (the ATI becoming a real `Screen`
with a workspace row assigned), but is not guaranteed to produce an actual picture.
If it still doesn't, the user has an S3 card as a fallback (described as better-
behaved, no history of this kind of issue) to swap in instead of the ATI.

## Second real-hardware crash (2026-11-27) — found and fixed

KDL backtrace matched **exactly** a bug already found and fixed in the old
`MultiGPU` branch (commit `9e67fee81b`): `AccelerantHWInterface::Shutdown()` ←
`Screen::~Screen()` ← `ScreenManager::_AddHWInterface()` ← `_ScanDrivers()` ←
`ScreenManager::ScreenManager()`. Cause: `_OpenAccelerant()`'s three failure paths
`unload_add_on()` the accelerant image but never reset `fAccelerantHook` to NULL
(nor, in the third path, `fAccelerantImage`) — a dangling pointer into memory just
unmapped. `Shutdown()` then does `if (fAccelerantHook != NULL)
fAccelerantHook(B_UNINIT_ACCELERANT, ...)` — jumping into unmapped memory. Pre-
existing bug, never reachable before: the old single-GPU scan loop just tried the
next device in the same loop on failure, never destroying a `Screen` mid-loop; the
new multi-GPU per-device-path code (Phase 0) tries each device exactly once and
destroys its `Screen` immediately on failure, making this reachable every time any
card's accelerant fails to init. Fixed identically to the old branch: reset
`fAccelerantHook = NULL` (and `fAccelerantImage = -1` in the third path) in all
three failure returns of `AccelerantHWInterface::_OpenAccelerant()`. Build verified.

**Separate, severe finding surfaced while investigating (not a regression from this
work, but a real landmine)**: the old `MultiGPU` branch has a commit (`53738df82f`)
disabling sm750's 4 hardware 2D-acceleration hooks (`B_FILL_RECTANGLE`,
`B_SCREEN_TO_SCREEN_BLIT`, `B_INVERT_RECTANGLE`, `B_FILL_SPAN` → all `return NULL`)
because enabling them hangs the machine (a real interrupt-handling bug on this
card) — exactly what `MULTIGPU_PROJECT_REPORT.md` already documented as "confirmed
necessary, load-bearing". `MultiMonitor_WS` never had this disable (independent
history). Dormant today (`fUserEnable2DAcceleration` defaults false), but a latent
hang risk the moment 2D acceleration gets enabled on sm750. Ported the same disable
proactively, with a comment explaining why and warning not to re-enable without
re-confirming on real hardware. Build verified.

## Third real-hardware test (2026-11-27) — ATI `InitAccelerant()` XCLK fix + proactive EDID-theft fix

No more KDL. `device_open()` now succeeds for ATI (0x0, open count: 1) — the ROM-BAR
fix worked. New failure one step later: `InitAccelerant()` fails with "Unsupported
XCLK source: 5." Exactly the same bug and fix already found in `MultiGPU` (commits
`649d0dd8cf` + `40433358b9`): `Mach64_InitDSPParams()` decodes `PLL_XCLK_CNTL`'s low
3 bits (codes 0-7) but only handled 0-4. A secondary card (never firmware-POSTed)
is left at its raw post-reset register value — code 5 on this chip — and the switch
treated it as unsupported hardware. Verified against Linux fbdev's `mach64_ct.c`
(`aty_postdividers[8] = {1,2,4,8,3,5,6,12}`) before touching more register logic:
codes 5/6/7 are real divisors (5,6,12), decomposed into this driver's own
`xClkRefDivider * 2^xClkPostDivider` scheme matching the existing case 4. Ported
identically into `mach64_init.cpp`. Build verified.

**Proactive fix, before the user even hit it**: the very next `MultiGPU` commit
(`3e0c9d5cef`) documents the next real bug hit after this exact fix: ATI
unconditionally inheriting `VESA_EDID_BOOT_INFO` (assuming "I'm the primary"),
stealing the real primary's (SM750's) monitor EDID when running as secondary.
Fixed for `ati` (gated behind `boot_framebuffer_belongs_to_this_device`), and
**proactively checked and fixed the identical pattern in 5 more in-scope drivers**
that have it: `s3`, `trident`, `kyro`, `xgi`, `intel_extreme`, `intel_arc`.
`radeon_hd` and `3dfx` don't have this code path at all (confirmed, no fix needed);
`sm750` already covered by an earlier fix this session. Build verified for all 6.

## Fourth real-hardware finding (2026-11-27) — cursor subsystem never made per-screen (new bug, not ported from `MultiGPU`)

After the XCLK fix, the syslog confirmed ATI's own mode-set succeeds (1920x1080 for
the secondary) — genuine forward progress, further than the old `MultiGPU` branch
ever got per its own closing report. But the user then reported garbled, wrong-
pitch/repeated-looking imagery on **SM750's own display** (the primary, the one
physically being looked at), independent of mouse movement, made worse (painted
white) specifically along the path the mouse cursor travels. Extensive git-log
archaeology against both `MultiGPU` and `MultiGPUActivation` found no matching
historical fix — because this isn't a hardware-compat bug like the previous three,
it's a **gap in this plan's own Phase 3 work**, never flagged before.

Root cause, confirmed by reading the actual code (not guessed): `Desktop::HWInterface()`
(`Desktop.h`) and `Desktop::GetDrawingEngine()` forwarded to
`fVirtualScreen.HWInterface()`/`fVirtualScreen.DrawingEngine()` — a **single cached
pointer** that `VirtualScreen::AddScreen()` (`VirtualScreen.cpp:154-155`) overwrites
on every screen it iterates, with its own long-standing comment admitting it:
`// TODO: this works only for single screen configurations`. With one GPU this was
always correct by definition. With two, the cache simply becomes whichever screen
`ScreenManager` happens to enumerate **last** — unrelated to which screen is actually
focused or BDF-primary. Phase 3 generalized window rendering/clipping/compositing
per-screen correctly, but never touched the **cursor** path, which still runs
entirely through this one stale pointer, server-wide:
- `EventDispatcher::fHWInterface` — cached **once**, at `Desktop::Init()`, from this
  same single-cache pointer, and never refreshed again anywhere else in the
  codebase (confirmed: only one `SetHWInterface()` caller existed). Every
  `MoveCursorTo()` for the life of the session targeted that one interface only.
- `ServerApp.cpp` (`SetCursorVisible`, `ObscureCursor`) and `ServerWindow.cpp`
  (drag-bitmap hide/show `SetCursorVisible`) and `Desktop.cpp`'s own `SetCursor`
  calls all go through `Desktop::HWInterface()` directly — same stale pointer.

Practical effect: cursor move/draw/restore operations were being issued against
whichever screen won the discovery-order race (plausibly ATI, added after SM750),
while the user was physically looking at and moving the mouse over SM750 — a
hardware-geometry mismatch (wrong pitch/resolution) between where the software-
cursor backup/restore logic believes it's reading/writing and what's actually
driving the visible display, which matches "repeated/wrong-pitch image, gets worse
specifically where the mouse has been" far better than a genuine PCI/IRQ conflict
hypothesis (checked and ruled out first: confirmed `ati`'s `OUTREG8`/`INREG8`
register access is pure per-device MMIO via its own cloned `regsArea`
(`accelerant.h:277-283`, `accelerant.cpp:36-67`) — not a shared legacy VGA port
range, so this is not a bus-level collision between the two cards).

**Fix applied** (`Desktop.h`, `Desktop.cpp`), build-verified (`jam app_server` clean):
- `Desktop::HWInterface()`/`GetDrawingEngine()` now resolve via
  `ScreenForWorkspace(fCurrentWorkspace)` — the same per-screen primitive Phase 3
  already established for new-window binding (`ServerWindow::MakeWindow`) — instead
  of the raw `VirtualScreen` single cache. This alone fixes every caller that goes
  through `Desktop::HWInterface()` (`ServerApp`'s cursor hide/show/obscure,
  `ServerWindow`'s drag-bitmap cursor hide/show, `Desktop`'s own `SetCursor` calls,
  plus `Desktop::Init()`'s initial `MoveCursorTo`/`CreateEventStream`/
  `SetCursorVisible`, updated to call the accessor instead of
  `fVirtualScreen.HWInterface()` directly) in one place.
- `Desktop::_SetWorkspace()`: added `fEventDispatcher.SetHWInterface(targetScreen->
  HWInterface())` right after `fCurrentWorkspace = index` — so `EventDispatcher`'s
  separately-cached pointer for live `MoveCursorTo()` calls stays in sync with
  whichever screen's row the user is actually focused on, instead of being frozen
  at whatever `Desktop::Init()` happened to resolve at boot.
- Single-GPU behavior is unchanged: with one screen, `ScreenForWorkspace()` always
  returns that one screen, identical to today.

**Retracted as the explanation for the garbled image** (the fix itself is still
correct and stays in): the user pointed out, correctly, that the two monitors are
physically separate outputs — a cursor command misrouted to ATI's `HWInterface`
could not possibly bleed onto SM750's own physical display either way, and a
single-pixel cursor routing bug cannot produce a full-screen repeated-pitch pattern
regardless. Cursor routing was a real bug, worth fixing, but not *this* bug.

## Fifth finding (2026-11-27) — found by scrolling `MultiGPU`'s actual commit history, as the user asked

Per the user's direct suggestion ("scorri i fix fatti in MultiGPU... vedi se c'era
un fix che potrebbe sembrare una correzione per questo problema"): `git log
MultiGPU -- src/servers/app/Desktop.cpp` surfaced the *last* commit on that branch,
`82289e7a47` ("Multi-GPU: scope per-workspace screen reconfiguration, stop
background staleness across switches") — made right before that project paused on
this exact unresolved corruption. Its message independently names the same
direct-rendering (no-compositor) condition the user just confirmed is active here
("il compositor 2d è spento, siamo in AGG puro"). It bundles three sub-fixes, all
three real, confirmed-present bugs in this branch's current code (not guesses), all
three now ported and build-verified:

1. **`VirtualScreen::SetConfiguration()`/`AddScreen()` touched every discovered
   screen's hardware on every call, not just the one actually being reconfigured.**
   Confirmed still true here: `AddScreen()`'s loop unconditionally calls
   `screen->SetMode()`/`SetPreferredMode()`/`SetBestMode()` for *every* screen in
   `fScreenList`, and `Desktop::_SetCurrentWorkspaceConfiguration()` — called on
   **every workspace switch** — passed no target screen at all, so a switch on
   SM750's own row was still re-driving a real mode-set ioctl against ATI (and
   vice versa) every single time. On hardware sharing a bus/bridge, exactly the
   kind of unrelated hardware touch that could disturb whatever the other card is
   currently displaying. Fixed: `VirtualScreen::SetConfiguration()`/`AddScreen()`
   gained an `onlyReconfigure`/`touchHardware` parameter (`VirtualScreen.h/.cpp`);
   `Desktop::_SetCurrentWorkspaceConfiguration()` now passes
   `ScreenForWorkspace(fCurrentWorkspace)` as the only screen allowed to actually
   touch hardware (`Desktop.cpp`). `Desktop::Init()`'s own first-ever call (line
   ~521) deliberately keeps the default (`NULL` = reconfigure everyone) — that one
   boot-time call is supposed to bring up every screen for the first time.
2. **`Desktop::_SetWorkspace()` never cleared the target screen's
   `BackgroundRegion()` staleness cache on a switch.** `_SetBackground()` only
   repaints the part of its given region that the cache doesn't already believe is
   background — correct when a compositor rebuilds the whole picture from each
   window's own backing bitmap every frame regardless, wrong in direct-rendering
   mode (confirmed active here): an area the *previous* workspace's now-hidden
   windows were still physically drawn over stays exactly as it was, while the
   cache still believes it's valid, already-painted background — so it never gets
   repainted, and stale pixels from whatever was there before stay visible as
   "background" in the new workspace. This is the closest match of the three to
   "looks like old/repeated content, not a fresh picture." Fixed: added
   `if (!fCompositor2DEnabled) targetScreen->BackgroundRegion().MakeEmpty();`
   right before `_SetWorkspace()`'s own `_RebuildClippingForAllWindows`/
   `_SetBackground` calls.
3. **`Desktop::Redraw()` built its dirty region from `fVirtualScreen.Frame()`** —
   the *combined*, summed-width span across every discovered screen (stock Haiku's
   extended-desktop semantics from `VirtualScreen::UpdateFrame()`), not any one
   screen's own local geometry — then fed that oversized region into `MarkDirty()`
   against whichever one screen the default resolves to. Same bug class the old
   branch found and fixed in `Desktop::Init()`'s `fScreenRegion` seeding (not
   applicable verbatim here, since this branch already moved that state onto
   `Screen` itself in Phase 3 — but `Redraw()` had an un-ported instance of the
   same mistake). `Redraw()` is reachable from four `ServerApp.cpp` BScreen-API
   handlers (brightness/color-space/resolution-change requests), so a `BScreen`
   call from any app could have been corrupting the *other* screen's geometry
   handling, independent of the two fixes above. Fixed: `Desktop::Redraw()` now
   loops `ActiveGpuScreens()`, marking each one dirty with its own `Frame()`
   against its own `DisplayedWorkspace()`, instead of one oversized combined
   region against one implicit screen.

All three build-verified (`jam app_server` clean) together. Single-GPU behavior
unchanged in all three (one screen: `onlyReconfigure` is moot, the background-
staleness clear only ever targets that one screen exactly as before, `Redraw()`'s
loop runs its body once over the same frame it always used).

**Retracted — wrong layer entirely.** Real-hardware retest: none of the three
app_server-level fixes above changed the garbled image, and the user clarified the
actual timing precisely: the corruption appears **the moment kernel driver loading
finishes** — before app_server even starts. All Phase-3/`Desktop.cpp` work happens
far too late to be the cause; this was a wrong turn, not a wrong fix.

## Sixth finding (2026-11-27) — the real cause, found by the user pointing at a specific commit: `3275d915d3`

User named the commit directly. It fixes a **kernel-memory out-of-bounds write in
the boot-splash drawing code itself** (`draw_ati_logo_safe()` in `ati/driver.cpp`,
`draw_logo()` in `sm750/init.c`) — a different, more subtle bug than the
`boot_framebuffer_belongs_to_this_device()` ownership gate already ported in an
earlier session (Phase 1 pre-test fix): that gate controls *whether* a card draws
its boot logo at all; this bug is about a card that correctly *is* allowed to draw
(including the primary, SM750, itself) still computing its pixel-copy loop's
`fbIndex` from **borrowed geometry** — `FRAME_BUFFER_BOOT_INFO`'s
width/height/bytes_per_row, which describes whichever single card the boot loader
actually used, not necessarily the specific device whose `device_open()`/logo code
is currently running — instead of clamping against `fb_size`, the device's own
actually-mapped VRAM BAR capacity. With one card these always coincide (the
boot loader's primary IS that one card); with two, a mismatch between the borrowed
screen geometry and this card's own, differently-sized VRAM BAR lets `fbIndex` run
past the mapped area, corrupting whatever kernel memory happens to follow it —
silently, at the exact moment driver loading finishes, with no crash yet. Later,
anything else touching that corrupted region (the old branch's own test saw this
surface eventually as an unreadable, binary debug_server report attributed to
app_server) produces exactly the kind of "makes no sense, wrong layer" symptom
this session chased through `Desktop.cpp` for an entire afternoon before the user
pointed at the actual commit.

**Fix applied and build-verified** (`jam ati sm750` clean, kernel drivers only —
nothing in `app_server` needed to change): both `draw_ati_logo_safe()` and
`draw_logo()` gained an explicit `fbCapacityWords = fb_size / 4` bound, with a
`continue` whenever the computed `fbIndex` would exceed it — stops the write from
ever leaving the area actually mapped for that specific device, regardless of how
wrong the borrowed geometry is. No change to mode/geometry selection logic itself.

**Retracted again — timing still doesn't match.** Real-hardware retest: image
unchanged. The user confirmed ATI alone, standalone, at this exact 1920x1080@16bpp
mode, already works perfectly (no corruption at all) — ruling out a Mach64/pitch/
mode-setting bug entirely, kernel-level or accelerant-level. The user's own
diagnosis, stated directly: **"non è un problema di driver, penso sia un problema
di come l'app server gestisca gli acceleranti"** — not a driver problem, a problem
in how app_server manages the accelerants. Correct call; this is the thread that
actually led somewhere.

## Seventh finding (2026-11-27) — found by following the user's own diagnosis: `Desktop::Init()` never initializes any screen but the focused one

`Desktop::Init()`'s one-time boot sequence calls `_RebuildClippingForAllWindows()`/
`_SetBackground()` exactly once, unparameterized — both default to
`fCurrentWorkspace`, which at boot is always workspace 0, always the primary's
(SM750's) own row. Nothing else in `Init()` loops over `ActiveGpuScreens()`.
Practical effect: the secondary screen's (ATI's) own row is never visited at all
during a normal boot (the user never manually switches a workspace onto it), so
**app_server never draws anything to it, ever** — no background fill, no clipping
rebuild, nothing. Whatever was already sitting in that card's VRAM before this
point (kernel boot-splash leftovers, raw post-reset register garbage, anything)
just stays on screen indefinitely, indistinguishable from "random corruption" even
though app_server itself is running perfectly and has simply never touched that
framebuffer. This is exactly consistent with every observation so far: the ATI
driver/accelerant is provably fine standalone, the symptom only exists with both
cards active, and it is squarely an app_server-side omission, not a driver bug —
matching the user's own diagnosis precisely.

**Fix applied and build-verified** (`jam app_server` clean): `Desktop::Init()`'s
background/clipping seed is now a loop over `fActiveGpuScreens`, each screen
seeded against its own `DisplayedWorkspace()` instead of one unparameterized call
against whichever screen `fCurrentWorkspace` happens to resolve to. Single-GPU
behavior unchanged (loop body runs once, same screen as before).

**Real-hardware retest result — a regression, and a correction of my own prior
mistake.** The user's report, precisely: (1) only SM750's monitor shows any
signal at all now; (2) SM750 itself shows a flat, solid-colored background and
**nothing else** — no Tracker, no desktop, no icons; (3) separately, the S3
secondary still reports 0 MB VRAM exactly as before (untouched, expected). The
user's own diagnosis of what went wrong, which is correct: *"tu pensando di
impostare lo sfondo azzurro per la secondaria di fatto hai impostato il
comportamento sulla primaria"* — while aiming this session's fix at the
secondary, the change actually altered behavior on the primary, breaking it.

**Root cause of the regression, found on review, not guessed**: this finding's
own fix was fine (a narrow, bounded loop over `ActiveGpuScreens()`), but a
*different* change made earlier the same session — redefining `Desktop::
HWInterface()`/`GetDrawingEngine()` (Desktop.h) to resolve via
`ScreenForWorkspace(fCurrentWorkspace)` instead of the original
`fVirtualScreen.HWInterface()`/`DrawingEngine()` — had far more blast radius than
intended. Those two accessors are not multi-GPU-specific plumbing: `ServerApp.cpp`
alone uses `fDesktop->HWInterface()` for a long list of unrelated, pre-existing
BScreen-style queries (`ProposeMode`, `GetModeList`, `GetMonitorInfo`,
`SetDPMSMode`, `GetBrightness`, `GetDeviceInfo`, overlay checks, `ReadBitmap` via
`GetDrawingEngine()`, etc.) that have nothing to do with cursor routing — the
actual, narrow bug this redefinition was meant to fix (see "Third finding" above:
`EventDispatcher`'s cursor interface staying stuck on whatever screen
`VirtualScreen::AddScreen()` iterated last). Changing the general accessor's
*meaning* for everyone, instead of fixing only the handful of cursor-specific call
sites that actually needed focus-aware resolution, is what broke the primary's own
desktop.

**Fix applied and build-verified** (`jam app_server` clean): reverted
`Desktop::HWInterface()`/`GetDrawingEngine()` in `Desktop.h` to their original,
always-safe bodies (`fVirtualScreen.HWInterface()`/`DrawingEngine()`) — every
pre-existing caller is back to exactly its original, proven behavior. The cursor
fix is kept, but narrowed: `Desktop::Init()`'s cursor-setup lines
(`MoveCursorTo`/`CreateEventStream`/`fEventDispatcher.SetHWInterface`/
`SetCursorVisible`) now resolve `ScreenForWorkspace(fCurrentWorkspace)-
>HWInterface()` into a local `focusedInterface` variable and use that directly,
instead of going through the general accessor. `_SetWorkspace()`'s
`fEventDispatcher.SetHWInterface(targetScreen->HWInterface())` was already
call-site-local from when it was written (uses its own local `targetScreen`, never
touched the general accessor) and needed no change. The `ServerApp.cpp`/
`ServerWindow.cpp` direct `fDesktop->HWInterface()->SetCursorVisible(...)` call
sites automatically revert to their original behavior along with the accessor —
the cursor-hide/show-during-drag nuance they represent is deliberately left
unaddressed for now (the user already independently flagged that cursor routing
was never the explanation for the garbled-image mystery; not worth another
wide-blast-radius attempt right now).

**Not yet real-hardware retested.** This round's seventh-finding loop (seeding
background/clipping for every active screen in `Desktop::Init()`) is kept as-is —
on review its own logic looks sound and properly scoped (bounded by
`ActiveGpuScreens().CountItems()`, each iteration uses its own `Screen*` and
`DisplayedWorkspace()` directly, no shared/global state) and was not the most
likely culprit given the accessor's much larger, demonstrated blast radius. If the
primary's desktop is still broken after reverting the accessor alone, this loop
becomes the next and only remaining suspect from this session's changes, and
should be isolated next (e.g. temporarily skip every iteration but `i == 0`, or
add explicit `syslog()` calls around it, before assuming it's at fault).

## Eighth round (2026-11-27) — rigorous step-by-step review requested by the user, plus two concrete follow-ups

User asked for a precise, code-grounded walkthrough of how two simultaneously-live
screens are actually supposed to work end to end (discovery → per-device
accelerant load → `Screen` wrapping → `ScreenForWorkspace` row mapping → window
binding → per-screen rendering dispatch), stating a direct suspicion: *"secondo me
l'app_server gestisce 1 solo accelerante."* The walkthrough held up architecturally
(each step verified against the actual current code, not recalled from memory) with
one honest, confirmed exception already known since Phase 3: `Desktop::
HWInterface()`/`GetDrawingEngine()` still forward to `VirtualScreen`'s single
cached interface, used by a long list of pre-existing, non-multi-GPU-aware callers
in `ServerApp.cpp`.

**A second, much more important thing surfaced while explaining this**: confirmed,
via source (`src/kits/tracker/DeskWindow.cpp:214`), that Tracker's desktop
background/icons window (`DeskWindow`) is created with `B_ALL_WORKSPACES` — this is
the already-documented, still-unfixed Phase 3 gap (a `B_ALL_WORKSPACES` window's
content is only ever physically rendered by its one creation-time-bound
`DrawingEngine` without a compositor) turning from a hypothetical risk into the
single most likely reason the secondary screen has shown nothing but background
(or worse) in *every* test this session, independent of whatever hardware-level bug
was being chased at the time. Not fixed yet — offered to the user as the next
likely-highest-value piece of work, pending confirmation from the diagnostics below.

**User's three concrete asks, all addressed and build-verified this round**:

1. *"le schede secondarie non possono semplicemente avere il seed di sfondo?"* —
   already true (the Seventh finding's `Desktop::Init()` loop is unconditional over
   every `ActiveGpuScreens()` entry, not just the first) — flagged as needing an
   actual retest, not a code change.
2. *"io al giorno d'oggi non ho mai visto il monitor della secondaria acceso! MAI!"*
   — a stronger, more specific signal than "shows corruption": if the monitor never
   shows a signal at all (not even a wrong one), the most direct suspect is the
   DPMS decision in `_UpdateActiveGpuScreens()` (`SetDPMSMode(hasEDID ? ON : OFF)`)
   turning the secondary off outright if its `GetEDID()` call fails or an
   `edid_unset` override is set — purely a hypothesis until proven, so **added
   unconditional `syslog()` diagnostics** (not compiled out in release, matching
   the old branch's own established technique for exactly this kind of
   otherwise-invisible real-hardware decision) throughout `_UpdateActiveGpuScreens()`:
   per-screen BDF/primary/`GetEDID()` status code/`edid_unset` override/resulting
   DPMS mode, plus a final summary of `ActiveGpuScreens()`'s resulting order and
   each screen's `DisplayedWorkspace()`. The next boot's syslog will say definitively
   whether the secondary is being included as active with DPMS ON, or turned off by
   this code before anything else even gets a chance to draw to it.
3. *"se VirtualScreen opera solo su un'unica cache forse dovremmo creare una cache
   separata... fare quello che non hanno fatto nel commento"* — the old
   `// TODO: can we have a multiplexing HWInterface as well?` comment. A dynamic,
   per-focus multiplexer was already tried (as the fix for cursor routing a few
   rounds back) and reverted after it broke the primary's own desktop, precisely
   because it changed behavior for every one of those unrelated BScreen-style
   callers, not just the ones that needed it (see
   `feedback_narrow_fix_blast_radius`). The safe version of "doing what the comment
   never did": `VirtualScreen` gained `SetPrimaryInterface(Screen*)`
   (`VirtualScreen.h/.cpp`) — not a multiplexer, a correction of which ONE screen
   the existing single cache consistently points to. `Desktop::
   _UpdateActiveGpuScreens()` now calls it with `ActiveGpuScreens().ItemAt(0)`
   right after computing the active list, so every legacy caller of `Desktop::
   HWInterface()`/`GetDrawingEngine()` now consistently gets the *primary* GPU,
   instead of "whichever screen `VirtualScreen::AddScreen()`'s loop happened to
   iterate last" (pure discovery-order noise, the actual bug — not the fact that
   there's a single cache at all, which every one of those callers' own protocol
   already assumes by design). Single-GPU behavior unchanged (one candidate, pinning
   it is a no-op change from what was already there).

All three build-verified together (`jam app_server` clean). Not yet real-hardware
tested — this round's retest is specifically meant to answer, via the new syslog
output: (a) is the secondary screen actually ending up DPMS-ON and in
`ActiveGpuScreens()` at all, (b) does it now show the plain background color the
Seventh finding's loop should paint there, confirming app_server really is driving
it, before any further work on the `B_ALL_WORKSPACES` content-rendering gap.

## Ninth finding (2026-11-27) — the real-hardware test of the Eighth round's diagnostics found a genuine Phase 2 bug, present since it was written

The new `syslog()` output did its job. Two boots logged:

- **Both cards present**: `primaryBdf="030000"` (SM750, correctly auto-detected
  as boot primary via Phase 1's `IsBootPrimary()` hook — confirmed working) — but
  `active[0] bdf="0b0400"` (ATI) `displayedWorkspace=0`, `active[1] bdf="030000"`
  (SM750, the real primary) `displayedWorkspace=2`. **The primary ended up
  *second* in `ActiveGpuScreens()`.** Visible result: SM750's own monitor (the one
  the user is actually looking at) showed a flat blue screen, no objects, **no
  mouse cursor at all** — because workspace 0 (where the system always boots to,
  where Tracker's `B_ALL_WORKSPACES` desktop window and every other app create
  their windows by default, and where `Desktop::Init()`'s own `focusedInterface`
  resolves the boot-time cursor) was bound to **ATI**, not SM750. SM750 only ever
  received the plain workspace-2 background color fill from the Seventh finding's
  loop — which is exactly why it looked "blank," even though the loop itself was
  working correctly the whole time.
- **SM750 alone**: `primaryBdf="030000"`, one screen, `active[0]` is that same
  screen at `displayedWorkspace=0` — correct, matches the working single-GPU
  baseline exactly (confirms the bug is purely about *list order* with 2+ screens,
  not a problem with SM750 itself or with the diagnostics/pinning added last
  round).

**Root cause, found directly from this log, not guessed**: `_UpdateActiveGpuScreens()`
correctly computes `primaryBdf` (configured order's first entry, else
auto-detected boot primary via `IsBootPrimary()`, else discovery index 0) and uses
it correctly for the EDID-exemption/DPMS decision per screen — but the final
*list-building* step never actually sorted `primaryBdf` to the front unless an
explicit user-configured `gpu_order` happened to list it there. With no saved
order yet (a fresh test, as here), the code fell through to appending whatever was
left in `candidates` in **raw discovery order** — which has no relation to which
BDF is primary. ATI was discovered first by `ScreenManager`, so SM750 (correctly
identified as primary moments earlier in the very same function) landed second
anyway. This is a real bug in the original Phase 2 implementation, not something
introduced this round — it just took this round's diagnostics (and `SetPrimaryInterface()`
blindly trusting "index 0 is primary," an assumption that was never actually
guaranteed) to surface it concretely.

**Fix applied and build-verified** (`jam app_server` clean): right before the
final "append whatever's left of `candidates`" loop, `_UpdateActiveGpuScreens()`
now explicitly looks up `primaryBdf` in `candidateBdfs` and inserts it into
`fActiveGpuScreens` first if it's still unplaced (i.e. wasn't already placed by an
explicit configured-order match above) — guaranteeing the determined primary is
always `ActiveGpuScreens().ItemAt(0)`, regardless of raw discovery order. No
change needed for the `primaryIsFallback` case (empty `primaryBdf`): that path's
own definition of "primary" is already discovery index 0, so it was already
correctly first.

**Separate, still-open mystery, now isolated more precisely than before**: even
while ATI was (unintentionally) hosting all the real desktop content — Tracker,
cursor, DPMS correctly ON — the user reports ATI's own physical monitor showed
**no signal at all**, same as always. This now looks like a genuine, separate
hardware/signal-output problem specific to ATI's monitor output, independent of
anything app_server decides to draw — worth re-testing specifically once the
ordering fix is confirmed to restore SM750's own desktop, to see whether ATI shows
*anything* (even its own, correctly-assigned workspace-2 plain background) once it
is actually treated as the secondary as intended.

**Predicted, testable outcome, stated honestly before the test rather than after**:
after this fix, ATI's screen should show a **plain, solid workspace background
color** (there are no windows yet at `Init()` time, so this is a color fill, not a
wallpaper image) — not the repeating-photo-band pattern. If that prediction holds,
this was the actual root cause. If a *different* residual artifact shows up once
Tracker/a desktop-background window loads (a `B_ALL_WORKSPACES` window, which
Tracker's desktop window almost certainly is), that would point straight at the
already-documented, still-unfixed Phase 3 gap: a `B_ALL_WORKSPACES` window's
content is only ever physically rendered by its one creation-time-bound
`DrawingEngine` in this no-compositor configuration, so Tracker's wallpaper/icons
would still never actually paint on a screen other than the one Tracker happened
to start on — a separate, already-scoped-out-as-bigger task (see Phase 3 "Known
gaps" above), not something this fix attempts.

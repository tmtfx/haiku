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
  exists to replace (see Context below and report §6); Phase 3 replaces it properly
  instead of reintroducing it now.
  **Deferred to Phase 5** (no consumer yet, so untestable now): generalizing
  `AppServer.cpp`'s `'hsbm'`/`'h2da'`/`'gedc'`/`'sedc'`/`'hcai'` capability queries off
  their `ScreenAt(0)` hardcoding — that protocol only matters once the preflet UI
  exists to call it; `AllActiveGpusHaveHardwareCursor()`'s live enforcement (above)
  does not depend on it at all.

**Not started yet**: Phase 3 (the architectural core: per-screen live rendering),
Phase 4 (compositor all-GPUs gate for the preflet checkbox UI specifically — the live
enforcement already exists, see above), Phase 5 (preflet UI — also where the
`AppServer.cpp` query generalization now belongs), Phase 6 (hardware verification
loop).

**Right now**: Phase 0-2 are implemented and build-verified but not yet tested on
real hardware with a full image boot. Natural next step is either Phase 3 (the big
architectural one) or a real-hardware checkpoint boot first — ask the user which.

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

  **Note (updated during implementation)**: the hook actually ported keeps the
  original branch's `B_IS_BOOT_PRIMARY_PIRATI` private/vendor-range constant, not a
  new standard `Accelerant.h` enum member as first drafted above — the user asked
  explicitly for a direct copy of that mechanism. `GetPciLocation` needed no new
  ioctl at all; see the PROGRESS CHECKPOINT above.

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

  **Note (updated during implementation)**: done as described above except the
  `AppServer.cpp` query generalization, deferred to Phase 5 (no consumer exists yet
  to test it against) — see PROGRESS CHECKPOINT.

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

  **Note (updated during implementation)**: the live-toggle enforcement
  (`AllActiveGpusHaveHardwareCursor()`) was actually added during Phase 2 since it
  doesn't depend on any preflet protocol. What's left here is purely the preflet
  checkbox's own pre-check UI logic (Phase 5 territory).

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

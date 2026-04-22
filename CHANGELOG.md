# Changelog

All notable changes to this module.

## v9.5 — 2026-04-21

### Fixed
- **`ResizeIntelGpuBars` outer scan was function-0-only.** Changed to
  iterate `(rootDev 0..31) × (rootFunc 0..7)` with standard multi-
  function detection (header bit 0x80 on function 0). Previously the
  SB900 root port at `00:15.3` (function **3**) was never visited, so
  the Arc A770 — after being physically moved to the riser — stayed at
  its BIOS-assigned 256 MB BAR. This is the same class of bug already
  fixed in `Apply990FxBridgeQuirkAll` (v7) and `ResizeAmdGpuBars`
  (v9.4), propagated here by symmetry.

### Verified
- Four GPUs (2× Tesla P100, Arc A770, FirePro W9170) simultaneously
  allocated with 16 GB BAR each, above 4 GB, no kernel workaround.
- Bridge chain `00:15.3 → 23:00.0 → 24:01.0` transitions from 32-bit
  256 MB prefetchable window to 64-bit 16 GB window as expected.

## v9.4 — 2026-04-21 (morning)

### Changed
- **`ResizeAmdGpuBars` discovery rewritten from bridge-DID match to
  endpoint VID:DID match.** The W9170 is now found by scanning every
  root port for a descendant with VID:DID `1002:67A0`, instead of
  hardcoding the SB900 bridge at DID `0x43A3`. Placement (W9170 BAR0
  at `0x1C00000000`, BAR2 at `0x2000000000`) unchanged.

### Motivated by
- Physical swap of W9170 from riser (SB900, 2.5 GT/s x1, GART 16 GB
  cap) to slot 2 on the Northbridge side (SR5690, 5 GT/s x4, full
  32 GB VRAM reachable). The v9.3 discovery was tied to the riser
  bridge and no longer found the card after the swap.

## v9.3 — 2026-04-20

### Fixed
- **BUG-A:** `ARC_BAR0_TARGET = 0xC00000000` collided with P100 #1
  BAR3 (32 MB at a 16 GB-aligned slot). Arc target moved to BAR2 at
  `0x1800000000`.
- **BUG-B:** `W9170_BAR0_TARGET = 0x1400000000` collided with P100 #2
  BAR3 (same pattern). Moved to `0x1C00000000`.
- **BUG-C:** `if (barIndex != 0) continue` in `ResizeIntelGpuBars`
  skipped Arc's actual VRAM BAR (which is BAR2, not BAR0 — BAR0 is
  the 16 MB register aperture). Filter changed to `barIndex != 2`.

### Added
- **PatchM2MX v2:** DSDT M2MX expansion to 256 GB (root bus decode
  range), allowing the full MMIO64 layout to be visible to the OS.

### Verified
- First-ever deployment where W9170 BAR is 16 GB (every prior
  version ran with the stock 256 MB BAR on this card).

## v9.2 — 2026-04-19

### Added
- Explicit placement constants for Arc and W9170 target BARs. Removed
  the auto-bump cursor that was previously used (non-deterministic
  placement was causing different addresses across boots).
- Renamed the module from `ReBarDxe` to `990fxOrchestrator` in `.inf`
  and log banners (no source/GUID change — same FFS slot).

### Known issues at this revision (fixed in v9.3)
- Three placement bugs, see v9.3 BUG-A/B/C above.

## v9.0 – v9.1 — 2026-04-18 through 2026-04-19

### Added
- NVRAM logger (`ReBarBootLog` UEFI variable) with per-stage `[Dx]`
  tags and a final flush at ExitBootServices.
- POST port 0x80 byte codes for every stage (useful when NVRAM is not
  reachable).
- `Apply990FxBridgeQuirkAll`: writes pref64 low-bit on AMD SR5690
  root ports without touching upper32 (the previous "zero upper32 and
  hope the OS reallocates" approach lost devices above 4 GB).

## v8 — 2026-04-19

### Added
- Explicit upper32 programming on bridge `00:15.3` at pref window
  `0x1400000000 – 0x1BFFFFFFFF`. Worked in isolation but caused a
  conflict with Arc BAR placement (kernel was decoding the base as
  `0x1440000000`, not `0x1400000000` as written — bridge alignment
  requirement). Fixed in v9 by moving targets.

## v7 — 2026-04-19

### Fixed
- **`Apply990FxBridgeQuirkAll` function-0-only scan.** SB900 root
  port is at `00:15.3`; previous scan missed it entirely. Fix: iterate
  all 8 functions per device with multi-function detection. This is
  the first time this specific bug was fixed in the codebase; it
  recurred in v9.4 (`ResizeAmdGpuBars`) and v9.5 (`ResizeIntelGpuBars`).

## v6 and earlier — 2026-04-14 through 2026-04-18

### Experimental
- Link Disable approach to hiding oversized BARs from the BIOS
  resource allocator (replacing an earlier D3hot + PCI Read hook
  approach, which had driver compatibility issues).
- CPU Northbridge MMIO window programming via D18F1.
- DSDT `MALH` in-memory patch.

## v1 — initial fork point

### Base
- Forked from [xCuri0/ReBarUEFI][upstream], which provides the DXE
  driver skeleton, the PreprocessController hook mechanism, the basic
  ExitBootServices callback pattern, and the BAR probing helpers.
- No original xCuri0 code remains in v9.5. The `FILE_GUID` is kept
  intentionally as a tribute — see `README.md`.

[upstream]: https://github.com/xCuri0/ReBarUEFI

---

## Versioning convention

`v<major>.<minor>` where:

- **Major bumps** mark architectural changes (approach shifts — e.g.
  D3hot → Link Disable at v6, auto-cursor → explicit placement at v9.2).
- **Minor bumps** mark bugfixes and discovery refactors within the
  same approach.

No semver guarantee on the NVRAM log format or POST code map, though
those have been stable since v9.0.

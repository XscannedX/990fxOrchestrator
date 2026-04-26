/*
SPDX-License-Identifier: MIT

MODULE: 990fxOrchestrator (v9.5) — renamed from ReBarDxe
Target: Gigabyte GA-990FX-Gaming Rev 1.1 + AMD FX + multi-GPU

v9.5 (2026-04-21) — ResizeIntelGpuBars MULTI-FUNCTION SCAN:
  v9.4 PROBLEM: After the physical swap W9170 -> slot 2, Arc -> riser, the W9170
  ReBAR starts (v9.4 bus-agnostic discovery works), but the Arc regresses
  to a 256MB BAR. Runtime (2026-04-21):
    00:15.3 pref a0000000-b07fffff [32-bit]  <- SB900 riser bridge, func 3!
    23:00.0 pref a0000000-afffffff [32-bit]  <- upstream 4fa0
    24:01.0 pref a0000000-afffffff [32-bit]  <- downstream 4fa4
    25:00.0 Arc A770 Region 2: 256M          <- ReBAR not executed
  ROOT CAUSE: the ResizeIntelGpuBars outer scan used
    EFI_PCI_ADDRESS(0, rootDev, 0, 0)        <- HARDCODED func=0
  So 00:15.3 (func 3) was NEVER visited. The Arc on the riser was
  invisible to the entire chain-rewrite.
  This is the SAME bug that v7 fixed in Apply990FxBridgeQuirkAll
  ("the SB900 root port lives at 00:15.3 (function 3), NOT function 0")
  and that v9.4 fixed in ResizeAmdGpuBars — but ResizeIntelGpuBars
  had been left behind.
  v9.5 FIX: outer loop becomes (rootDev 0..31) x (rootFunc 0..7) with
  standard multi-function early-exit. Same pattern as v9.4 Amd.
  Nothing else is needed: the chain-rewrite (root port + intermediate
  switches) already writes pref base/limit with bit0=1 + upper32, so
  once the Arc is reached via 00:15.3, the 3 bridges (00:15.3, 23:00.0,
  24:01.0) automatically switch from 32-bit pref [a0000000-...] to
  64-bit pref [0x1800000000-0x1BFFFFFFFF].
  Target unchanged: ARC_BAR2_TARGET = 0x1800000000 (16GB).
  CHANGES vs v9.4:
    - ResizeIntelGpuBars: outer scan now func 0..7 with multi-function.
    - gpuProcessed becomes function-scoped (stop at first match).
    - Banner "v9.4 LOG" -> "v9.5 LOG".
    - Marker flush: "[DC] ResizeIntelGpuBars v9.5 enter" + new OK log.

v9.4 (2026-04-21) — BUS-AGNOSTIC W9170 DISCOVERY:
  v9.3 PROBLEM: ResizeAmdGpuBars was hardcoded to the 00:15.3 bridge DID
  0x43A3 (SB900 = riser). The post-v9.3 bench (bench_results_20260420_v9.3/
  RESULTS.md) showed that the W9170 on the riser hits the 16GB GART
  limit and crashes with models >16GB (gemma-31B HANG, unrecoverable
  ring timeout). The physical solution is to mount the W9170 directly
  in a motherboard slot (NB bridge, different DID) and put the Arc on
  the riser (the Arc tolerates reduced bandwidth because it is only
  used as an additional compute GPU).
  v9.4 FIX: ResizeAmdGpuBars now discovers the W9170 the way
  ResizeIntelGpuBars discovers the Arc — it scans all root ports on bus 0
  and looks for the 1002:67A0 endpoint (Hawaii XT GL) underneath them.
  Position-independent: works whether the W9170 is on the riser
  (00:15.3/43A3) or on an NB slot (00:0b.0/5A1F or 00:0d.0/5A1F).
  Target addresses unchanged from v9.3:
    W9170_BAR0_TARGET   = 0x1C00000000
    W9170_BAR2_TARGET   = 0x2000000000
    W9170_BRIDGE_BASE   = 0x1C00000000
    W9170_BRIDGE_LIMIT  = 0x2000FFFFFF
  CHANGES vs v9.3:
    - ResizeAmdGpuBars: discovery loop is root-port scan + endpoint VID:DID
      match (1002:67A0) instead of bridge VID:DID match (1002:43A3).
    - pciFindExtCapEcam + pciReadDwordEcam + pciWriteDwordEcam use
      epDev (discovered dynamically) instead of a hardcoded 0.
    - Log [DE] bridge hardcoded "00:15.3" -> dynamic log with bridgeDev/Func.
    - Banner: "v9.3 LOG" -> "v9.4 LOG"
    - Marker flush: "OK v9.3" -> "OK v9.4"

v9.3 (2026-04-20) — v9.2 PLACEMENT FIX + M2MX EXPANSION TO 256GB:
  PRINCIPLE (unchanged from v9.2, memory/collaboration_principles.md §3):
    The module decides where everything goes. No auto-bump cursors, no
    delegation to BIOS/kernel: constant target addresses per known device.
    Where the BIOS works (Tesla P100) we don't touch it; where it fails
    (Arc, W9170) we control everything ourselves, deterministically.
  RETROSPECTIVE — 3 v9.2 bugs fixed here:
    BUG-A: ARC_BAR0_TARGET = 0xC00000000 collided with P100 #1 BAR3 (32MB
           pref64 which the BIOS places in that 16GB slot). The idea "Arc
           in the gap between the P100s" was wrong: there is no gap, the
           first Tesla's 32MB BAR3 blocks a whole 16GB-aligned slot.
    BUG-B: W9170_BAR0_TARGET = 0x1400000000 collided with P100 #2 BAR3
           (same dynamic).
    BUG-C: The Arc VRAM is BAR2 (index 2), not BAR0. The filter
           "if (barIndex != 0) continue" in ResizeIntelGpuBars skipped
           the VRAM. Result: REBAR_CTRL written (BAR decodes 16GB)
           but BAR address not reprogrammed and bridge chain not widened
           -> i915 "Failed to setup region(-6) type=1" (black screen).
  v9.3 MMIO64 LAYOUT (M2MX 0 - 0x3FFFFFFFFF = 256 GB, expanded via PatchM2MX v2):
    0x800000000  - 0xBFFFFFFFF    P100 #1 BAR1 16 GB   [BIOS, not touched]
    0xC00000000  - 0xC01FFFFFF    P100 #1 BAR3 32 MB   [BIOS]
    0x1000000000 - 0x13FFFFFFFF   P100 #2 BAR1 16 GB   [BIOS, not touched]
    0x1400000000 - 0x1401FFFFFF   P100 #2 BAR3 32 MB   [BIOS]
    0x1800000000 - 0x1BFFFFFFFF   Arc A770 BAR2 16 GB  [OURS — ARC_BAR2_TARGET]
    0x1C00000000 - 0x1FFFFFFFFF   W9170 BAR0 16 GB     [OURS — W9170_BAR0_TARGET]
    0x2000000000 - 0x20007FFFFF   W9170 BAR2  8 MB     [OURS — W9170_BAR2_TARGET]
    0x2000800000 - 0x3FFFFFFFFF   spare ~126 GB        [free, future use]
  The Arc BAR2 at 0x1800000000 is where the BIOS already puts it natively;
  our ResizeIntelGpuBars just resizes the BAR from 256MB to 16GB and
  re-declares the range in the bridge chain. The W9170 goes at the start
  of the space just opened by the 256GB M2MX.
  CHANGES vs v9.2:
    - Constant renamed: ARC_BAR0_TARGET (0xC00000000) -> ARC_BAR2_TARGET
      (0x1800000000). Name + address both corrected.
    - W9170_BAR0_TARGET: 0x1400000000 -> 0x1C00000000 (out of collision).
    - W9170_BAR2_TARGET: 0x1800000000 -> 0x2000000000 (inside M2MX v2 space).
    - W9170_BRIDGE_BASE/LIMIT: [0x1400000000, 0x1800FFFFFF]
                            -> [0x1C00000000, 0x2000FFFFFF].
    - ResizeIntelGpuBars: filter "barIndex != 0" -> "barIndex != 2".
      Target written to BAR2 (offset 0x18/0x1C), not BAR0 (0x10/0x14).
    - Logger banner: "v9.2 LOG" -> "v9.3 LOG"
    - Marker flush: "OK v9.2" -> "OK v9.3"
  EXTERNAL DEPENDENCY:
    - PatchM2MX.txt v2 must be applied to the BIOS (expands ACPI
      QWordMemory Max 128GB->256GB, Length 124GB->252GB).
    - NB D18F1 Window 1 already programmed to 0-256GB in ProgramCpuMmioWindow
      (aligned with the new ACPI).

v9.2 (2026-04-20) — EXPLICIT PLACEMENT "we decide" [BUG-A/B/C]:
  See v9.3 above for the retrospective of the fixed bugs.
  Banner: "v9.1 LOG" -> "v9.2 LOG"

v9.1 (2026-04-20) targeted change relative to v9:
  - Apply990FxBridgeQuirkAll on 5a16/5a1c: NO longer zeroes upper32
    (0x28/0x2C). Only bit0=1 on Base/Limit (pref 64-bit declaration).
  - Reason for v9: writing 0 to upper32 squashed the pref window below 4G,
    causing "can't claim" to fail on the 64-bit BARs >4G of P100/Arc when
    the kernel booted without pci=realloc. With pci=realloc the kernel
    reallocated but P100 #1 disappeared (BAR2 32MB outside bridge window).
  - Fix: preserve the upper32 values that the BIOS/AGESA (or
    ResizeIntelGpuBars for the Arc chain) have already written correctly.
  - Logger banner: "v9 LOG" -> "v9.1 LOG"

v9 (2026-04-20) changes relative to v8:
  - REFACTOR Apply990FxBridgeQuirkAll: REMOVED the v8 branch for DID 0x43A3
    (which wrote base/limit/upper32 on the 00:15.3 bridge). Reason: the
    kernel decoded the v8 window as 0x1440000000-0x1BBFFFFFFF (not
    0x1400000000-0x1BFFFFFFFF as intended), conflict with Arc at
    0x1800000000, 384MB fallback, W9170 BAR0 remained 256MB.
  - NEW ResizeAmdGpuBars(): sole owner of the 00:15.3 window + W9170 ReBAR.
    Called in OnExitBootServices AFTER ResizeIntelGpuBars, when
    gNextPrefAddr = 0x1C00000000 (end of Arc). Programs:
      * 00:15.3 bridge pref64 window -> 0x1C00000000-0x1FFFFFFFFF (16GB)
      * REBAR_CTRL (cap+0x208) size index 14 (16GB) on W9170 endpoint
      * W9170 BAR0 -> 0x1C00000000 (64-bit pref)
    Target: FirePro W9170 DID=0x67A0 on bus 0x23 (via SB900 riser).
  - Diagnostic beep melody (behind #define ENABLE_BEEP_MELODY 1):
    2 short · 2 long · 2 short at 880 Hz via PIT 8253 + gate port 0x61,
    called BEFORE POST 0xFC (ear+LED sync). Useful on systems
    without video output (Arc losing display with BAR >4G).
  - New POST codes: 0xDE (AMD resize entry) and 0xDF (success/fail branching).
  - Logger banner: "v8 LOG" -> "v9 LOG"

v8 (2026-04-20) changes relative to v7 [REMOVED IN v9 — kept here for history]:
  - Apply990FxBridgeQuirkAll: additional branch on DID 0x43A3 (SB900 00:15.3)
    which wrote base/limit/upper32 pointing at 0x1400000000-0x1BFFFFFFFF.
    Case C problem: window decoded as 0x1440000000, conflict with Arc.
  - Logger banner: "v7 LOG" -> "v8 LOG"

v7 (2026-04-20) changes relative to v6:
  - Module renamed to "990fxOrchestrator" (visible in UEFITool/MMTool)
  - Bridge whitelist: added DID 0x43A3 (SB900 SB root port 00:15.3)
    -> enables FirePro W9100 support on the PCIe2 x1 riser via southbridge
  - Apply990FxBridgeQuirkAll: loop extended to all functions (0-7)
    -> the SB900 root port lives at 00:15.3 (func 3), it wasn't being scanned

This module is the orchestrator: it loads BEFORE PciBus and prepares
the entire hardware path for 64-bit MMIO, hides the P100 from the BIOS,
and makes it visible to the Linux kernel.

Multi-GPU target configuration:
  Slot 1: NVIDIA Tesla P100 16GB (compute, NVLink)
  Slot 2: NVIDIA Tesla P100 16GB (compute, NVLink)
  Slot 3: Intel Arc A770 16GB (display)
  x1 riser: AMD FirePro 32GB (compute)
  Total BAR space: ~80GB+ prefetchable

Execution sequence:
  1. DSDT patch: sets MALH=One in memory -> activates CRS1 (QWordMemory)
     for the 64-bit resources we wrote into the custom DSDT.
  2. CPU NB MMIO Window: DXE attempt on D18F1 Window 1 (may be lost
     before Linux — the definitive programming is at step 6).
  3. Pre-scan + Link Disable: scans all PCI buses, finds devices with
     64-bit BARs >4GB, disables the PCIe link on the upstream bridge.
     The device physically disappears (VID=0xFFFF at the hardware level).
     No software hook needed. The BIOS doesn't see it -> no d4/d6.
  4. PreprocessController hook (via RegisterProtocolNotify): when PciBus
     enumerates, we intercept every device for ReBAR setup. The 990FX
     bridge quirk is NOT applied during the BIOS (causes a black screen
     with offensive patches that force >4GB allocation).
  5. ExitBootServices:
     a. CPU NB MMIO Window 1 — DEFINITIVE programming (force=TRUE)
        from above-RAM up to 256GB. Covers all GPUs (2xP100+A770+FirePro).
        The DXE attempt (step 2) gets lost, this one survives.
     b. SR5690 HT Gate — B0:D0.0 reg 0x94=0x03 (RE+WE)
     c. 990FX bridge quirk — 64-bit prefetchable on all bridges
     d. Re-enable PCIe link on the hidden devices
     e. Link training poll (500ms timeout) + BAR programming above 4GB
        + bridge windows to avoid PNP conflict (BAR at 0x00000000)
     The entire 64-bit path is activated ONLY here, for Linux.
*/
#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/PciRootBridgeIo.h>
#include <IndustryStandard/Pci22.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <IndustryStandard/Acpi.h>
#include <Guid/Acpi.h>
#include <Library/IoLib.h>
#include "include/pciRegs.h"
#include "include/PciHostBridgeResourceAllocation.h"

// =====================================================================
// CUSTOM POST CODES — visible on the motherboard's LED display
// =====================================================================
// Range 0xD0-0xDF reserved for our module. Does not conflict with
// the standard AMI Aptio IV codes. Written to I/O port 0x80.
//
// Normal boot sequence:
//   D0 -> D1 -> D2 -> D3 -> D5 -> ... -> D8 -> D9 -> DA -> DB -> DC -> DD
//
// If the display stays stuck on a code, that step failed.
// The Dx codes get overwritten by the BIOS after our module.
//
#define POST_PORT               0x80

#define POST_MODULE_LOADED      0xD0  // rebarInit() started
#define POST_DSDT_PATCH         0xD1  // patching MALH in DSDT
#define POST_NB_MMIO_WINDOW     0xD2  // programming CPU NB D18F1 Window 1
#define POST_PRESCAN_START      0xD3  // start of PCI bus pre-scan
#define POST_LINK_DISABLE_DONE  0xD5  // devices hidden via Link Disable
#define POST_HOOK_REGISTERED    0xD6  // PreprocessController hook ready
#define POST_EXITBS_ENTER       0xD8  // ExitBootServices callback
#define POST_EXITBS_NB_WINDOW   0xD9  // NB MMIO Window forced at ExitBS
#define POST_EXITBS_HT_GATE     0xDA  // SR5690 HT gate opened
#define POST_EXITBS_LINK_EN     0xDB  // PCIe links re-enabled
#define POST_EXITBS_BAR_PROG    0xDC  // BARs programmed above 4GB
#define POST_EXITBS_DONE        0xDD  // handoff to Linux complete
#define POST_FULL_COMPLETE      0xFC  // FC = "Full Complete" — module active, all OK
// POST_EXITBS_AMD_REBAR (0xDE) and POST_AMD_REBAR_DONE (0xDF) — new in v9
#define POST_EXITBS_AMD_REBAR   0xDE  // v9: ResizeAmdGpuBars enter (W9170)
#define POST_AMD_REBAR_DONE     0xDF  // v9: ResizeAmdGpuBars exit (success or fail, see log)
#define POST_ERROR_GENERIC      0xEE  // generic module error
#define POST_ERROR_LINK_TRAIN   0xEF  // device not responding after link training

// v9: ENABLE_BEEP_MELODY = 1 -> plays PlayFcMelody() before POST 0xFC.
// Requires a populated SPEAKER header on the motherboard (GA-990FX-Gaming: 4-pin SPK_1).
#define ENABLE_BEEP_MELODY      1

// =====================================================================
// BARE-METAL BUSY-WAIT (safe inside EVT_SIGNAL_EXIT_BOOT_SERVICES)
// =====================================================================
// gBS->Stall() MUST NOT be called after ExitBootServices — the boot
// services table is no longer valid.  Use this macro instead; it is
// calibrated against the existing POST 0xFC delay: 200,000,000 iterations
// ≈ 2 seconds on this FX-based platform (conservative, timing is
// approximate and CPU-speed-dependent).
#define BUSY_WAIT_MS(ms)  do { \
    volatile UINT64 _bw; \
    for (_bw = 0; _bw < (UINT64)(ms) * 100000ULL; _bw++) {} \
} while (0)

#define PCI_POSSIBLE_ERROR(val) ((val) == 0xffffffff)
#define PCI_VENDOR_ID_ATI 0x1002
#define BUILD_YEAR 2012

// PCI Power Management
#define PCI_CAP_ID_PM               0x01
#define PCI_PM_CTRL                 4
#define PCI_PM_CTRL_STATE_MASK      0x0003
#define PCI_PM_CTRL_STATE_D0        0x0000
#define PCI_PM_CTRL_STATE_D3HOT     0x0003

// PCIe Link Control
#define PCI_CAP_ID_PCIE             0x10
#define PCIE_LINK_CONTROL_OFFSET    0x10   // offset from the PCIe cap
#define PCIE_LINK_DISABLE           (1 << 4)

#define MAX_HIDDEN_DEVICES          16

// ECAM (Enhanced Configuration Access Mechanism) for extended config space
// On AMD 990FX the ECAM base is at 0xE0000000 (confirmed from dmesg/PNP)
// Needed to access PCIe Extended Capabilities (offset > 0xFF)
// such as Resizable BAR (Cap ID 0x0015)
#define ECAM_BASE                   0xE0000000ULL
// PCI_EXT_CAP_ID_REBAR already defined in pciRegs.h

// =====================================================================
// v9.3 MMIO64 LAYOUT — EXPLICIT PLACEMENT (we decide)
// =====================================================================
// Constant target addresses for every device the module manages.
// Motivation: avoid any overlap with the BIOS allocations for the
// Tesla P100 (which work and we don't touch) + bit-exact determinism
// between reboots. All addresses are inside the DSDT M2MX window
// (0 - 0x3FFFFFFFFF, 256 GB — requires PatchM2MX.txt v2 on the BIOS).
//
//   [BIOS, not touched] P100 #1 BAR1  0x800000000  .. 0xBFFFFFFFF   (16GB)
//   [BIOS, not touched] P100 #1 BAR3  0xC00000000  .. 0xC01FFFFFF   (32MB)
//   [BIOS, not touched] P100 #2 BAR1  0x1000000000 .. 0x13FFFFFFFF  (16GB)
//   [BIOS, not touched] P100 #2 BAR3  0x1400000000 .. 0x1401FFFFFF  (32MB)
//   [OURS]              Arc A770 BAR2 0x1800000000 .. 0x1BFFFFFFFF  (16GB)
//   [OURS]              W9170 BAR0    0x1C00000000 .. 0x1FFFFFFFFF  (16GB)
//   [OURS]              W9170 BAR2    0x2000000000 .. 0x20007FFFFF  (8MB)
//   [spare]                           0x2000800000 .. 0x3FFFFFFFFF  (~126GB)
//
// Historical note v9.2: ARC was placed at 0xC00000000 and W9170 at 0x1400000000,
// both collided with the BAR3s (32MB 64-bit pref) of the two Teslas that
// occupy 16GB-aligned slots. Fix in v9.3: the whole OURS cluster moves
// above 0x1800000000, where the BIOS already naturally puts the Arc.
//
#define ARC_BAR2_TARGET          0x1800000000ULL   // Arc A770 BAR2 (VRAM) 16GB
#define W9170_BAR0_TARGET        0x1C00000000ULL   // W9170 BAR0 16GB
#define W9170_BAR2_TARGET        0x2000000000ULL   // W9170 BAR2 8MB
// Bridge 00:15.3 (SB900 root port toward W9170) must cover BAR0+BAR2:
// base aligned to 1MB: 0x1C00000000 -> base reg = 0x0000|0x01
// inclusive 1MB limit: 0x2000FFFFFF -> limit reg = 0x0FF0|0x01
//   upper32 base  = 0x1C, upper32 limit = 0x20
#define W9170_BRIDGE_BASE        0x1C00000000ULL
#define W9170_BRIDGE_LIMIT       0x2000FFFFFFULL   // inclusive (covers BAR0 16GB + BAR2 8MB + slack)

// Fallback-only cursors: used ONLY by ProgramHiddenDeviceBars for
// non-whitelist devices (those hidden via Link Disable and then not
// handled by the specific Resize* functions). For Arc and W9170 we use
// the constants above, NOT these cursors.
static UINT64 gNextPrefAddr = 0x800000000ULL;   // fallback 32GB start
static UINT64 gNextMmioAddr = 0x410000000ULL;   // fallback ~16.25GB start

// =====================================================================
// V6 LOGGER — writes boot log to the UEFI NVRAM variable ReBarBootLog
// =====================================================================
// 16KB RAM buffer, single flush at the end of OnExitBootServices via SetVariable.
// Readable from Linux: sudo cat /sys/firmware/efi/efivars/ReBarBootLog-<GUID>
// Minimal implementation without PrintLib to avoid touching .inf dependencies
//
#define REBAR_LOG_MAX  (16 * 1024)
static CHAR8  gLogBuf[REBAR_LOG_MAX];
static UINTN  gLogPos = 0;

static VOID LogC(CHAR8 c) {
    if (gLogPos < REBAR_LOG_MAX - 1) {
        gLogBuf[gLogPos++] = c;
        gLogBuf[gLogPos] = 0;
    }
}
static VOID LogS(const CHAR8 *s) {
    while (s && *s && gLogPos < REBAR_LOG_MAX - 1) {
        gLogBuf[gLogPos++] = *s++;
    }
    if (gLogPos < REBAR_LOG_MAX) gLogBuf[gLogPos] = 0;
}
static VOID LogHex(UINT64 v, UINTN digits) {
    CHAR8 tmp[17];
    UINTN i;
    if (digits > 16) digits = 16;
    for (i = 0; i < digits; i++) {
        UINTN nib = (UINTN)((v >> ((digits-1-i)*4)) & 0xF);
        tmp[i] = (CHAR8)(nib < 10 ? '0'+nib : 'a'+nib-10);
    }
    tmp[digits] = 0;
    LogS(tmp);
}
static VOID LogDec(UINT64 v) {
    CHAR8 tmp[21]; UINTN i = 0;
    if (v == 0) { LogC('0'); return; }
    while (v) { tmp[i++] = (CHAR8)('0' + v%10); v /= 10; }
    while (i--) LogC(tmp[i]);
}
#define L_STR(s)     LogS((const CHAR8*)(s))
#define L_HEX(v,d)   LogHex((UINT64)(v), (UINTN)(d))
#define L_DEC(v)     LogDec((UINT64)(v))
#define L_NL()       LogC('\n')

// Dump PCI config space (first 64 bytes) in format "BB:DD.F: XX XX XX ..."
static VOID LogPciConfig64(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo,
    UINT8 bus, UINT8 dev, UINT8 func)
{
    UINTN   i;
    UINT32  dw;
    UINT64  addr;
    L_STR("  cfg "); L_HEX(bus,2); L_STR(":"); L_HEX(dev,2); L_STR(".");
    L_HEX(func,1); L_STR(":");
    for (i = 0; i < 64; i += 4) {
        addr = EFI_PCI_ADDRESS(bus, dev, func, i);
        dw = 0xFFFFFFFFu;
        if (rbIo != NULL) {
            rbIo->Pci.Read(rbIo, EfiPciWidthUint32, addr, 1, &dw);
        }
        L_STR(" "); L_HEX(dw, 8);
        if ((i & 0x1F) == 0x1C) { L_NL(); L_STR("          "); }
    }
    L_NL();
}

// Dedicated GUID for the log variable (different from ReBarState)
// From Linux: /sys/firmware/efi/efivars/ReBarBootLog-b00710c0-a992-4a0f-8b54-0291517c21aa
static EFI_GUID gReBarLogGuid = {
    0xb00710c0, 0xa992, 0x4a0f,
    {0x8b, 0x54, 0x02, 0x91, 0x51, 0x7c, 0x21, 0xaa}
};

// Flush the buffer to an NVRAM variable. Call BEFORE disabling runtime services.
static VOID LogFlushToNvram(VOID) {
    UINT32 attrs = EFI_VARIABLE_NON_VOLATILE |
                   EFI_VARIABLE_BOOTSERVICE_ACCESS |
                   EFI_VARIABLE_RUNTIME_ACCESS;
    if (gRT == NULL || gLogPos == 0) return;
    gRT->SetVariable(
        L"ReBarBootLog",
        &gReBarLogGuid,
        attrs,
        gLogPos,
        gLogBuf);
}

// =====================================================================
// STRUCTURES AND GLOBAL VARIABLES
// =====================================================================

// For every hidden device we save the parent bridge and its Link Control
typedef struct {
    UINT8                            DevBus;   // device bus (for log)
    UINT8                            DevDev;
    UINT8                            DevFunc;
    UINT8                            BridgeBus;  // upstream bridge
    UINT8                            BridgeDev;
    UINT8                            BridgeFunc;
    UINT8                            PcieCapOffset;  // PCIe cap of the bridge
    UINT16                           OrigLinkCtrl;   // original Link Control value
    UINT16                           BridgeVid;      // bridge VID (for ExitBS quirk)
    UINT16                           BridgeDid;      // bridge DID (for ExitBS quirk)
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* RootBridgeIo;
} HIDDEN_DEVICE_ENTRY;

static HIDDEN_DEVICE_ENTRY  gHiddenDevices[MAX_HIDDEN_DEVICES];
static UINTN                gHiddenDeviceCount = 0;
static EFI_EVENT            gExitBootServicesEvent = NULL;
static EFI_EVENT            gResAllocNotifyEvent = NULL;
static VOID*                gResAllocNotifyReg = NULL;
static EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* gSavedRbIo = NULL; // for ExitBS

// Forward declarations for functions used in ExitBootServices
static BOOLEAN ProgramHtBridgeGate(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo);
static BOOLEAN ProgramCpuMmioWindow(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo, BOOLEAN force);

static EFI_GUID reBarStateGuid = {
    0xa3c5b77a, 0xc88f, 0x4a93,
    {0xbf, 0x1c, 0x4a, 0x92, 0xa3, 0x2c, 0x65, 0xce}
};

static UINT8 reBarState = 0;
static EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL* pciResAlloc;
static EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* pciRootBridgeIo;
static EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL_PREPROCESS_CONTROLLER o_PreprocessController;

// =====================================================================
// UTILITY
// =====================================================================

INTN fls(UINT32 x)
{
    UINT32 r = 0;
    if (x == 0) return -1;
    while (x >>= 1) r++;
    return (INTN)r;
}

UINT64 pciAddrOffset(UINTN pciAddress, INTN offset)
{
    UINTN reg  = (pciAddress & 0xffffffff00000000ULL) >> 32;
    UINTN bus  = (pciAddress & 0xff000000) >> 24;
    UINTN dev  = (pciAddress & 0xff0000) >> 16;
    UINTN func = (pciAddress & 0xff00) >> 8;
    return EFI_PCI_ADDRESS(bus, dev, func, ((INT64)reg + offset));
}

// =====================================================================
// PCI CONFIG ACCESS (uses the global pciRootBridgeIo)
// =====================================================================

EFI_STATUS pciReadConfigDword(UINTN pciAddress, INTN pos, UINT32* buf)
{
    return pciRootBridgeIo->Pci.Read(pciRootBridgeIo, EfiPciWidthUint32, pciAddrOffset(pciAddress, pos), 1, buf);
}

EFI_STATUS pciWriteConfigDword(UINTN pciAddress, INTN pos, UINT32* buf)
{
    return pciRootBridgeIo->Pci.Write(pciRootBridgeIo, EfiPciWidthUint32, pciAddrOffset(pciAddress, pos), 1, buf);
}

EFI_STATUS pciReadConfigWord(UINTN pciAddress, INTN pos, UINT16* buf)
{
    return pciRootBridgeIo->Pci.Read(pciRootBridgeIo, EfiPciWidthUint16, pciAddrOffset(pciAddress, pos), 1, buf);
}

EFI_STATUS pciWriteConfigWord(UINTN pciAddress, INTN pos, UINT16* buf)
{
    return pciRootBridgeIo->Pci.Write(pciRootBridgeIo, EfiPciWidthUint16, pciAddrOffset(pciAddress, pos), 1, buf);
}

EFI_STATUS pciReadConfigByte(UINTN pciAddress, INTN pos, UINT8* buf)
{
    return pciRootBridgeIo->Pci.Read(pciRootBridgeIo, EfiPciWidthUint8, pciAddrOffset(pciAddress, pos), 1, buf);
}

EFI_STATUS pciWriteConfigByte(UINTN pciAddress, INTN pos, UINT8* buf)
{
    return pciRootBridgeIo->Pci.Write(pciRootBridgeIo, EfiPciWidthUint8, pciAddrOffset(pciAddress, pos), 1, buf);
}

// Versions that accept an explicit rootBridgeIo (for the pre-scan)
static EFI_STATUS pciReadDword(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo, UINT64 addr, UINT32* buf)
{
    return rbIo->Pci.Read(rbIo, EfiPciWidthUint32, addr, 1, buf);
}

static EFI_STATUS pciWriteDword(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo, UINT64 addr, UINT32* buf)
{
    return rbIo->Pci.Write(rbIo, EfiPciWidthUint32, addr, 1, buf);
}

static EFI_STATUS pciReadWord(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo, UINT64 addr, UINT16* buf)
{
    return rbIo->Pci.Read(rbIo, EfiPciWidthUint16, addr, 1, buf);
}

static EFI_STATUS pciWriteWord(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo, UINT64 addr, UINT16* buf)
{
    return rbIo->Pci.Write(rbIo, EfiPciWidthUint16, addr, 1, buf);
}

static EFI_STATUS pciReadByte(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo, UINT64 addr, UINT8* buf)
{
    return rbIo->Pci.Read(rbIo, EfiPciWidthUint8, addr, 1, buf);
}

static EFI_STATUS pciWriteByte(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo, UINT64 addr, UINT8* buf)
{
    return rbIo->Pci.Write(rbIo, EfiPciWidthUint8, addr, 1, buf);
}

// =====================================================================
// PCI CAPABILITY SEARCH
// =====================================================================

UINT8 pciFindCapability(UINTN pciAddress, UINT8 capId)
{
    UINT16 status;
    UINT8  capPtr;
    UINT8  thisCapId;
    UINTN  ttl = 48;

    if (EFI_ERROR(pciReadConfigWord(pciAddress, 0x06, &status)))
        return 0;
    if (!(status & (1 << 4)))
        return 0;
    if (EFI_ERROR(pciReadConfigByte(pciAddress, 0x34, &capPtr)))
        return 0;
    capPtr &= 0xFC;

    while (capPtr && ttl--) {
        if (EFI_ERROR(pciReadConfigByte(pciAddress, (INTN)capPtr, &thisCapId)))
            break;
        if (thisCapId == capId)
            return capPtr;
        if (EFI_ERROR(pciReadConfigByte(pciAddress, (INTN)(capPtr + 1), &capPtr)))
            break;
        capPtr &= 0xFC;
    }
    return 0;
}

// Version with explicit rootBridgeIo (for pre-scan)
static UINT8 pciFindCapabilityEx(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo, UINT64 baseAddr, UINT8 capId)
{
    UINT16 status;
    UINT8  capPtr, thisCapId;
    UINTN  ttl = 48;

    if (EFI_ERROR(pciReadWord(rbIo, baseAddr + 0x06, &status)))
        return 0;
    if (!(status & (1 << 4)))
        return 0;
    if (EFI_ERROR(pciReadByte(rbIo, baseAddr + 0x34, &capPtr)))
        return 0;
    capPtr &= 0xFC;

    while (capPtr && ttl--) {
        if (EFI_ERROR(pciReadByte(rbIo, baseAddr + capPtr, &thisCapId)))
            break;
        if (thisCapId == capId)
            return capPtr;
        if (EFI_ERROR(pciReadByte(rbIo, baseAddr + capPtr + 1, &capPtr)))
            break;
        capPtr &= 0xFC;
    }
    return 0;
}

UINT16 pciFindExtCapability(UINTN pciAddress, INTN cap)
{
    INTN ttl;
    UINT32 header;
    UINT16 pos = PCI_CFG_SPACE_SIZE;

    ttl = (PCI_CFG_SPACE_EXP_SIZE - PCI_CFG_SPACE_SIZE) / 8;
    if (EFI_ERROR(pciReadConfigDword(pciAddress, pos, &header)))
        return 0;
    if (header == 0 || PCI_POSSIBLE_ERROR(header))
        return 0;

    while (ttl-- > 0) {
        if (PCI_EXT_CAP_ID(header) == cap && pos != 0)
            return pos;
        pos = PCI_EXT_CAP_NEXT(header);
        if (pos < PCI_CFG_SPACE_SIZE)
            break;
        if (EFI_ERROR(pciReadConfigDword(pciAddress, pos, &header)))
            break;
    }
    return 0;
}

// =====================================================================
// REBAR LOGIC
// =====================================================================

INTN pciRebarFindPos(UINTN pciAddress, INTN pos, UINT8 bar)
{
    UINTN nbars, i;
    UINT32 ctrl = 0;

    if (EFI_ERROR(pciReadConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl)))
        return -1;
    nbars = (ctrl & PCI_REBAR_CTRL_NBAR_MASK) >> PCI_REBAR_CTRL_NBAR_SHIFT;

    for (i = 0; i < nbars; i++, pos += 8) {
        UINTN bar_idx;
        ctrl = 0;
        if (EFI_ERROR(pciReadConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl)))
            continue;
        bar_idx = ctrl & PCI_REBAR_CTRL_BAR_IDX;
        if (bar_idx == bar)
            return pos;
    }
    return -1;
}

UINT32 pciRebarGetPossibleSizes(UINTN pciAddress, UINTN epos, UINT16 vid, UINT16 did, UINT8 bar)
{
    INTN pos;
    UINT32 cap = 0;

    pos = pciRebarFindPos(pciAddress, (INTN)epos, bar);
    if (pos < 0) return 0;

    if (EFI_ERROR(pciReadConfigDword(pciAddress, pos + PCI_REBAR_CAP, &cap)))
        return 0;
    cap &= PCI_REBAR_CAP_SIZES;

    if (vid == PCI_VENDOR_ID_ATI && did == 0x731f &&
        bar == 0 && cap == 0x7000)
        cap = 0x3f000;

    return cap >> 4;
}

INTN pciRebarSetSize(UINTN pciAddress, UINTN epos, UINT8 bar, UINT8 size)
{
    INTN pos;
    UINT32 ctrl = 0;

    pos = pciRebarFindPos(pciAddress, (INTN)epos, bar);
    if (pos < 0) return pos;

    if (EFI_ERROR(pciReadConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl)))
        return -1;
    ctrl &= (UINT32)~PCI_REBAR_CTRL_BAR_SIZE;
    ctrl |= (UINT32)size << PCI_REBAR_CTRL_BAR_SHIFT;

    pciWriteConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl);
    return 0;
}

// =====================================================================
// DEVICE HIDDEN CHECK (for PreprocessController)
// =====================================================================

static BOOLEAN IsDeviceHidden(UINT8 bus, UINT8 dev, UINT8 func)
{
    UINTN i;
    for (i = 0; i < gHiddenDeviceCount; i++) {
        if (gHiddenDevices[i].DevBus  == bus &&
            gHiddenDevices[i].DevDev  == dev &&
            gHiddenDevices[i].DevFunc == func)
            return TRUE;
    }
    return FALSE;
}

// =====================================================================
// PRE-SCAN: FIND AND HIDE DEVICES WITH BAR >4GB
// =====================================================================
//
// Scans all PCI buses BEFORE PciBus does enumeration.
// For every device found with a 64-bit prefetchable BAR >4GB:
//   1. Disable the PCIe link on the upstream bridge (Link Disable)
//   2. The device physically disappears (VID=0xFFFF at hardware level)
//   3. Add it to the gHiddenDevices list for restoration
//
// PciBus doesn't see the device -> no d4/d6 error.
// At ExitBootServices the link is re-enabled for Linux.
//

static BOOLEAN DeviceHasLargeBarEx(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo,
    UINT64 baseAddr)
{
    UINT8 barIndex;

    for (barIndex = 0; barIndex <= 4; barIndex++) {
        UINT64 barAddr = baseAddr + 0x10 + (barIndex * 4);
        UINT32 bar0Val, bar1Orig, bar1Probe;
        UINT32 allOnes = 0xFFFFFFFF;

        if (EFI_ERROR(pciReadDword(rbIo, barAddr, &bar0Val)))
            continue;

        // Memory (bit0=0), 64-bit (bit[2:1]=10), Prefetchable (bit3=1)
        if ((bar0Val & 0x01) != 0)    continue;
        if ((bar0Val & 0x06) != 0x04) continue;
        if ((bar0Val & 0x08) == 0)    continue;

        // Upper DWORD of the 64-bit BAR
        if (EFI_ERROR(pciReadDword(rbIo, barAddr + 4, &bar1Orig)))
            continue;

        // Size probe: write all 1s, read, restore
        if (EFI_ERROR(pciWriteDword(rbIo, barAddr + 4, &allOnes)))
            continue;
        if (EFI_ERROR(pciReadDword(rbIo, barAddr + 4, &bar1Probe))) {
            pciWriteDword(rbIo, barAddr + 4, &bar1Orig);
            continue;
        }
        pciWriteDword(rbIo, barAddr + 4, &bar1Orig);

        // Upper DWORD with significant bits -> BAR > 4GB
        if (bar1Probe != 0 && bar1Probe != 0xFFFFFFFF) {
            DEBUG((DEBUG_INFO,
                "ReBarDXE: Pre-scan BAR%d upper probe=0x%08x -> >4GB\n",
                barIndex, bar1Probe));
            return TRUE;
        }

        barIndex++; // skip upper DWORD
    }
    return FALSE;
}

// =====================================================================
// FIND THE UPSTREAM PCIe BRIDGE OF A DEVICE
// =====================================================================
// Scan buses < deviceBus looking for a PCI-PCI bridge whose secondary
// bus number matches deviceBus. That is the upstream bridge.

static BOOLEAN FindParentBridge(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo,
    UINT8 deviceBus,
    UINT8* outBridgeBus, UINT8* outBridgeDev, UINT8* outBridgeFunc)
{
    UINT16 scanBus;

    for (scanBus = 0; scanBus < deviceBus; scanBus++) {
        UINT8 scanDev;
        for (scanDev = 0; scanDev < 32; scanDev++) {
            UINT8 scanFunc, maxFunc = 1;
            for (scanFunc = 0; scanFunc < maxFunc; scanFunc++) {
                UINT64 addr = EFI_PCI_ADDRESS(scanBus, scanDev, scanFunc, 0);
                UINT32 vidDid;
                UINT8  headerType, secBus;

                if (EFI_ERROR(pciReadDword(rbIo, addr, &vidDid)))
                    continue;
                if ((vidDid & 0xFFFF) == 0xFFFF || (vidDid & 0xFFFF) == 0)
                    continue;

                if (scanFunc == 0) {
                    if (!EFI_ERROR(pciReadByte(rbIo, addr + 0x0E, &headerType))) {
                        if (headerType & 0x80) maxFunc = 8;
                    }
                } else {
                    pciReadByte(rbIo, addr + 0x0E, &headerType);
                }

                // Is it a PCI-PCI bridge? (header type 1)
                if ((headerType & 0x7F) != 1)
                    continue;

                // Read secondary bus number (offset 0x19)
                if (EFI_ERROR(pciReadByte(rbIo, addr + 0x19, &secBus)))
                    continue;

                if (secBus == deviceBus) {
                    *outBridgeBus  = (UINT8)scanBus;
                    *outBridgeDev  = scanDev;
                    *outBridgeFunc = scanFunc;
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}

// =====================================================================
// HIDE DEVICE VIA PCIe LINK DISABLE ON THE UPSTREAM BRIDGE
// =====================================================================
// Disables the PCIe link on the upstream bridge: the device physically
// disappears from the PCI bus. Both UEFI and legacy PCI BIOS (CSM via
// CF8/CFC) see VID=0xFFFF. No software hook needed.

static VOID HideDeviceViaLinkDisable(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo,
    UINT8 bus, UINT8 dev, UINT8 func)
{
    UINT8  bridgeBus, bridgeDev, bridgeFunc;
    UINT64 bridgeAddr;
    UINT8  pcieCap;
    UINT16 linkCtrl, newLinkCtrl;
    UINT32 bridgeVidDid;

    if (gHiddenDeviceCount >= MAX_HIDDEN_DEVICES)
        return;

    // Find the upstream bridge
    if (!FindParentBridge(rbIo, bus, &bridgeBus, &bridgeDev, &bridgeFunc)) {
        DEBUG((DEBUG_WARN,
            "ReBarDXE: Device %02x:%02x.%x - upstream bridge not found!\n",
            bus, dev, func));
        return;
    }

    bridgeAddr = EFI_PCI_ADDRESS(bridgeBus, bridgeDev, bridgeFunc, 0);

    // Read VID/DID of the bridge (needed for the ExitBootServices quirk)
    if (EFI_ERROR(pciReadDword(rbIo, bridgeAddr, &bridgeVidDid)))
        return;

    // Find PCIe capability on the bridge
    pcieCap = pciFindCapabilityEx(rbIo, bridgeAddr, PCI_CAP_ID_PCIE);
    if (pcieCap == 0) {
        DEBUG((DEBUG_WARN,
            "ReBarDXE: Bridge %02x:%02x.%x without PCIe cap\n",
            bridgeBus, bridgeDev, bridgeFunc));
        return;
    }

    // Read Link Control
    if (EFI_ERROR(pciReadWord(rbIo,
            bridgeAddr + pcieCap + PCIE_LINK_CONTROL_OFFSET, &linkCtrl)))
        return;

    // Set Link Disable (bit 4)
    newLinkCtrl = linkCtrl | PCIE_LINK_DISABLE;
    if (EFI_ERROR(pciWriteWord(rbIo,
            bridgeAddr + pcieCap + PCIE_LINK_CONTROL_OFFSET, &newLinkCtrl)))
        return;

    // Save the info for restoration
    gHiddenDevices[gHiddenDeviceCount].DevBus        = bus;
    gHiddenDevices[gHiddenDeviceCount].DevDev        = dev;
    gHiddenDevices[gHiddenDeviceCount].DevFunc       = func;
    gHiddenDevices[gHiddenDeviceCount].BridgeBus     = bridgeBus;
    gHiddenDevices[gHiddenDeviceCount].BridgeDev     = bridgeDev;
    gHiddenDevices[gHiddenDeviceCount].BridgeFunc    = bridgeFunc;
    gHiddenDevices[gHiddenDeviceCount].PcieCapOffset = pcieCap;
    gHiddenDevices[gHiddenDeviceCount].OrigLinkCtrl  = linkCtrl;
    gHiddenDevices[gHiddenDeviceCount].BridgeVid     = (UINT16)(bridgeVidDid & 0xFFFF);
    gHiddenDevices[gHiddenDeviceCount].BridgeDid     = (UINT16)(bridgeVidDid >> 16);
    gHiddenDevices[gHiddenDeviceCount].RootBridgeIo  = rbIo;
    gHiddenDeviceCount++;

    DEBUG((DEBUG_INFO,
        "ReBarDXE: === LINK DISABLED === device %02x:%02x.%x via bridge "
        "%02x:%02x.%x (PCIe cap=0x%x, LinkCtrl 0x%04x->0x%04x)\n",
        bus, dev, func, bridgeBus, bridgeDev, bridgeFunc,
        pcieCap, linkCtrl, newLinkCtrl));
}

// =====================================================================
// BRIDGE ENUMERATION: ASSIGN TEMPORARY BUSES
// =====================================================================
// Before PciBus runs, the PCIe bridges on bus 0 have no secondary bus
// assigned (AGESA PEI does not do PCIe enumeration on desktop 990FX).
// Without bus numbers the devices behind the bridges are unreachable.
// We assign temporary buses so we can scan the devices behind them.
// PciBus will overwrite them during its own enumeration.

typedef struct {
    UINT8 Dev;
    UINT8 Func;
    UINT8 OrigSecBus;
    UINT8 OrigSubBus;
    UINT8 TempSecBus;
} TEMP_BUS_ENTRY;

#define MAX_TEMP_BRIDGES 32

static UINT16 EnumerateBridgesAndAssignBuses(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo,
    TEMP_BUS_ENTRY* entries,
    UINTN* entryCount)
{
    UINT8  nextBus = 1;
    UINT8  dev;
    *entryCount = 0;

    for (dev = 0; dev < 32; dev++) {
        UINT8 maxFunc = 1, func;
        for (func = 0; func < maxFunc; func++) {
            UINT64 addr = EFI_PCI_ADDRESS(0, dev, func, 0);
            UINT32 vidDid;
            UINT8  headerType, secBus, subBus;

            if (EFI_ERROR(pciReadDword(rbIo, addr, &vidDid)))
                continue;
            if ((vidDid & 0xFFFF) == 0xFFFF || (vidDid & 0xFFFF) == 0)
                continue;

            if (func == 0) {
                if (!EFI_ERROR(pciReadByte(rbIo, addr + 0x0E, &headerType))) {
                    if (headerType & 0x80) maxFunc = 8;
                }
            } else {
                pciReadByte(rbIo, addr + 0x0E, &headerType);
            }

            // Only PCI-PCI bridges (header type 1)
            if ((headerType & 0x7F) != 1)
                continue;

            pciReadByte(rbIo, addr + 0x19, &secBus);
            pciReadByte(rbIo, addr + 0x1A, &subBus);

            if (secBus != 0) {
                // Bridge already configured (by AGESA/firmware)
                if (secBus >= nextBus)
                    nextBus = secBus + 1;
                DEBUG((DEBUG_INFO,
                    "ReBarDXE: Bridge 00:%02x.%x already configured sec=%d sub=%d\n",
                    dev, func, secBus, subBus));
                continue;
            }

            // Bridge without secondary bus — assign a temporary one
            if (*entryCount < MAX_TEMP_BRIDGES) {
                UINT8 tempSec = nextBus;
                UINT8 tempSub = nextBus;
                UINT8 priZero = 0;

                entries[*entryCount].Dev        = dev;
                entries[*entryCount].Func       = func;
                entries[*entryCount].OrigSecBus = 0;
                entries[*entryCount].OrigSubBus = subBus;
                entries[*entryCount].TempSecBus = tempSec;
                (*entryCount)++;

                // Program the bridge with a temporary bus
                pciWriteByte(rbIo, addr + 0x18, &priZero);   // primary = 0
                pciWriteByte(rbIo, addr + 0x19, &tempSec);   // secondary
                pciWriteByte(rbIo, addr + 0x1A, &tempSub);   // subordinate

                DEBUG((DEBUG_INFO,
                    "ReBarDXE: Bridge 00:%02x.%x -> temp bus %d "
                    "(VID:DID=%04x:%04x)\n",
                    dev, func, tempSec,
                    (UINT16)(vidDid & 0xFFFF), (UINT16)(vidDid >> 16)));

                nextBus++;
            }
        }
    }

    return (UINT16)nextBus;
}

// Restore the original buses (0) on the bridges — PciBus will reprogram them
static VOID RestoreTempBusNumbers(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo,
    TEMP_BUS_ENTRY* entries,
    UINTN entryCount)
{
    UINTN i;
    for (i = 0; i < entryCount; i++) {
        UINT64 addr = EFI_PCI_ADDRESS(0, entries[i].Dev, entries[i].Func, 0);
        UINT8 zero = 0;
        pciWriteByte(rbIo, addr + 0x19, &zero);
        pciWriteByte(rbIo, addr + 0x1A, &entries[i].OrigSubBus);
    }
}

static VOID PreScanAndHideDevices(VOID)
{
    EFI_STATUS  status;
    UINTN       handleCount;
    EFI_HANDLE* handleBuffer;
    UINTN       h;

    status = gBS->LocateHandleBuffer(
        ByProtocol,
        &gEfiPciRootBridgeIoProtocolGuid,
        NULL,
        &handleCount,
        &handleBuffer);

    if (EFI_ERROR(status))
        return;

    DEBUG((DEBUG_INFO, "ReBarDXE: Pre-scan started, %d root bridges found\n",
        (UINT32)handleCount));

    for (h = 0; h < handleCount; h++) {
        EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo;
        UINT16 bus, maxBus;
        TEMP_BUS_ENTRY tempBridges[MAX_TEMP_BRIDGES];
        UINTN tempBridgeCount = 0;

        status = gBS->HandleProtocol(
            handleBuffer[h],
            &gEfiPciRootBridgeIoProtocolGuid,
            (VOID**)&rbIo);

        if (EFI_ERROR(status))
            continue;

        // PHASE 1: Enumerate bridges on bus 0, assign temporary buses
        // BEFORE the scan, so devices behind the bridges are reachable
        maxBus = EnumerateBridgesAndAssignBuses(rbIo, tempBridges, &tempBridgeCount);

        DEBUG((DEBUG_INFO,
            "ReBarDXE: Enumerated %d bridges, scanning bus 0-%d\n",
            (UINT32)tempBridgeCount, maxBus - 1));

        // PHASE 2: Scan the assigned buses for devices with BAR >4GB
        for (bus = 0; bus < maxBus; bus++) {
            UINT8 dev;
            for (dev = 0; dev < 32; dev++) {
                UINT8  maxFunc = 1;
                UINT8  func;

                for (func = 0; func < maxFunc; func++) {
                    UINT64 addr = EFI_PCI_ADDRESS(bus, dev, func, 0);
                    UINT32 vidDid;
                    UINT16 vid;
                    UINT8  headerType;

                    if (EFI_ERROR(pciReadDword(rbIo, addr, &vidDid)))
                        continue;

                    vid = (UINT16)(vidDid & 0xFFFF);
                    if (vid == 0xFFFF || vid == 0x0000)
                        continue;

                    // Check multi-function at func 0
                    if (func == 0) {
                        if (!EFI_ERROR(pciReadByte(rbIo, addr + 0x0E, &headerType))) {
                            if (headerType & 0x80)
                                maxFunc = 8;
                        }
                    }

                    DEBUG((DEBUG_INFO,
                        "ReBarDXE: Pre-scan found %02x:%02x.%x VID=%04x DID=%04x\n",
                        (UINT8)bus, dev, func, vid, (UINT16)(vidDid >> 16)));

                    // Display protection: do NOT hide the Intel GPU (Arc A770)
                    // The Arc is needed for video output during POST and in Linux.
                    // If we hide it, the BIOS has no VGA -> halt at 0d.
                    // NVIDIA (P100) and AMD (FirePro) GPUs are hidden.
                    if (vid == 0x8086) {
                        DEBUG((DEBUG_INFO,
                            "ReBarDXE: Intel GPU %02x:%02x.%x PROTECTED "
                            "(display card, not hidden)\n",
                            (UINT8)bus, dev, func));
                        continue;
                    }

                    // Check if it has a BAR > 4GB
                    if (DeviceHasLargeBarEx(rbIo, addr)) {
                        HideDeviceViaLinkDisable(rbIo, (UINT8)bus, dev, func);
                    }
                }
            }
        }

        // PHASE 3: Restore original buses on the bridges
        // Bridges with hidden devices (link disabled) keep the link
        // disabled regardless of bus number. PciBus will reprogram them.
        RestoreTempBusNumbers(rbIo, tempBridges, tempBridgeCount);
    }

    FreePool(handleBuffer);

    DEBUG((DEBUG_INFO, "ReBarDXE: Pre-scan complete, %d devices hidden\n",
        (UINT32)gHiddenDeviceCount));
}


// =====================================================================
// BAR AND BRIDGE PROGRAMMING POST LINK-ENABLE
// =====================================================================
// After restoring the PCIe link, the device shows up with BARs at zero
// (never programmed by the BIOS because it was hidden). If Linux sees
// a BAR at 0x00000000 with size 16GB, it creates a PNP conflict with
// system resources (RAM, ECAM, APIC). The PNP resources get disabled
// and the NVIDIA driver fails with rm_init_adapter.
//
// This function programs the BARs at valid addresses above 4GB and
// configures the memory windows of the upstream bridge. Linux pci=realloc
// can then move everything wherever it wants, but without PNP conflicts.

// Address layout for multi-GPU (above 4GB, inside the 256GB NB Window):
//   0x410000000 -  0x4FFFFFFFF : non-prefetchable (BAR0 regs, ~4GB)
//   0x800000000 -  0xBFFFFFFFF : P100 #1 BAR1 16GB pref
//   0xC00000000 -  0xFFFFFFFFF : P100 #2 BAR1 16GB pref
//   0x1000000000 - 0x13FFFFFFFF : Arc A770 BAR 16GB pref
//   0x1400000000 - 0x1BFFFFFFFF : FirePro BAR 32GB pref
//   (the static cursors allocate sequentially, aligned to size)
// Start addresses defined as globals (gNextPrefAddr, gNextMmioAddr)

static VOID ProgramHiddenDeviceBars(HIDDEN_DEVICE_ENTRY* entry)
{
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo = entry->RootBridgeIo;
    UINT64 bridgeAddr = EFI_PCI_ADDRESS(entry->BridgeBus, entry->BridgeDev,
                                         entry->BridgeFunc, 0);
    UINT8  secBus = 0;
    UINT64 devAddr;
    UINT32 vidDid = 0xFFFFFFFF;
    UINTN  retries;

    // Use global cursors (shared with ResizeIntelGpuBars)

    UINT64 prefLow = 0, prefHigh = 0;
    UINT64 mmioLow = 0, mmioHigh = 0;
    BOOLEAN hasPref = FALSE, hasMmio = FALSE;

    // Read the current secondary bus (assigned by PciBus during enumeration)
    if (EFI_ERROR(pciReadByte(rbIo, bridgeAddr + 0x19, &secBus)) || secBus == 0) {
        DEBUG((DEBUG_WARN,
            "ReBarDXE: Bridge %02x:%02x.%x sec bus=%d, BAR programming skipped\n",
            entry->BridgeBus, entry->BridgeDev, entry->BridgeFunc, secBus));
        return;
    }

    devAddr = EFI_PCI_ADDRESS(secBus, entry->DevDev, entry->DevFunc, 0);

    // Wait for link training — poll VID with a 500ms timeout
    for (retries = 0; retries < 500; retries++) {
        gBS->Stall(1000);  // 1ms
        if (!EFI_ERROR(pciReadDword(rbIo, devAddr, &vidDid))) {
            if ((vidDid & 0xFFFF) != 0xFFFF && (vidDid & 0xFFFF) != 0)
                break;
        }
    }

    if ((vidDid & 0xFFFF) == 0xFFFF || (vidDid & 0xFFFF) == 0) {
        IoWrite8(POST_PORT, POST_ERROR_LINK_TRAIN);
        DEBUG((DEBUG_WARN,
            "ReBarDXE: Device %02x:%02x.%x not responding after %d ms! "
            "[POST DF]\n",
            secBus, entry->DevDev, entry->DevFunc, (UINT32)retries));
        return;
    }

    IoWrite8(POST_PORT, POST_EXITBS_BAR_PROG);
    DEBUG((DEBUG_INFO,
        "ReBarDXE: Device %02x:%02x.%x online after %d ms "
        "(VID:DID=%04x:%04x)\n",
        secBus, entry->DevDev, entry->DevFunc, (UINT32)retries,
        (UINT16)(vidDid & 0xFFFF), (UINT16)(vidDid >> 16)));

    // Scan and program every BAR
    {
        UINT8 barIdx;
        UINT32 allOnes = 0xFFFFFFFF;

        for (barIdx = 0; barIdx < 6; ) {
            UINT32 barOff = 0x10 + barIdx * 4;
            UINT32 origLo, sizeLo, origHi = 0, sizeHi = 0;
            BOOLEAN is64, isPref;
            UINT64 sizeMask, barSize, assignAddr;
            UINT32 newLo, newHi;

            if (EFI_ERROR(pciReadDword(rbIo, devAddr + barOff, &origLo))) {
                barIdx++;
                continue;
            }

            // Skip I/O BAR
            if (origLo & 1) { barIdx++; continue; }

            is64 = ((origLo & 0x06) == 0x04);
            isPref = ((origLo & 0x08) != 0);

            // Size probe — low DWORD
            pciWriteDword(rbIo, devAddr + barOff, &allOnes);
            pciReadDword(rbIo, devAddr + barOff, &sizeLo);
            pciWriteDword(rbIo, devAddr + barOff, &origLo);
            sizeLo &= 0xFFFFFFF0;

            if (is64) {
                pciReadDword(rbIo, devAddr + barOff + 4, &origHi);
                pciWriteDword(rbIo, devAddr + barOff + 4, &allOnes);
                pciReadDword(rbIo, devAddr + barOff + 4, &sizeHi);
                pciWriteDword(rbIo, devAddr + barOff + 4, &origHi);
            }

            sizeMask = is64 ? (((UINT64)sizeHi << 32) | sizeLo)
                            : ((UINT64)sizeLo);
            if (sizeMask == 0) {
                barIdx += is64 ? 2 : 1;
                continue;
            }
            barSize = (~sizeMask + 1) & sizeMask;
            if (barSize == 0) {
                barIdx += is64 ? 2 : 1;
                continue;
            }

            // Assign an address above 4GB, aligned to size
            if (isPref && is64) {
                gNextPrefAddr = (gNextPrefAddr + barSize - 1) & ~(barSize - 1);
                assignAddr = gNextPrefAddr;
                gNextPrefAddr += barSize;
                if (!hasPref) { prefLow = assignAddr; hasPref = TRUE; }
                prefHigh = assignAddr + barSize - 1;
            } else {
                gNextMmioAddr = (gNextMmioAddr + barSize - 1) & ~(barSize - 1);
                assignAddr = gNextMmioAddr;
                gNextMmioAddr += barSize;
                if (!hasMmio) { mmioLow = assignAddr; hasMmio = TRUE; }
                mmioHigh = assignAddr + barSize - 1;
            }

            // Write BAR with the assigned address
            newLo = (UINT32)(assignAddr & 0xFFFFFFF0) | (origLo & 0x0F);
            pciWriteDword(rbIo, devAddr + barOff, &newLo);
            if (is64) {
                newHi = (UINT32)(assignAddr >> 32);
                pciWriteDword(rbIo, devAddr + barOff + 4, &newHi);
            }

            DEBUG((DEBUG_INFO,
                "ReBarDXE: ExitBS BAR%d -> 0x%lx (size 0x%lx)\n",
                barIdx, assignAddr, barSize));

            barIdx += is64 ? 2 : 1;
        }
    }

    // Program the bridge prefetchable window (64-bit)
    if (hasPref) {
        UINT16 pBase  = (UINT16)((prefLow >> 16) & 0xFFF0) | 0x0001;
        UINT16 pLimit = (UINT16)((prefHigh >> 16) & 0xFFF0) | 0x0001;
        UINT32 pBaseUp  = (UINT32)(prefLow >> 32);
        UINT32 pLimitUp = (UINT32)(prefHigh >> 32);

        pciWriteWord(rbIo, bridgeAddr + 0x24, &pBase);
        pciWriteWord(rbIo, bridgeAddr + 0x26, &pLimit);
        pciWriteDword(rbIo, bridgeAddr + 0x28, &pBaseUp);
        pciWriteDword(rbIo, bridgeAddr + 0x2C, &pLimitUp);

        DEBUG((DEBUG_INFO,
            "ReBarDXE: Bridge %02x:%02x.%x pref window: 0x%lx - 0x%lx\n",
            entry->BridgeBus, entry->BridgeDev, entry->BridgeFunc,
            prefLow, prefHigh));
    }

    // Program the bridge non-prefetchable window (if used)
    if (hasMmio) {
        UINT16 mBase  = (UINT16)((mmioLow >> 16) & 0xFFF0);
        UINT16 mLimit = (UINT16)((mmioHigh >> 16) & 0xFFF0);

        pciWriteWord(rbIo, bridgeAddr + 0x20, &mBase);
        pciWriteWord(rbIo, bridgeAddr + 0x22, &mLimit);

        DEBUG((DEBUG_INFO,
            "ReBarDXE: Bridge %02x:%02x.%x MMIO window: 0x%lx - 0x%lx\n",
            entry->BridgeBus, entry->BridgeDev, entry->BridgeFunc,
            mmioLow, mmioHigh));
    }

    // Enable Memory Space + Bus Master on the device
    {
        UINT16 cmd = 0;
        pciReadWord(rbIo, devAddr + 0x04, &cmd);
        cmd |= 0x06;  // bit1=MemSpace, bit2=BusMaster
        pciWriteWord(rbIo, devAddr + 0x04, &cmd);
    }

    // Ensure Bus Master + Memory on the bridge
    {
        UINT16 cmd = 0;
        pciReadWord(rbIo, bridgeAddr + 0x04, &cmd);
        cmd |= 0x07;  // bit0=I/O, bit1=Mem, bit2=BusMaster
        pciWriteWord(rbIo, bridgeAddr + 0x04, &cmd);
    }

    DEBUG((DEBUG_INFO,
        "ReBarDXE: Device %02x:%02x.%x BAR programming complete\n",
        secBus, entry->DevDev, entry->DevFunc));
}


// =====================================================================
// RE-ENABLE PCIe LINK ON HIDDEN DEVICES
// =====================================================================
// Restores the original Link Control value on the upstream bridge,
// bringing the PCIe link back up. After link training, it programs the
// BARs and bridge windows to avoid PNP conflicts on Linux.

static VOID ReEnableAllHiddenDevices(VOID)
{
    UINTN i;

    for (i = 0; i < gHiddenDeviceCount; i++) {
        EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo = gHiddenDevices[i].RootBridgeIo;
        UINT64 bridgeAddr = EFI_PCI_ADDRESS(
            gHiddenDevices[i].BridgeBus,
            gHiddenDevices[i].BridgeDev,
            gHiddenDevices[i].BridgeFunc, 0);
        UINT16 linkCtrl = gHiddenDevices[i].OrigLinkCtrl;

        // 990FX bridge quirk: make sure the upstream bridge supports
        // 64-bit prefetchable BEFORE re-enabling the link.
        // PciBus may have overwritten the bit during enumeration.
        // Linux needs to see 64-bit prefetchable to allocate above 4GB.
        if (gHiddenDevices[i].BridgeVid == 0x1002 &&
            (gHiddenDevices[i].BridgeDid == 0x5a16 ||
             gHiddenDevices[i].BridgeDid == 0x5a1c ||
             gHiddenDevices[i].BridgeDid == 0x43A3)) {   // v7: SB900 root port (FirePro W9100 riser)
            UINT16 pBase = 0, pLimit = 0;
            UINT32 pUpperZero = 0;

            pciReadWord(rbIo, bridgeAddr + 0x24, &pBase);
            pciReadWord(rbIo, bridgeAddr + 0x26, &pLimit);
            pBase  = (pBase  & 0xFFF0) | 0x0001;  // bit0=1 -> 64-bit
            pLimit = (pLimit & 0xFFF0) | 0x0001;
            pciWriteWord(rbIo, bridgeAddr + 0x24, &pBase);
            pciWriteWord(rbIo, bridgeAddr + 0x26, &pLimit);
            pciWriteDword(rbIo, bridgeAddr + 0x28, &pUpperZero);
            pciWriteDword(rbIo, bridgeAddr + 0x2C, &pUpperZero);

            DEBUG((DEBUG_INFO,
                "ReBarDXE: 990FX bridge quirk %02x:%02x.%x reapplied "
                "(prefetchable 64-bit)\n",
                gHiddenDevices[i].BridgeBus, gHiddenDevices[i].BridgeDev,
                gHiddenDevices[i].BridgeFunc));
        }

        // Restore original Link Control (without Link Disable bit)
        pciWriteWord(rbIo,
            bridgeAddr + gHiddenDevices[i].PcieCapOffset + PCIE_LINK_CONTROL_OFFSET,
            &linkCtrl);

        DEBUG((DEBUG_INFO,
            "ReBarDXE: === LINK RE-ENABLED === device %02x:%02x.%x "
            "via bridge %02x:%02x.%x (LinkCtrl -> 0x%04x)\n",
            gHiddenDevices[i].DevBus, gHiddenDevices[i].DevDev,
            gHiddenDevices[i].DevFunc,
            gHiddenDevices[i].BridgeBus, gHiddenDevices[i].BridgeDev,
            gHiddenDevices[i].BridgeFunc, linkCtrl));

        // Wait for link training and program BARs/bridge to avoid
        // Linux seeing BARs at 0x00000000 (fatal PNP conflict)
        ProgramHiddenDeviceBars(&gHiddenDevices[i]);
    }
}

// =====================================================================
// ECAM ACCESS — EXTENDED CONFIG SPACE (OFFSET > 0xFF)
// =====================================================================
// The PCI Root Bridge IO protocol only supports offset 0-255 (standard
// config space). PCIe Extended Capabilities (like Resizable BAR) live
// at offset 0x100+. We access them via ECAM (memory-mapped config space).

static EFI_STATUS pciReadDwordEcam(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo,
    UINT8 bus, UINT8 dev, UINT8 func, UINT16 reg,
    UINT32* value)
{
    UINT64 addr = ECAM_BASE | ((UINT64)bus << 20) | ((UINT64)dev << 15) |
                  ((UINT64)func << 12) | (UINT64)reg;
    return rbIo->Mem.Read(rbIo, EfiPciWidthUint32, addr, 1, value);
}

static EFI_STATUS pciWriteDwordEcam(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo,
    UINT8 bus, UINT8 dev, UINT8 func, UINT16 reg,
    UINT32* value)
{
    UINT64 addr = ECAM_BASE | ((UINT64)bus << 20) | ((UINT64)dev << 15) |
                  ((UINT64)func << 12) | (UINT64)reg;
    return rbIo->Mem.Write(rbIo, EfiPciWidthUint32, addr, 1, value);
}

// Find a PCIe Extended Capability by ID (ECAM version, for offset > 0xFF)
static UINT16 pciFindExtCapEcam(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo,
    UINT8 bus, UINT8 dev, UINT8 func, UINT16 capId)
{
    UINT16 offset = 0x100;  // Extended cap list starts at 0x100
    UINT32 header;
    UINTN  ttl = (0x1000 - 0x100) / 8;  // max cap entries in extended space

    while (offset != 0 && offset < 0x1000 && ttl-- > 0) {
        if (EFI_ERROR(pciReadDwordEcam(rbIo, bus, dev, func, offset, &header)))
            return 0;
        if (header == 0 || header == 0xFFFFFFFF)
            return 0;
        if ((header & 0xFFFF) == capId)
            return offset;
        offset = (UINT16)((header >> 20) & 0xFFC);  // Next cap pointer
    }
    return 0;
}


// =====================================================================
// REBAR: RESIZE BARS ON INTEL GPU (ARC A770) AT EXITBOOTSERVICES
// =====================================================================
// The Arc A770 is not hidden (Intel VID filter) — the BIOS allocates it
// with a 256MB BAR below 4GB (display works during POST). At ExitBS
// we use the PCIe Resizable BAR capability to expand the BAR to 16GB
// and relocate it above 4GB. Linux will see the BAR at full size.
//
// v9.5 TOPOLOGY (post-swap Arc -> riser, 2026-04-21):
//   00:15.3 (SB900 SR5690 root port, DID 0x43A3, **function 3**)
//     23:00.0 (Intel PCIe switch upstream 4fa0)
//       24:01.0 (Intel PCIe switch downstream 4fa4)
//         25:00.0 (Arc A770, VID 8086:56a0)       <- ReBAR endpoint @ 0x420
//
// v9.4 BUG: the outer root-port loop only scanned **function 0** of
// every dev (EFI_PCI_ADDRESS(0, rootDev, 0, 0)), so 00:15.3 (func 3)
// was never visited. Arc on the riser remained invisible to ReBAR,
// 256MB BAR. Same bug already fixed in v7 for Apply990FxBridgeQuirkAll
// and in v9.4 for ResizeAmdGpuBars.
//
// v9.5 FIX: outer loop now scans dev 0..31 x func 0..7, with standard
// multi-function early-exit (header bit 0x80).
//
// The bridge chain (root port + intermediate switches) is reprogrammed
// with 64-bit prefetchable windows (pref base/limit bit0=1 + upper32).

static VOID ResizeIntelGpuBars(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo)
{
    UINT8 rootDev, rootFunc;
    BOOLEAN gpuProcessed = FALSE;

    DEBUG((DEBUG_INFO, "ReBarDXE: === Searching Intel GPU for ReBAR v9.5 ===\n"));
    L_STR("[DC] ResizeIntelGpuBars v9.5 enter\n");

    // v9.5: scan dev 0..31 x func 0..7 — 00:15.3 (SB900 riser) is func 3.
    for (rootDev = 0; rootDev < 32 && !gpuProcessed; rootDev++) {
    for (rootFunc = 0; rootFunc < 8 && !gpuProcessed; rootFunc++) {
        UINT64 rpAddr = EFI_PCI_ADDRESS(0, rootDev, rootFunc, 0);
        UINT32 rpVidDid;
        UINT8  rpHdr, rpSecBus, rpSubBus;
        UINT8  bus;

        if (EFI_ERROR(pciReadDword(rbIo, rpAddr, &rpVidDid)))
            break;
        if ((rpVidDid & 0xFFFF) == 0xFFFF) {
            if (rootFunc == 0) break;  // device absent, skip whole dev
            continue;
        }
        if (EFI_ERROR(pciReadByte(rbIo, rpAddr + 0x0E, &rpHdr)))
            continue;
        if ((rpHdr & 0x7F) != 1) {
            // Non-bridge: if func0 is not multi-function, skip dev
            if (rootFunc == 0 && (rpHdr & 0x80) == 0) break;
            continue;
        }
        pciReadByte(rbIo, rpAddr + 0x19, &rpSecBus);
        pciReadByte(rbIo, rpAddr + 0x1A, &rpSubBus);
        if (rpSecBus == 0)
            continue;

        // Scan ALL buses under this root port (sec..sub)
        // looking for an Intel display endpoint with ReBAR
        for (bus = rpSecBus; bus <= rpSubBus && !gpuProcessed; bus++) {
            UINT8 d;
            for (d = 0; d < 32 && !gpuProcessed; d++) {
                UINT64 devAddr = EFI_PCI_ADDRESS(bus, d, 0, 0);
                UINT32 dVidDid;
                UINT8  dHdr, cls;
                UINT16 rebarOff;
                UINT32 rebarCtrl0;
                UINT8  numBars, i;
                UINT64 prefStart = 0;
                UINT64 prefEnd = 0;

                if (EFI_ERROR(pciReadDword(rbIo, devAddr, &dVidDid)))
                    continue;
                if ((dVidDid & 0xFFFF) == 0xFFFF)
                    continue;
                if ((dVidDid & 0xFFFF) != 0x8086)
                    continue;  // Intel only

                pciReadByte(rbIo, devAddr + 0x0E, &dHdr);
                if ((dHdr & 0x7F) != 0)
                    continue;  // Endpoint only (type 0), skip bridge/switch

                pciReadByte(rbIo, devAddr + 0x0B, &cls);
                if (cls != 0x03)
                    continue;  // Display controller only (class 03)

                // Intel GPU endpoint found!
                rebarOff = pciFindExtCapEcam(rbIo, bus, d, 0,
                    PCI_EXT_CAP_ID_REBAR);
                if (rebarOff == 0) {
                    DEBUG((DEBUG_WARN,
                        "ReBarDXE: Intel GPU %02x:%02x.0 without ReBAR\n",
                        bus, d));
                    continue;
                }

                DEBUG((DEBUG_INFO,
                    "ReBarDXE: Intel GPU %02x:%02x.0 (DID=%04x) "
                    "ReBAR @ 0x%x — chain from root port 00:%02x.%x\n",
                    bus, d, (UINT16)(dVidDid >> 16), rebarOff, rootDev, rootFunc));

                // Number of resizable BARs (bits [7:5] of the first ctrl)
                if (EFI_ERROR(pciReadDwordEcam(rbIo, bus, d, 0,
                        rebarOff + 0x08, &rebarCtrl0)))
                    continue;
                numBars = (UINT8)((rebarCtrl0 >> 5) & 0x07);
                if (numBars == 0)
                    numBars = 1;

                DEBUG((DEBUG_INFO,
                    "ReBarDXE: %d resizable BARs\n", numBars));

                // 1. Disable Memory Space on the device
                {
                    UINT16 cmd = 0;
                    UINT16 cmdOff;
                    pciReadWord(rbIo, devAddr + 0x04, &cmd);
                    cmdOff = cmd & 0xFFF9;  // Clear MemSpace(1) + BusMaster(2)
                    pciWriteWord(rbIo, devAddr + 0x04, &cmdOff);
                }

                // 2. For each resizable BAR: find max, resize, assign
                for (i = 0; i < numBars; i++) {
                    UINT32 cap, ctrl;
                    UINT16 capOff = rebarOff + 0x04 + (i * 8);
                    UINT16 ctrlOff = rebarOff + 0x08 + (i * 8);
                    UINT8  barIndex, maxSizeIdx, s;
                    UINT32 sizes;
                    UINT64 newSize, assignAddr;
                    UINT32 barOff, origLo, origHi, newLo, newHi;

                    if (EFI_ERROR(pciReadDwordEcam(rbIo, bus, d, 0,
                            capOff, &cap)))
                        continue;
                    if (EFI_ERROR(pciReadDwordEcam(rbIo, bus, d, 0,
                            ctrlOff, &ctrl)))
                        continue;

                    barIndex = (UINT8)(ctrl & 0x07);

                    // Find the maximum supported size
                    // cap bits[31:4]: bit N -> size 2^(N+20)
                    // N=8 -> 256MB, N=14 -> 16GB
                    sizes = (cap >> 4) & 0x0FFFFFFF;
                    maxSizeIdx = 0;
                    for (s = 27; s > 0; s--) {
                        if (sizes & (1U << s)) {
                            maxSizeIdx = s;
                            break;
                        }
                    }
                    if (maxSizeIdx == 0) {
                        if (sizes & 1)
                            maxSizeIdx = 0;
                        else
                            continue;
                    }

                    newSize = 1ULL << (maxSizeIdx + 20);

                    DEBUG((DEBUG_INFO,
                        "ReBarDXE: BAR%d: sizes=0x%x, max=%d MB\n",
                        barIndex, sizes, (UINT32)(newSize >> 20)));

                    // Write new size: ctrl bits[13:8] = size index
                    ctrl = (ctrl & ~0x3F00U) | ((UINT32)maxSizeIdx << 8);
                    pciWriteDwordEcam(rbIo, bus, d, 0, ctrlOff, &ctrl);

                    // Program BAR with an address above 4GB
                    barOff = 0x10 + barIndex * 4;
                    pciReadDword(rbIo, devAddr + barOff, &origLo);

                    if ((origLo & 0x06) != 0x04) {
                        DEBUG((DEBUG_WARN,
                            "ReBarDXE: BAR%d is 32-bit, skip\n", barIndex));
                        continue;
                    }

                    pciReadDword(rbIo, devAddr + barOff + 4, &origHi);

                    // v9.3: explicit placement. The Arc VRAM is BAR2
                    // (not BAR0: BAR0 is a 16MB MMIO register). We only
                    // move BAR2 up to ARC_BAR2_TARGET (16GB). Other BARs
                    // keep the BIOS assignment.
                    // v9.3 NOTE: this fixes v9.2 BUG-C (the filter
                    // barIndex != 0 skipped the real VRAM).
                    if (barIndex != 2) {
                        DEBUG((DEBUG_INFO,
                            "ReBarDXE: Arc BAR%d skip resize "
                            "(v9.3: only BAR2 handled explicitly)\n",
                            barIndex));
                        continue;
                    }
                    if ((origLo & 0x08) == 0) {
                        DEBUG((DEBUG_WARN,
                            "ReBarDXE: Arc BAR2 is not pref — skip\n"));
                        continue;
                    }
                    assignAddr = ARC_BAR2_TARGET;   // hard-coded v9.3

                    newLo = (UINT32)(assignAddr & 0xFFFFFFF0)
                        | (origLo & 0x0F);
                    newHi = (UINT32)(assignAddr >> 32);
                    pciWriteDword(rbIo, devAddr + barOff, &newLo);
                    pciWriteDword(rbIo, devAddr + barOff + 4, &newHi);

                    DEBUG((DEBUG_INFO,
                        "ReBarDXE: BAR%d resized: %d MB @ 0x%lx\n",
                        barIndex, (UINT32)(newSize >> 20), assignAddr));

                    // Track prefetchable range for the bridge
                    if (origLo & 0x08) {
                        if (prefStart == 0 || assignAddr < prefStart)
                            prefStart = assignAddr;
                        if (assignAddr + newSize > prefEnd)
                            prefEnd = assignAddr + newSize;
                    }
                }

                // 3. Re-enable Memory Space + Bus Master on the device
                {
                    UINT16 cmd = 0;
                    pciReadWord(rbIo, devAddr + 0x04, &cmd);
                    cmd |= 0x0006;
                    pciWriteWord(rbIo, devAddr + 0x04, &cmd);
                }

                // ====================================================
                // 4. REPROGRAM THE ENTIRE BRIDGE CHAIN
                // ====================================================
                // Every bridge from the root port to the GPU must have:
                //   - Prefetchable base/limit of 64-bit type (bit 0 = 1)
                //   - Range covering all assigned prefetchable BARs
                //   - Bus Master + Memory Space enabled
                //
                // Criteria to identify bridges in the path:
                //   secondary_bus <= gpuBus <= subordinate_bus
                if (prefStart != 0 && prefEnd > prefStart) {
                    UINT16 pBase;
                    UINT16 pLimit;
                    UINT32 pBaseUp;
                    UINT32 pLimitUp;
                    UINT8  bBus, bDev, bFunc;

                    pBase = (UINT16)((prefStart >> 16) & 0xFFF0) | 0x0001;
                    pLimit = (UINT16)(((prefEnd - 1) >> 16) & 0xFFF0)
                        | 0x0001;
                    pBaseUp = (UINT32)(prefStart >> 32);
                    pLimitUp = (UINT32)((prefEnd - 1) >> 32);

                    // 4a. Root port (bus 0)
                    pciWriteWord(rbIo, rpAddr + 0x24, &pBase);
                    pciWriteWord(rbIo, rpAddr + 0x26, &pLimit);
                    pciWriteDword(rbIo, rpAddr + 0x28, &pBaseUp);
                    pciWriteDword(rbIo, rpAddr + 0x2C, &pLimitUp);
                    {
                        UINT16 cmd = 0;
                        pciReadWord(rbIo, rpAddr + 0x04, &cmd);
                        cmd |= 0x0007;
                        pciWriteWord(rbIo, rpAddr + 0x04, &cmd);
                    }
                    DEBUG((DEBUG_INFO,
                        "ReBarDXE: Root 00:%02x.%x pref -> "
                        "0x%lx - 0x%lx (64-bit)\n",
                        rootDev, rootFunc, prefStart, prefEnd - 1));

                    // 4b. Intermediate bridges (bus rpSecBus .. gpuBus-1)
                    // Scan every bus in the interval for bridges
                    // whose sec..sub range contains the GPU's bus
                    for (bBus = rpSecBus; bBus < bus; bBus++) {
                        for (bDev = 0; bDev < 32; bDev++) {
                            for (bFunc = 0; bFunc < 8; bFunc++) {
                                UINT64 bAddr = EFI_PCI_ADDRESS(
                                    bBus, bDev, bFunc, 0);
                                UINT32 bVid;
                                UINT8  bHdr, bSec, bSub;

                                if (EFI_ERROR(pciReadDword(
                                        rbIo, bAddr, &bVid)))
                                    continue;
                                if ((bVid & 0xFFFF) == 0xFFFF)
                                    continue;
                                pciReadByte(rbIo, bAddr + 0x0E, &bHdr);
                                if ((bHdr & 0x7F) != 1)
                                    continue;

                                pciReadByte(rbIo, bAddr + 0x19, &bSec);
                                pciReadByte(rbIo, bAddr + 0x1A, &bSub);

                                // Only bridges on the path to the GPU
                                if (bus < bSec || bus > bSub)
                                    continue;

                                // 64-bit prefetchable window
                                pciWriteWord(rbIo, bAddr + 0x24, &pBase);
                                pciWriteWord(rbIo, bAddr + 0x26, &pLimit);
                                pciWriteDword(rbIo, bAddr + 0x28, &pBaseUp);
                                pciWriteDword(rbIo, bAddr + 0x2C, &pLimitUp);

                                // Enable IO + Mem + BusMaster
                                {
                                    UINT16 cmd = 0;
                                    pciReadWord(rbIo, bAddr + 0x04, &cmd);
                                    cmd |= 0x0007;
                                    pciWriteWord(rbIo, bAddr + 0x04, &cmd);
                                }

                                DEBUG((DEBUG_INFO,
                                    "ReBarDXE: Bridge %02x:%02x.%x "
                                    "pref -> 0x%lx - 0x%lx (64-bit)\n",
                                    bBus, bDev, bFunc,
                                    prefStart, prefEnd - 1));

                                // Multi-function check
                                if (bFunc == 0 && !(bHdr & 0x80))
                                    break;
                            }
                        }
                    }
                }

                gpuProcessed = TRUE;
                L_STR("[DC] ResizeIntelGpuBars OK v9.5 — Arc BAR2 16GB @ 0x");
                L_HEX(ARC_BAR2_TARGET, 16);
                L_STR(" bridge root 00:");
                L_HEX(rootDev, 2); L_STR(".");
                L_HEX(rootFunc, 1); L_NL();
                DEBUG((DEBUG_INFO,
                    "ReBarDXE: Intel GPU %02x:%02x.0 ReBAR complete "
                    "(root 00:%02x.%x)!\n", bus, d, rootDev, rootFunc));
            }
        }
    }
    }  // close rootFunc

    if (!gpuProcessed) {
        L_STR("[DC] Intel GPU (8086 cls 03) NOT FOUND on any root port/func\n");
    }
}


static VOID Apply990FxBridgeQuirkAll(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo)
{
    // Scan bus 0 (ALL functions, not just 0) to find ALL AMD 1002
    // bridges: SR5690 NB root port (5a16/5a18/5a1c/5a1f) and
    // SB900 SB root port (43A3 — v7, used by the W9100 riser) and enable
    // 64-bit prefetchable. Needed for Linux pci=realloc.
    //
    // v7: the SB900 root port lives at 00:15.3 (function 3), NOT function 0 —
    // the previous loop skipped this bridge.
    UINT8 dev, func;
    for (dev = 0; dev < 32; dev++) {
        for (func = 0; func < 8; func++) {
            UINT64 addr = EFI_PCI_ADDRESS(0, dev, func, 0);
            UINT32 vidDid;
            UINT16 vid, did;
            UINT16 pBase = 0, pLimit = 0;
            UINT32 pUpperZero = 0;
            UINT8  headerType;

            if (EFI_ERROR(pciReadDword(rbIo, addr, &vidDid)))
                break;
            vid = (UINT16)(vidDid & 0xFFFF);
            did = (UINT16)(vidDid >> 16);
            if (vid == 0xFFFF) {
                // No device at this addr: if func==0, skip the whole dev
                if (func == 0) break;
                continue;
            }
            if (vid != 0x1002) {
                // Non-AMD: if func0 is not multi-function, skip the dev
                if (func == 0) {
                    if (EFI_ERROR(pciReadByte(rbIo, addr + 0x0E, &headerType)))
                        break;
                    if ((headerType & 0x80) == 0) break;  // not multi-function
                }
                continue;
            }
            // DID allowlist: SR5690 NB (5a16/5a1c) + SB900 SB (43A3 v7)
            if (did != 0x5a16 && did != 0x5a1c && did != 0x43A3) {
                if (func == 0) {
                    if (EFI_ERROR(pciReadByte(rbIo, addr + 0x0E, &headerType)))
                        break;
                    if ((headerType & 0x80) == 0) break;
                }
                continue;
            }

            // Verify that it is a bridge (header type 1)
            if (EFI_ERROR(pciReadByte(rbIo, addr + 0x0E, &headerType)))
                continue;
            if ((headerType & 0x7F) != 1)
                continue;

            if (did == 0x43A3) {
                // v9: SB900 root port (00:15.3) — we do NOT touch base/limit/upper32 here.
                // The v8 branch (0x14/0x1B -> window 0x1400000000) caused a conflict with
                // Arc at 0x1800000000 (kernel decoded the base as 0x1440000000,
                // dmesg: "address conflict with PCI Bus 0000:14").
                // In v9 the 00:15.3 window is programmed by ResizeAmdGpuBars() AFTER
                // ResizeIntelGpuBars, at gNextPrefAddr=0x1C00000000 (end of Arc).
                // The 0x43A3 entry stays in the whitelist because other parts of the
                // module (ReEnableAllHiddenDevices, reBarSetupDevice) use the same list.
                L_STR("[DB] v9 00:15.3 43a3 detected — window delegated to ResizeAmdGpuBars\n");
            } else {
                // v9.1: for 5a16/5a1c we only mark bit0=1 on Base/Limit
                // ("prefetch 64-bit support" declaration).
                // We no longer zero upper32 (0x28/0x2C) — doing so squashed the
                // pref window below 4G and caused "can't claim" failures for the
                // P100/Arc BARs above 4G without pci=realloc.
                // Preserve what the BIOS/AGESA (or ResizeIntelGpuBars for the Arc)
                // have already written to upper32.
                (VOID)pUpperZero;
                pciReadWord(rbIo, addr + 0x24, &pBase);
                pciReadWord(rbIo, addr + 0x26, &pLimit);
                pBase  = (pBase  & 0xFFF0) | 0x0001;   // bit0=1 -> 64-bit
                pLimit = (pLimit & 0xFFF0) | 0x0001;
                pciWriteWord(rbIo, addr + 0x24, &pBase);
                pciWriteWord(rbIo, addr + 0x26, &pLimit);
                // upper32 intentionally NOT touched (preserve-BIOS)
            }

            L_STR("[DB] Quirk bridge 00:");
            L_HEX(dev, 2); L_STR(".");
            L_HEX(func, 1); L_STR(" DID=");
            L_HEX(did, 4); L_STR(" pref64=OK\n");

            DEBUG((DEBUG_INFO,
                "ReBarDXE: ExitBS quirk AMD bridge 00:%02x.%x (DID 0x%x) "
                "-> prefetchable 64-bit\n", dev, func, did));

            // If func0 is not multi-function, exit after processing it
            if (func == 0 && (headerType & 0x80) == 0) break;
        }
    }
}

// =====================================================================
// v9: DIAGNOSTIC BEEP MELODY via PC SPEAKER (PIT 8253/8254 + port 0x61)
// =====================================================================
// Hardware path:
//   - Port 0x43: PIT Mode/Command register
//   - Port 0x42: PIT Channel 2 data (square-wave generator for speaker)
//   - Port 0x61: System Control. Bit0=Timer2 gate, Bit1=Speaker data.
//     Both set to 1 -> speaker sounds at the frequency programmed on ch2.
// Frequency: f = 1193180 / divisor. UINT16 divisor -> f >= ~18 Hz.
// Timing: gBS->Stall(usec) — precise at ms via TSC, not the PIT itself.
//
// We save/restore port 0x61 to preserve the upper bits (NMI/parity/IOCHK).

#if ENABLE_BEEP_MELODY

static VOID V9BeepOn(UINT16 freqHz)
{
    UINT16 divisor;
    UINT8  gate;

    if (freqHz < 19) freqHz = 19;  // UINT16 div limit
    divisor = (UINT16)(1193180U / (UINT32)freqHz);

    IoWrite8(0x43, 0xB6);                         // ch2, lo/hi byte, mode 3
    IoWrite8(0x42, (UINT8)(divisor & 0xFF));
    IoWrite8(0x42, (UINT8)((divisor >> 8) & 0xFF));
    gate = IoRead8(0x61);
    IoWrite8(0x61, (UINT8)(gate | 0x03));         // timer2 gate + speaker
}

static VOID V9BeepOff(VOID)
{
    UINT8 gate = IoRead8(0x61);
    IoWrite8(0x61, (UINT8)(gate & 0xFC));         // clear bit0+bit1
}

static VOID V9Beep(UINT16 hz, UINTN ms)
{
    V9BeepOn(hz);
    BUSY_WAIT_MS(ms);        // gBS->Stall invalid here (post-ExitBootServices)
    V9BeepOff();
    BUSY_WAIT_MS(80);        // inter-beep gap
}

// Melody "V" (morse "V" = · · · —, here inverted to 2·2—2·):
//   2 short (120 ms) · 2 long (400 ms) · 2 short (120 ms) at 880 Hz
// Total duration ~2.1 s. Called BEFORE POST 0xFC for ear+LED sync.
// Save/restore of port 0x61 to preserve the upper bits (NMI/parity/IOCHK).
static VOID V9PlayFcMelody(VOID)
{
    UINT8 orig61 = IoRead8(0x61);

    V9Beep(880, 120); V9Beep(880, 120);           // .. short
    BUSY_WAIT_MS(150);
    V9Beep(880, 400); V9Beep(880, 400);           // -- long
    BUSY_WAIT_MS(150);
    V9Beep(880, 120); V9Beep(880, 120);           // .. short

    IoWrite8(0x61, orig61);                        // restore upper bits
}

#else
#define V9PlayFcMelody()  do { } while (0)
#endif  // ENABLE_BEEP_MELODY

// =====================================================================
// v9.4: RESIZEAMDGPUBARS — W9170 BAR0 16GB + BAR2 8MB, BUS-AGNOSTIC
// =====================================================================
// Target: FirePro W9170 32GB (Hawaii XT GL, VID:DID = 0x1002:0x67A0).
// ReBAR capability: offset 0x200 (cap ID 0x0015), supports up to 16 GB
// (bit 19 in REBAR_CAP = 0x0007F000 — verified live with setpci).
//
// Change in v9.4 (vs v9.3):
//   v9.3 hardcoded the bridge = 00:15.3 DID 0x43A3 (SB900 = riser).
//   In v9.4 the discovery is bus-agnostic like ResizeIntelGpuBars: it scans
//   all root ports on bus 0 and looks for the 1002:67A0 endpoint underneath.
//   That way, if the user moves the W9170 from the riser to slot 3 (bridge
//   00:0b.0 DID 0x5a1f, SR5690 NB) to work around the 16GB GART limit of
//   the riser, the module still finds it and reprograms the correct bridge.
//
// Sequence (v9.4):
//   1. Scan bus 0 -> root port (hdr type 1) -> secondary/sub bus
//   2. For each bus under the root port, look for endpoint 1002:67A0 type 0
//   3. Once found: bridgeAddr = that root port, epAddr = endpoint
//   4. pciFindExtCapEcam(cap_id=0x0015) -> expected offset 0x200
//   5. Bridge window pref64 [W9170_BRIDGE_BASE..W9170_BRIDGE_LIMIT]
//      (0x1C00000000 - 0x2000FFFFFF: covers BAR0 16GB + BAR2 8MB + slack)
//   6. REBAR_CTRL (cap+0x208) size index 14 (-> 16 GB) on W9170 endpoint
//   7. BAR0 = W9170_BAR0_TARGET (0x1C00000000), 64-bit pref
//   8. BAR2 = W9170_BAR2_TARGET (0x2000000000), 64-bit pref, 8MB non-resizable
//   9. Log [DF] + POST 0xDF
//
// v9.4: no cursor, no hardcoded bridge-DID. Constant #define addresses,
// position-independent. Fail paths log [DE] + POST 0xDE and return
// without writing anything incompatible (implicit rollback).

static VOID ResizeAmdGpuBars(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo)
{
    UINT8  rootDev, rootFunc;
    UINT8  bridgeDev = 0, bridgeFunc = 0;
    UINT64 bridgeAddr = 0;
    UINT32 bridgeVidDid = 0;
    BOOLEAN foundEndpoint = FALSE;
    UINT8  secBus = 0, epDev = 0;
    UINT64 epAddr = 0;
    UINT32 epVidDid = 0;
    UINT8  epHdr;
    UINT16 rebarOff;
    UINT32 rebarCap;
    UINT32 rebarCtrl;
    UINT8  maxSizeIdx;
    UINT64 newSize;
    UINT64 assignAddr;
    UINT32 newLo, newHi;
    UINT16 pBase, pLimit;
    UINT32 pBaseUp, pLimitUp;
    UINT16 cmd;
    UINT16 cmdOff;
    UINT32 readback = 0;

    IoWrite8(POST_PORT, POST_EXITBS_AMD_REBAR);
    L_STR("[DE] ResizeAmdGpuBars v9.4 enter\n");

    // v9.4: bus-agnostic discovery — scan all root ports on bus 0 and
    // look for endpoint 1002:67A0 (W9170 Hawaii XT GL) underneath. This
    // way the module finds the W9170 whether it is on the riser
    // (00:15.3/43A3) or in slot 3 (00:0b.0/5A1F) or any other slot.
    for (rootDev = 0; rootDev < 32 && !foundEndpoint; rootDev++) {
        for (rootFunc = 0; rootFunc < 8 && !foundEndpoint; rootFunc++) {
            UINT64  rpAddr = EFI_PCI_ADDRESS(0, rootDev, rootFunc, 0);
            UINT32  rpVidDid;
            UINT8   rpHdr, rpSecBus, rpSubBus;
            UINT8   bus, d;

            if (EFI_ERROR(pciReadDword(rbIo, rpAddr, &rpVidDid))) break;
            if ((rpVidDid & 0xFFFF) == 0xFFFF) {
                if (rootFunc == 0) break;  // device absent, skip
                continue;
            }
            if (EFI_ERROR(pciReadByte(rbIo, rpAddr + 0x0E, &rpHdr))) continue;
            // Bridges only (type 1). If func=0 and not multi-function, skip dev.
            if ((rpHdr & 0x7F) != 1) {
                if (rootFunc == 0 && (rpHdr & 0x80) == 0) break;
                continue;
            }
            pciReadByte(rbIo, rpAddr + 0x19, &rpSecBus);
            pciReadByte(rbIo, rpAddr + 0x1A, &rpSubBus);
            if (rpSecBus == 0) continue;

            // Scan all buses under the root port
            for (bus = rpSecBus; bus <= rpSubBus && !foundEndpoint; bus++) {
                for (d = 0; d < 32 && !foundEndpoint; d++) {
                    UINT64 devAddr = EFI_PCI_ADDRESS(bus, d, 0, 0);
                    UINT32 vd;
                    UINT8  hdr;

                    if (EFI_ERROR(pciReadDword(rbIo, devAddr, &vd))) continue;
                    if ((vd & 0xFFFF) == 0xFFFF) continue;
                    if ((vd & 0xFFFF) != 0x1002) continue;
                    if ((UINT16)(vd >> 16) != 0x67A0) continue;
                    if (EFI_ERROR(pciReadByte(rbIo, devAddr + 0x0E, &hdr)))
                        continue;
                    if ((hdr & 0x7F) != 0) continue;  // endpoint type 0 only

                    // W9170 match!
                    bridgeDev    = rootDev;
                    bridgeFunc   = rootFunc;
                    bridgeAddr   = rpAddr;
                    bridgeVidDid = rpVidDid;
                    secBus       = bus;
                    epDev        = d;
                    epAddr       = devAddr;
                    epVidDid     = vd;
                    foundEndpoint = TRUE;
                }
            }
        }
    }

    if (!foundEndpoint) {
        L_STR("[DE] W9170 (1002:67A0) NOT FOUND on any root port — skip\n");
        IoWrite8(POST_PORT, POST_AMD_REBAR_DONE);
        return;
    }

    L_STR("[DE] W9170 @ "); L_HEX(secBus, 2); L_STR(":");
    L_HEX(epDev, 2); L_STR(".0 under bridge 00:");
    L_HEX(bridgeDev, 2); L_STR("."); L_HEX(bridgeFunc, 1);
    L_STR(" (bridgeVidDid=0x"); L_HEX(bridgeVidDid, 8); L_STR(")\n");

    // Endpoint header already validated as type 0 in the discovery loop
    pciReadByte(rbIo, epAddr + 0x0E, &epHdr);
    (void)epHdr;  // already verified during the scan

    // 4. Find ReBAR ext cap (expected offset 0x200)
    rebarOff = pciFindExtCapEcam(rbIo, secBus, epDev, 0, PCI_EXT_CAP_ID_REBAR);
    if (rebarOff == 0) {
        L_STR("[DE] W9170 without ReBAR cap — skip\n");
        IoWrite8(POST_PORT, POST_AMD_REBAR_DONE);
        return;
    }
    L_STR("[DE] W9170 ReBAR @ 0x"); L_HEX(rebarOff, 4); L_NL();

    // 5. Read REBAR_CAP (cap+0x04) to determine the max supported size
    if (EFI_ERROR(pciReadDwordEcam(rbIo, secBus, epDev, 0, rebarOff + 0x04, &rebarCap))) {
        L_STR("[DE] REBAR_CAP read FAIL\n");
        IoWrite8(POST_PORT, POST_AMD_REBAR_DONE);
        return;
    }
    L_STR("[DE] REBAR_CAP=0x"); L_HEX(rebarCap, 8); L_NL();

    // Size mask bits[31:4], bit N = size 2^(N+20).
    // For W9170 expected 0x0007F000 -> bits 12-18 set -> max N=18 -> 256 MB..16 GB
    // In REBAR_CAP bit[4] seems to be reserved/padding on some implementations.
    // We use (rebarCap >> 4) & 0x0FFFFFFF like in ResizeIntelGpuBars.
    {
        UINT32 sizes = (rebarCap >> 4) & 0x0FFFFFFF;
        UINT8  s;
        maxSizeIdx = 0;
        for (s = 27; s > 0; s--) {
            if (sizes & (1U << s)) { maxSizeIdx = s; break; }
        }
        if (maxSizeIdx == 0 && (sizes & 1) == 0) {
            L_STR("[DE] REBAR_CAP sizes=0 — skip\n");
            IoWrite8(POST_PORT, POST_AMD_REBAR_DONE);
            return;
        }
    }
    newSize = 1ULL << (maxSizeIdx + 20);
    L_STR("[DE] max size idx="); L_HEX(maxSizeIdx, 2);
    L_STR(" = "); L_HEX((UINT32)(newSize >> 20), 8); L_STR(" MB\n");

    // 6. Read current REBAR_CTRL (cap+0x08)
    if (EFI_ERROR(pciReadDwordEcam(rbIo, secBus, epDev, 0, rebarOff + 0x08, &rebarCtrl))) {
        L_STR("[DE] REBAR_CTRL read FAIL\n");
        IoWrite8(POST_PORT, POST_AMD_REBAR_DONE);
        return;
    }
    L_STR("[DE] REBAR_CTRL (pre)=0x"); L_HEX(rebarCtrl, 8); L_NL();

    // 7. v9.4: hard-coded BAR0 target (no cursor, position-independent).
    assignAddr = W9170_BAR0_TARGET;
    L_STR("[DE] W9170 BAR0 target=0x"); L_HEX(assignAddr, 16); L_NL();

    // 8. v9.4 bridge window pref64: covers BAR0 (16GB @ 0x1C00000000)
    //    + BAR2 (8MB @ 0x2000000000) + 1MB margin. Endpoints from constants.
    //    The bridge is the one discovered in step 1 (whatever root port that is).
    pBase    = (UINT16)((W9170_BRIDGE_BASE  >> 16) & 0xFFF0) | 0x0001;
    pLimit   = (UINT16)((W9170_BRIDGE_LIMIT >> 16) & 0xFFF0) | 0x0001;
    pBaseUp  = (UINT32)(W9170_BRIDGE_BASE  >> 32);
    pLimitUp = (UINT32)(W9170_BRIDGE_LIMIT >> 32);

    pciWriteWord (rbIo, bridgeAddr + 0x24, &pBase);
    pciWriteWord (rbIo, bridgeAddr + 0x26, &pLimit);
    pciWriteDword(rbIo, bridgeAddr + 0x28, &pBaseUp);
    pciWriteDword(rbIo, bridgeAddr + 0x2C, &pLimitUp);
    L_STR("[DE] bridge 00:"); L_HEX(bridgeDev, 2); L_STR(".");
    L_HEX(bridgeFunc, 1); L_STR(" pref64 PB="); L_HEX(pBase, 4);
    L_STR(" PL="); L_HEX(pLimit, 4);
    L_STR(" PBu="); L_HEX(pBaseUp, 8);
    L_STR(" PLu="); L_HEX(pLimitUp, 8); L_NL();

    // Enable Memory+BusMaster on the bridge
    pciReadWord(rbIo, bridgeAddr + 0x04, &cmd);
    cmd |= 0x0006;
    pciWriteWord(rbIo, bridgeAddr + 0x04, &cmd);

    // 9. Disable Memory Space on the device before changing BAR/size
    pciReadWord(rbIo, epAddr + 0x04, &cmd);
    cmdOff = cmd & 0xFFF9;   // clear MemSpace(1) + BusMaster(2)
    pciWriteWord(rbIo, epAddr + 0x04, &cmdOff);

    // 10. Write REBAR_CTRL with size index
    rebarCtrl = (rebarCtrl & ~0x3F00U) | ((UINT32)maxSizeIdx << 8);
    if (EFI_ERROR(pciWriteDwordEcam(rbIo, secBus, epDev, 0, rebarOff + 0x08, &rebarCtrl))) {
        L_STR("[DE] REBAR_CTRL write FAIL\n");
        // Restore command and return
        pciWriteWord(rbIo, epAddr + 0x04, &cmd);
        IoWrite8(POST_PORT, POST_AMD_REBAR_DONE);
        return;
    }
    pciReadDwordEcam(rbIo, secBus, epDev, 0, rebarOff + 0x08, &readback);
    L_STR("[DE] REBAR_CTRL (post)=0x"); L_HEX(readback, 8); L_NL();
    if (((readback >> 8) & 0x3F) != maxSizeIdx) {
        L_STR("[DE] REBAR_CTRL readback mismatch — hw refuses the resize, skip BAR write\n");
        pciWriteWord(rbIo, epAddr + 0x04, &cmd);
        IoWrite8(POST_PORT, POST_AMD_REBAR_DONE);
        return;
    }

    // 11. Write BAR0 low/high: addr[31:4] + 64-bit pref type (bits 0x0C)
    newLo = (UINT32)(assignAddr & 0xFFFFFFF0) | 0x0C;  // 64-bit pref
    newHi = (UINT32)(assignAddr >> 32);
    pciWriteDword(rbIo, epAddr + 0x10, &newLo);
    pciWriteDword(rbIo, epAddr + 0x14, &newHi);
    L_STR("[DE] W9170 BAR0 low=0x"); L_HEX(newLo, 8);
    L_STR(" high=0x"); L_HEX(newHi, 8); L_NL();

    // 11b. v9.3: program BAR2 (8MB MMIO regs, 64-bit pref, non-resizable).
    //      Verify that the current config indicates 64-bit pref, then
    //      write the 8 bytes of BAR2 (0x18 low / 0x1C high) to the target.
    //      This prevents the kernel from leaving BAR2 at 0 ("can't claim"):
    //      we explicitly assign a home inside the bridge window.
    {
        UINT32 bar2Lo = 0, bar2LoNew, bar2HiNew;
        pciReadDword(rbIo, epAddr + 0x18, &bar2Lo);
        L_STR("[DE] W9170 BAR2 origLo=0x"); L_HEX(bar2Lo, 8); L_NL();
        // bit0 == 0 (memory), bits[2:1] == 10 (64-bit), bit3 = pref (should be 1)
        if ((bar2Lo & 0x01) == 0 && (bar2Lo & 0x06) == 0x04) {
            bar2LoNew = (UINT32)(W9170_BAR2_TARGET & 0xFFFFFFF0)
                | (bar2Lo & 0x0F);   // preserve flags (64-bit, pref)
            bar2HiNew = (UINT32)(W9170_BAR2_TARGET >> 32);
            pciWriteDword(rbIo, epAddr + 0x18, &bar2LoNew);
            pciWriteDword(rbIo, epAddr + 0x1C, &bar2HiNew);
            L_STR("[DE] W9170 BAR2 low=0x"); L_HEX(bar2LoNew, 8);
            L_STR(" high=0x"); L_HEX(bar2HiNew, 8); L_NL();
        } else {
            L_STR("[DE] W9170 BAR2 not 64-bit pref — skip programming\n");
        }
    }

    // 12. Re-enable Memory+BusMaster on the device
    cmd |= 0x0006;
    pciWriteWord(rbIo, epAddr + 0x04, &cmd);

    L_STR("[DF] ResizeAmdGpuBars OK v9.4 — BAR0 16GB @ 0x");
    L_HEX(assignAddr, 16);
    L_STR(" + BAR2 8MB @ 0x"); L_HEX(W9170_BAR2_TARGET, 16); L_NL();
    IoWrite8(POST_PORT, POST_AMD_REBAR_DONE);
}

static VOID EFIAPI OnExitBootServices(
    IN EFI_EVENT    Event,
    IN VOID*        Context
)
{
    IoWrite8(POST_PORT, POST_EXITBS_ENTER);
    L_STR("[D8] OnExitBootServices entry, hidden=");
    L_DEC(gHiddenDeviceCount); L_NL();
    DEBUG((DEBUG_INFO,
        "ReBarDXE: === ExitBootServices === %d hidden devices [POST D8]\n",
        (UINT32)gHiddenDeviceCount));

    // 0. CPU NB MMIO Window 1: DEFINITIVE programming
    //    The write at DXE dispatch (step 2) is lost — something zeroes it
    //    before Linux. The HT gate (step 1 below) survives because it is
    //    on the SR5690, not the CPU NB. Reprogram with force=TRUE.
    //    Window 1: from above-RAM to 256GB (covers 4 GPUs: 2xP100+A770+FirePro)
    if (gSavedRbIo != NULL) {
        IoWrite8(POST_PORT, POST_EXITBS_NB_WINDOW);
        // Readback BEFORE the forced reprogramming
        {
            UINT64 addr = EFI_PCI_ADDRESS(0, 0x18, 1, 0x88);
            UINT32 w88 = 0, w8c = 0;
            gSavedRbIo->Pci.Read(gSavedRbIo, EfiPciWidthUint32, addr, 1, &w88);
            addr = EFI_PCI_ADDRESS(0, 0x18, 1, 0x8C);
            gSavedRbIo->Pci.Read(gSavedRbIo, EfiPciWidthUint32, addr, 1, &w8c);
            L_STR("[D9] NB Window BEFORE force: 88=");
            L_HEX(w88, 8); L_STR(" 8C=");
            L_HEX(w8c, 8); L_NL();
        }
        if (ProgramCpuMmioWindow(gSavedRbIo, TRUE)) {
            L_STR("[D9] ProgramCpuMmioWindow FORCE: OK\n");
            DEBUG((DEBUG_INFO,
                "ReBarDXE: ExitBS NB MMIO Window 1 FORCED (256GB) [POST D9]\n"));
        } else {
            L_STR("[D9] ProgramCpuMmioWindow FORCE: FAIL\n");
            DEBUG((DEBUG_ERROR,
                "ReBarDXE: ExitBS ERROR programming NB Window!\n"));
        }
        // Readback AFTER
        {
            UINT64 addr = EFI_PCI_ADDRESS(0, 0x18, 1, 0x88);
            UINT32 w88 = 0, w8c = 0;
            gSavedRbIo->Pci.Read(gSavedRbIo, EfiPciWidthUint32, addr, 1, &w88);
            addr = EFI_PCI_ADDRESS(0, 0x18, 1, 0x8C);
            gSavedRbIo->Pci.Read(gSavedRbIo, EfiPciWidthUint32, addr, 1, &w8c);
            L_STR("[D9] NB Window AFTER force: 88=");
            L_HEX(w88, 8); L_STR(" 8C=");
            L_HEX(w8c, 8); L_NL();
        }
    }

    // 1. Open the 64-bit HT gate on the SR5690 (was setpci -s 00:00.0 94.L=3)
    if (gSavedRbIo != NULL) {
        IoWrite8(POST_PORT, POST_EXITBS_HT_GATE);
        // Pre-gate readback
        {
            UINT64 addr = EFI_PCI_ADDRESS(0, 0, 0, 0x94);
            UINT32 w94 = 0;
            gSavedRbIo->Pci.Read(gSavedRbIo, EfiPciWidthUint32, addr, 1, &w94);
            L_STR("[DA] SR5690 0x94 BEFORE: "); L_HEX(w94, 8); L_NL();
        }
        ProgramHtBridgeGate(gSavedRbIo);
        {
            UINT64 addr = EFI_PCI_ADDRESS(0, 0, 0, 0x94);
            UINT32 w94 = 0;
            gSavedRbIo->Pci.Read(gSavedRbIo, EfiPciWidthUint32, addr, 1, &w94);
            L_STR("[DA] SR5690 0x94 AFTER:  "); L_HEX(w94, 8); L_NL();
        }
    }

    // 2. Apply the 990FX bridge quirk on ALL bridges (64-bit prefetchable)
    if (gSavedRbIo != NULL) {
        L_STR("[DB] Apply990FxBridgeQuirkAll\n");
        Apply990FxBridgeQuirkAll(gSavedRbIo);
    }

    // 3. Re-enable the PCIe links of the hidden devices
    IoWrite8(POST_PORT, POST_EXITBS_LINK_EN);
    L_STR("[DB] ReEnableAllHiddenDevices\n");
    ReEnableAllHiddenDevices();

    // 4. Resizable BAR on Intel GPU (Arc A770) — resize from 256MB to max
    //    Must run AFTER the bridge quirk (step 2) because it needs 64-bit prefetchable
    //    Uses global cursors shared with ProgramHiddenDeviceBars
    if (gSavedRbIo != NULL) {
        L_STR("[DC] ResizeIntelGpuBars enter\n");
        ResizeIntelGpuBars(gSavedRbIo);
        L_STR("[DC] ResizeIntelGpuBars exit, nextPref=");
        L_HEX(gNextPrefAddr, 16); L_STR(" nextMmio=");
        L_HEX(gNextMmioAddr, 16); L_NL();
    }

    // 5. v9: Resizable BAR on FirePro W9170 (via SB900 riser) — 256 MB -> 16 GB.
    //    Must run AFTER ResizeIntelGpuBars because it reuses gNextPrefAddr
    //    updated by the Arc window (expected: 0x1C00000000).
    //    Sole owner of the 00:15.3 bridge window (the v8 branch in
    //    Apply990FxBridgeQuirkAll has been removed).
    if (gSavedRbIo != NULL) {
        ResizeAmdGpuBars(gSavedRbIo);
        L_STR("[DF] ExitBS after AMD resize, nextPref=");
        L_HEX(gNextPrefAddr, 16); L_NL();
    }

    IoWrite8(POST_PORT, POST_EXITBS_DONE);
    L_STR("[DD] ExitBS sequence complete, flushing log to NVRAM\n");
    DEBUG((DEBUG_INFO, "ReBarDXE: === Ready for Linux pci=realloc === [POST DD]\n"));

    // --- V6 FLUSH: write the buffer to the UEFI NVRAM variable ---
    // Done here because after the FC sequence (delay) we are effectively
    // past ExitBootServices and SetVariable may become unreliable
    LogFlushToNvram();

    // v9: acoustic melody "2 short · 2 long · 2 short" at 880 Hz before
    // POST 0xFC — ear+LED sync. Useful when the Arc loses display with
    // BAR >4G: you know you reached the end of ExitBS even with a black screen.
    // Duration ~2.1 s; the FC delay below becomes largely superfluous but we
    // keep it as a buffer.
    V9PlayFcMelody();

    // FC = "Full Complete" — visible ~2 seconds on the motherboard LED
    // before the BIOS overwrites it with AA
    IoWrite8(POST_PORT, POST_FULL_COMPLETE);
    {
        volatile UINT64 delay;
        for (delay = 0; delay < 200000000ULL; delay++) {}
    }
    IoWrite8(POST_PORT, POST_FULL_COMPLETE);  // final re-write
}

// =====================================================================
// DSDT PATCH: UNLOCK 64-BIT WINDOW
// =====================================================================

static UINT8 gDsdtSearchPattern[] = {
    0x08, 0x4D, 0x41, 0x4C, 0x48, 0x00,  // Name(MALH, Zero)
    0x08, 0x4D, 0x41, 0x4D, 0x4C, 0x00,  // Name(MAML, Zero)
    0x08, 0x4D, 0x41, 0x4D, 0x48, 0x00   // Name(MAMH, Zero)
};

static EFI_ACPI_DESCRIPTION_HEADER* FindDsdt(EFI_SYSTEM_TABLE* SystemTable)
{
    EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER* Rsdp = NULL;
    EFI_ACPI_DESCRIPTION_HEADER* Xsdt = NULL;
    UINT64* XsdtEntry;
    UINTN EntryCount;
    UINTN Index;

    for (Index = 0; Index < SystemTable->NumberOfTableEntries; Index++) {
        EFI_CONFIGURATION_TABLE* Table = &SystemTable->ConfigurationTable[Index];
        if (CompareGuid(&Table->VendorGuid, &gEfiAcpi20TableGuid)) {
            Rsdp = (EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER*)Table->VendorTable;
            break;
        }
        if (CompareGuid(&Table->VendorGuid, &gEfiAcpi10TableGuid)) {
            Rsdp = (EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER*)Table->VendorTable;
        }
    }

    if (Rsdp == NULL) {
        DEBUG((DEBUG_ERROR, "ReBarDXE: RSDP not found\n"));
        return NULL;
    }

    if (Rsdp->Revision >= 2 && Rsdp->XsdtAddress != 0) {
        Xsdt = (EFI_ACPI_DESCRIPTION_HEADER*)(UINTN)Rsdp->XsdtAddress;
        EntryCount = (Xsdt->Length - sizeof(EFI_ACPI_DESCRIPTION_HEADER)) / sizeof(UINT64);
        XsdtEntry = (UINT64*)((UINT8*)Xsdt + sizeof(EFI_ACPI_DESCRIPTION_HEADER));

        for (Index = 0; Index < EntryCount; Index++) {
            EFI_ACPI_DESCRIPTION_HEADER* Header =
                (EFI_ACPI_DESCRIPTION_HEADER*)(UINTN)XsdtEntry[Index];
            if (Header->Signature == EFI_ACPI_2_0_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE) {
                EFI_ACPI_2_0_FIXED_ACPI_DESCRIPTION_TABLE* Fadt =
                    (EFI_ACPI_2_0_FIXED_ACPI_DESCRIPTION_TABLE*)Header;
                if (Fadt->Header.Revision >= 2 && Fadt->XDsdt != 0)
                    return (EFI_ACPI_DESCRIPTION_HEADER*)(UINTN)Fadt->XDsdt;
                if (Fadt->Dsdt != 0)
                    return (EFI_ACPI_DESCRIPTION_HEADER*)(UINTN)Fadt->Dsdt;
            }
        }
    }
    else if (Rsdp->RsdtAddress != 0) {
        EFI_ACPI_DESCRIPTION_HEADER* Rsdt =
            (EFI_ACPI_DESCRIPTION_HEADER*)(UINTN)Rsdp->RsdtAddress;
        UINT32* RsdtEntry;

        EntryCount = (Rsdt->Length - sizeof(EFI_ACPI_DESCRIPTION_HEADER)) / sizeof(UINT32);
        RsdtEntry = (UINT32*)((UINT8*)Rsdt + sizeof(EFI_ACPI_DESCRIPTION_HEADER));

        for (Index = 0; Index < EntryCount; Index++) {
            EFI_ACPI_DESCRIPTION_HEADER* Header =
                (EFI_ACPI_DESCRIPTION_HEADER*)(UINTN)RsdtEntry[Index];
            if (Header->Signature == EFI_ACPI_2_0_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE) {
                EFI_ACPI_2_0_FIXED_ACPI_DESCRIPTION_TABLE* Fadt =
                    (EFI_ACPI_2_0_FIXED_ACPI_DESCRIPTION_TABLE*)Header;
                if (Fadt->Dsdt != 0)
                    return (EFI_ACPI_DESCRIPTION_HEADER*)(UINTN)Fadt->Dsdt;
            }
        }
    }

    DEBUG((DEBUG_ERROR, "ReBarDXE: DSDT not found\n"));
    return NULL;
}

static VOID FixDsdtChecksum(EFI_ACPI_DESCRIPTION_HEADER* Dsdt)
{
    UINT8* Bytes = (UINT8*)Dsdt;
    UINT32 Length = Dsdt->Length;
    UINT8 Sum = 0;
    UINT32 i;

    Dsdt->Checksum = 0;
    for (i = 0; i < Length; i++)
        Sum += Bytes[i];
    Dsdt->Checksum = (UINT8)(0x100 - Sum);
}

static BOOLEAN PatchDsdtMalh(EFI_SYSTEM_TABLE* SystemTable)
{
    EFI_ACPI_DESCRIPTION_HEADER* Dsdt;
    UINT8* DsdtBytes;
    UINT32 DsdtLength;
    UINT32 i;
    UINT32 PatternLen = sizeof(gDsdtSearchPattern);

    Dsdt = FindDsdt(SystemTable);
    if (Dsdt == NULL)
        return FALSE;

    if (Dsdt->Signature != EFI_ACPI_2_0_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE) {
        DEBUG((DEBUG_ERROR, "ReBarDXE: DSDT invalid signature: 0x%x\n", Dsdt->Signature));
        return FALSE;
    }

    DsdtBytes = (UINT8*)Dsdt;
    DsdtLength = Dsdt->Length;

    DEBUG((DEBUG_INFO, "ReBarDXE: DSDT len=%u checksum=0x%x\n", DsdtLength, Dsdt->Checksum));

    for (i = sizeof(EFI_ACPI_DESCRIPTION_HEADER); i + PatternLen <= DsdtLength; i++) {
        if (DsdtBytes[i]   == gDsdtSearchPattern[0] &&
            DsdtBytes[i+1] == gDsdtSearchPattern[1] &&
            DsdtBytes[i+5] == gDsdtSearchPattern[5] &&
            DsdtBytes[i+6] == gDsdtSearchPattern[6]) {

            BOOLEAN match = TRUE;
            UINT32 j;
            for (j = 0; j < PatternLen; j++) {
                if (DsdtBytes[i+j] != gDsdtSearchPattern[j]) {
                    match = FALSE;
                    break;
                }
            }
            if (!match) continue;

            DEBUG((DEBUG_INFO, "ReBarDXE: DSDT pattern found at offset 0x%x\n", i));

            DsdtBytes[i + 5]  = AML_ONE_OP;    // MALH = One (enables 64-bit window)
            // MAML and MAMH not touched: CRS1 (CPRB=One) uses hardcoded values,
            // CRS2 is not reached on this board.

            FixDsdtChecksum(Dsdt);

            DEBUG((DEBUG_INFO, "ReBarDXE: DSDT PATCH APPLIED! 64-bit window active\n"));
            return TRUE;
        }
    }

    DEBUG((DEBUG_WARN, "ReBarDXE: DSDT pattern not found (already patched?)\n"));
    return FALSE;
}

// =====================================================================
// CPU FX NORTHBRIDGE: PROGRAM 64-BIT MMIO WINDOW
// =====================================================================
// The Address Map registers of the AMD FX (Family 15h) live at Bus0:Dev24(0x18):Func1.
// Desktop AGESA never programs the MMIO windows above 4GB.
// We program Window 1 (D18F1x88/8C) to route MMIO transactions
// from above RAM up to 64GB-1 toward the SR5690 (DstNode=0).
//
// Register format (BKDG Family 15h):
//   D18F1x[80+8*n] Base:  bits[31:8] = MmioBase[39:16], bit[1]=WE, bit[0]=RE
//   D18F1x[84+8*n] Limit: bits[31:8] = MmioLimit[39:16], bits[2:0]=DstNode
//   D18F1x[180+8*n] Base High:  bits[7:0] = MmioBase[47:40]
//   D18F1x[184+8*n] Limit High: bits[7:0] = MmioLimit[47:40]

// CPU NB PCI address: Bus 0, Device 0x18, Function 1
#define CPU_NB_BUS   0
#define CPU_NB_DEV   0x18
#define CPU_NB_FUNC  1

// Window 1 register offsets
#define MMIO_BASE_LOW_1   0x88
#define MMIO_LIMIT_LOW_1  0x8C
#define MMIO_BASE_HIGH_1  0x188
#define MMIO_LIMIT_HIGH_1 0x18C

// DRAM Limit register (used to find top-of-memory dynamically)
#define DRAM_LIMIT_LOW_0  0x44
#define DRAM_LIMIT_HIGH_0 0x144

// force=FALSE: skip if already active (for the DXE dispatch call)
// force=TRUE:  always program (for ExitBootServices — the DXE value is lost)
static BOOLEAN ProgramCpuMmioWindow(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo, BOOLEAN force)
{
    UINT64 cpuNbAddr;
    UINT32 dramLimitLow, dramLimitHigh;
    UINT32 baseLow, limitLow, baseHigh, limitHigh;
    UINT32 existingBase;
    UINT64 topOfMem;
    UINT64 mmioBase;
    UINT64 mmioLimit;

    cpuNbAddr = EFI_PCI_ADDRESS(CPU_NB_BUS, CPU_NB_DEV, CPU_NB_FUNC, 0);

    // If not forced, check whether Window 1 is already programmed
    if (!force) {
        if (EFI_ERROR(pciReadDword(rbIo, cpuNbAddr + MMIO_BASE_LOW_1, &existingBase)))
            return FALSE;

        if (existingBase & 0x03) {
            DEBUG((DEBUG_INFO,
                "ReBarDXE: CPU MMIO Window 1 already active (0x%08x), skip\n",
                existingBase));
            return TRUE;
        }
    }

    // Read DRAM Limit to find where RAM ends
    // NOTE: offsets >= 0x100 must be accessed via pciAddrOffset() so that
    // EFI_PCI_ADDRESS encodes the extended bits correctly instead of
    // overflowing into the function-number field of the address.
    if (EFI_ERROR(pciReadDword(rbIo, cpuNbAddr + DRAM_LIMIT_LOW_0, &dramLimitLow)))
        return FALSE;
    if (EFI_ERROR(pciReadDword(rbIo, pciAddrOffset(cpuNbAddr, DRAM_LIMIT_HIGH_0), &dramLimitHigh)))
        return FALSE;

    // DRAM Limit format: bits[31:21] = DramLimit[31:21]
    // DramLimit High: bits[7:0] = DramLimit[39:32]
    topOfMem = ((UINT64)(dramLimitHigh & 0xFF) << 32) |
               ((UINT64)(dramLimitLow & 0xFFE00000));
    // Add the trailing block (2MB granularity)
    topOfMem += 0x200000;

    DEBUG((DEBUG_INFO,
        "ReBarDXE: DRAM Top of Memory = 0x%lx\n", topOfMem));

    // MMIO Base: just above RAM, aligned to 64KB (minimum granularity)
    // Add 256MB margin for safety
    mmioBase = (topOfMem + 0x10000000ULL) & ~0xFFFFULL;

    // MMIO Limit: 256GB - 1
    // Sized for the multi-GPU configuration:
    //   P100 #1:    16GB pref (slot 1)
    //   P100 #2:    16GB pref (slot 2, NVLink)
    //   Arc A770:   16GB pref (slot 3, display)
    //   FirePro:    32GB pref (via PCIe x1 riser)
    //   + non-pref BAR0/BAR3 for each GPU (~200MB total)
    // Total: ~80GB+ of BAR space, 256GB gives plenty of margin
    mmioLimit = 0x3FFFFFFFFFULL;

    DEBUG((DEBUG_INFO,
        "ReBarDXE: Programming CPU MMIO Window 1: 0x%lx - 0x%lx (force=%d)\n",
        mmioBase, mmioLimit, (UINT32)force));

    // Compute register values
    // Base Low: bits[31:8] = MmioBase[39:16], bit[1]=WE, bit[0]=RE
    baseLow = (UINT32)(((mmioBase >> 16) & 0xFFFFFF) << 8) | 0x03;

    // Limit Low: bits[31:8] = MmioLimit[39:16], bits[2:0]=DstNode(0)
    limitLow = (UINT32)(((mmioLimit >> 16) & 0xFFFFFF) << 8) | 0x00;

    // Base High: bits[7:0] = MmioBase[47:40]
    baseHigh = (UINT32)((mmioBase >> 40) & 0xFF);

    // Limit High: bits[7:0] = MmioLimit[47:40]
    limitHigh = (UINT32)((mmioLimit >> 40) & 0xFF);

    DEBUG((DEBUG_INFO,
        "ReBarDXE: Registers: Base=0x%08x/%08x Limit=0x%08x/%08x\n",
        baseHigh, baseLow, limitHigh, limitLow));

    // WRITE: high registers first, then low (atomic activation)
    // Offsets 0x188 and 0x18C are >= 0x100 — must use pciAddrOffset() so
    // EFI_PCI_ADDRESS encodes the extended bits instead of overflowing into
    // the PCI function-number field.
    if (EFI_ERROR(pciWriteDword(rbIo, pciAddrOffset(cpuNbAddr, MMIO_BASE_HIGH_1), &baseHigh)))
        return FALSE;
    if (EFI_ERROR(pciWriteDword(rbIo, pciAddrOffset(cpuNbAddr, MMIO_LIMIT_HIGH_1), &limitHigh)))
        return FALSE;
    if (EFI_ERROR(pciWriteDword(rbIo, cpuNbAddr + MMIO_LIMIT_LOW_1, &limitLow)))
        return FALSE;
    // Base Low goes last (contains RE/WE which activate the window)
    if (EFI_ERROR(pciWriteDword(rbIo, cpuNbAddr + MMIO_BASE_LOW_1, &baseLow)))
        return FALSE;

    DEBUG((DEBUG_INFO,
        "ReBarDXE: CPU MMIO Window 1 PROGRAMMED! 256GB routing active\n"));

    return TRUE;
}

// =====================================================================
// SR5690 HT HOST BRIDGE: OPEN THE 64-BIT MMIO GATE
// =====================================================================
// The SR5690 (B0:D0.0) controls the forwarding of 64-bit MMIO
// transactions on the HyperTransport link between CPU and chipset.
// Without this gate open, the above-4GB MMIO transactions programmed
// in the CPU MMIO Window don't reach the SR5690's PCIe fabric.
//
// Register 0x94 bits[1:0]:
//   bit 0 = Read Enable  (RE)
//   bit 1 = Write Enable (WE)
//   0x03  = gate fully open
//
// Equivalent to the Linux command: setpci -s 00:00.0 94.L=00000003
// But we do it during boot, before PciBus.

#define SR5690_HT_HOST_BUS      0
#define SR5690_HT_HOST_DEV      0
#define SR5690_HT_HOST_FUNC     0
#define SR5690_MMIO64_GATE_REG  0x94
#define SR5690_VID              0x1002

static BOOLEAN ProgramHtBridgeGate(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo)
{
    UINT64 htAddr = EFI_PCI_ADDRESS(SR5690_HT_HOST_BUS, SR5690_HT_HOST_DEV,
                                     SR5690_HT_HOST_FUNC, 0);
    UINT32 vidDid, gateVal, newGateVal;

    // Verify that B0:D0.0 is actually the SR5690 (AMD/ATI VID)
    if (EFI_ERROR(pciReadDword(rbIo, htAddr, &vidDid)))
        return FALSE;

    if ((vidDid & 0xFFFF) != SR5690_VID) {
        DEBUG((DEBUG_WARN,
            "ReBarDXE: B0:D0.0 VID=0x%04x, not SR5690 (expected 0x%04x)\n",
            (UINT16)(vidDid & 0xFFFF), SR5690_VID));
        return FALSE;
    }

    DEBUG((DEBUG_INFO,
        "ReBarDXE: SR5690 found B0:D0.0 VID:DID=%04x:%04x\n",
        (UINT16)(vidDid & 0xFFFF), (UINT16)(vidDid >> 16)));

    // Read the current gate register value
    if (EFI_ERROR(pciReadDword(rbIo, htAddr + SR5690_MMIO64_GATE_REG, &gateVal)))
        return FALSE;

    DEBUG((DEBUG_INFO,
        "ReBarDXE: SR5690 reg 0x94 current = 0x%08x\n", gateVal));

    // Open the gate: set bit 0 (RE) and bit 1 (WE)
    newGateVal = gateVal | 0x03;

    if (newGateVal == gateVal) {
        DEBUG((DEBUG_INFO, "ReBarDXE: SR5690 HT gate already open\n"));
        return TRUE;
    }

    if (EFI_ERROR(pciWriteDword(rbIo, htAddr + SR5690_MMIO64_GATE_REG, &newGateVal)))
        return FALSE;

    DEBUG((DEBUG_INFO,
        "ReBarDXE: SR5690 HT GATE OPENED! reg 0x94: 0x%08x -> 0x%08x\n",
        gateVal, newGateVal));

    return TRUE;
}

// =====================================================================
// DEVICE SETUP (called by PreprocessController as a backup)
// =====================================================================

VOID reBarSetupDevice(EFI_HANDLE handle, EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_PCI_ADDRESS addrInfo)
{
    UINTN epos;
    UINT16 vid, did;
    UINTN pciAddress;
    EFI_STATUS hpStatus;

    hpStatus = gBS->HandleProtocol(handle, &gEfiPciRootBridgeIoProtocolGuid, (void**)&pciRootBridgeIo);
    if (EFI_ERROR(hpStatus) || pciRootBridgeIo == NULL) {
        DEBUG((DEBUG_WARN,
            "ReBarDXE: reBarSetupDevice HandleProtocol failed: %r\n", hpStatus));
        return;
    }

    pciAddress = EFI_PCI_ADDRESS(addrInfo.Bus, addrInfo.Device, addrInfo.Function, 0);
    pciReadConfigWord(pciAddress, 0, &vid);
    pciReadConfigWord(pciAddress, 2, &did);

    if (vid == 0xFFFF)
        return;

    DEBUG((DEBUG_INFO, "ReBarDXE: PreprocessCtrl device vid:%04x did:%04x %02x:%02x.%x\n",
        vid, did, addrInfo.Bus, addrInfo.Device, addrInfo.Function));

    // 990FX bridge quirk: moved to ExitBootServices.
    // During the BIOS we do NOT touch the bridges — the offensive hex patches (7/8)
    // would force >4GB allocation even for normal GPUs (1660 Super),
    // causing a black screen. The quirk is only needed for Linux.
    if (vid == 0x1002 && (did == 0x5a16 || did == 0x5a1c || did == 0x43A3)) {
        DEBUG((DEBUG_INFO,
            "ReBarDXE: 990FX/SB900 bridge %02x:%02x.%x (DID 0x%x) — 64-bit quirk "
            "deferred to ExitBootServices\n",
            addrInfo.Bus, addrInfo.Device, addrInfo.Function, did));
    }

    // ReBAR (only if ReBarState > 0 and device is not hidden)
    if (reBarState && !IsDeviceHidden(addrInfo.Bus, addrInfo.Device, addrInfo.Function)) {
        epos = pciFindExtCapability(pciAddress, PCI_EXT_CAP_ID_REBAR);
        if (epos) {
            for (UINT8 bar = 0; bar < 6; bar++) {
                UINT32 rBarS = pciRebarGetPossibleSizes(pciAddress, epos, vid, did, bar);
                if (!rBarS) continue;
                for (UINT8 n = MIN((UINT8)fls(rBarS), reBarState); n > 0; n--) {
                    if (rBarS & (1 << n)) {
                        pciRebarSetSize(pciAddress, epos, bar, n);
                        break;
                    }
                }
            }
        }
    }
}

// =====================================================================
// PREPROCESS CONTROLLER HOOK (BACKUP)
// =====================================================================

EFI_STATUS EFIAPI PreprocessControllerOverride(
    IN  EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL* This,
    IN  EFI_HANDLE                                        RootBridgeHandle,
    IN  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_PCI_ADDRESS       PciAddress,
    IN  EFI_PCI_CONTROLLER_RESOURCE_ALLOCATION_PHASE      Phase
)
{
    EFI_STATUS status = o_PreprocessController(This, RootBridgeHandle, PciAddress, Phase);

    if (Phase <= EfiPciBeforeResourceCollection) {
        reBarSetupDevice(RootBridgeHandle, PciAddress);
    }

    return status;
}

// =====================================================================
// PROTOCOL NOTIFY: INSTALL HOOK WHEN THE PROTOCOL BECOMES AVAILABLE
// =====================================================================
// When the module loads before PciBus, the PciHostBridgeResourceAllocation
// protocol does not exist yet. We use a notification callback to catch it
// as soon as it is installed.

VOID EFIAPI OnResAllocProtocolNotify(
    IN EFI_EVENT Event,
    IN VOID*     Context
)
{
    EFI_STATUS status;
    UINTN handleCount;
    EFI_HANDLE* handleBuffer;

    status = gBS->LocateHandleBuffer(
        ByProtocol,
        &gEfiPciHostBridgeResourceAllocationProtocolGuid,
        NULL,
        &handleCount,
        &handleBuffer);

    if (EFI_ERROR(status) || handleCount == 0)
        return;

    status = gBS->OpenProtocol(
        handleBuffer[0],
        &gEfiPciHostBridgeResourceAllocationProtocolGuid,
        (VOID**)&pciResAlloc,
        gImageHandle,
        NULL,
        EFI_OPEN_PROTOCOL_GET_PROTOCOL);

    if (EFI_ERROR(status)) {
        FreePool(handleBuffer);
        return;
    }

    DEBUG((DEBUG_INFO, "ReBarDXE: ResourceAllocation protocol found, installing PreprocessController hook\n"));
    o_PreprocessController = pciResAlloc->PreprocessController;
    pciResAlloc->PreprocessController = &PreprocessControllerOverride;
    FreePool(handleBuffer);

    // Close the event — hook installed, no longer needed
    gBS->CloseEvent(gResAllocNotifyEvent);
    gResAllocNotifyEvent = NULL;
}

// =====================================================================
// READY TO BOOT CALLBACK — DSDT PATCH
// =====================================================================
// The DSDT is installed by the AMI ACPI driver, which runs AFTER our
// module. We patch the DSDT at ReadyToBoot, when all ACPI tables are
// definitely loaded but BEFORE the bootloader reads them.

static EFI_EVENT gReadyToBootEvent = NULL;

VOID EFIAPI OnReadyToBoot(
    IN EFI_EVENT Event,
    IN VOID*     Context
)
{
    EFI_SYSTEM_TABLE* SystemTable = (EFI_SYSTEM_TABLE*)Context;

    DEBUG((DEBUG_INFO, "ReBarDXE: ReadyToBoot — attempting DSDT patch\n"));

    if (PatchDsdtMalh(SystemTable)) {
        DEBUG((DEBUG_INFO, "ReBarDXE: DSDT PATCH APPLIED in ReadyToBoot!\n"));
    } else {
        DEBUG((DEBUG_WARN, "ReBarDXE: DSDT patch failed even in ReadyToBoot\n"));
    }

    // Close the event — no longer needed
    gBS->CloseEvent(Event);
    gReadyToBootEvent = NULL;
}

// =====================================================================
// ENTRY POINT
// =====================================================================

EFI_STATUS EFIAPI rebarInit(
    IN EFI_HANDLE imageHandle,
    IN EFI_SYSTEM_TABLE* systemTable)
{
    UINTN bufferSize = 1;
    EFI_STATUS status;
    UINT32 attributes;
    EFI_TIME time;

    IoWrite8(POST_PORT, POST_MODULE_LOADED);
    DEBUG((DEBUG_INFO, "ReBarDXE: ===== LOADED ===== [POST D0]\n"));

    // --- V8 LOG: banner + CSM scan ---
    L_STR("=== 990fxOrchestrator v9.5 LOG ===\n");
    L_STR("[D0] rebarInit entry\n");
    L_STR("[D0] imageHandle="); L_HEX((UINT64)(UINTN)imageHandle, 16); L_NL();
    L_STR("[D0] systemTable="); L_HEX((UINT64)(UINTN)systemTable, 16); L_NL();
    L_STR("[D0] SystemTable->FirmwareRevision=");
    L_HEX(systemTable->FirmwareRevision, 8); L_NL();
    L_STR("[D0] SystemTable->NumberOfTableEntries=");
    L_DEC(systemTable->NumberOfTableEntries); L_NL();
    // Scan CSM protocols to tell whether CSM is really active
    {
        EFI_GUID legacyBiosGuid = {
            0xdb9a1e3d, 0x45cb, 0x4abb,
            {0x85, 0x3b, 0xe5, 0x38, 0x7f, 0xdb, 0x2e, 0x2d}
        };
        VOID *legProt = NULL;
        EFI_STATUS sCsm = gBS->LocateProtocol(&legacyBiosGuid, NULL, &legProt);
        L_STR("[D0] EFI_LEGACY_BIOS_PROTOCOL: ");
        if (!EFI_ERROR(sCsm)) { L_STR("PRESENT (CSM really active)\n"); }
        else { L_STR("NOT FOUND (CSM really off, menu may lie)\n"); }
    }

    // 1. DSDT PATCH: try immediately, if it fails register a ReadyToBoot callback
    IoWrite8(POST_PORT, POST_DSDT_PATCH);
    L_STR("[D1] DSDT patch attempt\n");
    if (PatchDsdtMalh(systemTable)) {
        L_STR("[D1] DSDT patched immediately (MALH=1)\n");
        DEBUG((DEBUG_INFO, "ReBarDXE: DSDT patched immediately [POST D1]\n"));
    } else {
        L_STR("[D1] DSDT not yet available, ReadyToBoot callback registered\n");
        DEBUG((DEBUG_INFO, "ReBarDXE: DSDT not yet available, registering ReadyToBoot callback\n"));
        status = gBS->CreateEventEx(
            EVT_NOTIFY_SIGNAL,
            TPL_CALLBACK,
            OnReadyToBoot,
            (VOID*)systemTable,
            &gEfiEventReadyToBootGuid,
            &gReadyToBootEvent);
        if (EFI_ERROR(status)) {
            DEBUG((DEBUG_ERROR, "ReBarDXE: ERROR creating ReadyToBoot event: %r\n", status));
        }
    }

    // 2. CPU FX MMIO WINDOW: program the northbridge to route MMIO >4GB
    IoWrite8(POST_PORT, POST_NB_MMIO_WINDOW);
    L_STR("[D2] NB MMIO Window DXE programming\n");
    {
        EFI_STATUS mmioStatus;
        UINTN mmioHandleCount;
        EFI_HANDLE* mmioHandleBuffer;
        EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* mmioRbIo;

        mmioStatus = gBS->LocateHandleBuffer(
            ByProtocol, &gEfiPciRootBridgeIoProtocolGuid,
            NULL, &mmioHandleCount, &mmioHandleBuffer);

        L_STR("[D2] PciRootBridgeIo handles=");
        L_DEC(mmioHandleCount); L_STR(" status=");
        L_HEX(mmioStatus, 8); L_NL();

        if (!EFI_ERROR(mmioStatus) && mmioHandleCount > 0) {
            mmioStatus = gBS->HandleProtocol(
                mmioHandleBuffer[0], &gEfiPciRootBridgeIoProtocolGuid,
                (VOID**)&mmioRbIo);

            if (!EFI_ERROR(mmioStatus)) {
                // 2a. CPU MMIO Window: DXE attempt (may be lost before Linux)
                // The definitive programming happens at ExitBootServices (force=TRUE)
                if (ProgramCpuMmioWindow(mmioRbIo, FALSE)) {
                    L_STR("[D2] ProgramCpuMmioWindow DXE: OK\n");
                    DEBUG((DEBUG_INFO, "ReBarDXE: CPU MMIO 64-bit window OK\n"));
                } else {
                    L_STR("[D2] ProgramCpuMmioWindow DXE: FAIL\n");
                    DEBUG((DEBUG_ERROR, "ReBarDXE: ERROR programming CPU MMIO window\n"));
                }
                // D18F1 88/8C readback to see if the write takes
                {
                    UINT64 addr = EFI_PCI_ADDRESS(0, 0x18, 1, 0x88);
                    UINT32 w88 = 0, w8c = 0;
                    mmioRbIo->Pci.Read(mmioRbIo, EfiPciWidthUint32, addr, 1, &w88);
                    addr = EFI_PCI_ADDRESS(0, 0x18, 1, 0x8C);
                    mmioRbIo->Pci.Read(mmioRbIo, EfiPciWidthUint32, addr, 1, &w8c);
                    L_STR("[D2] D18F1 readback: 88=");
                    L_HEX(w88, 8); L_STR(" 8C=");
                    L_HEX(w8c, 8); L_NL();
                }

                // Save rbIo for ExitBootServices (HT Gate + bridge quirk)
                gSavedRbIo = mmioRbIo;

                // HT Gate and bridge quirk moved to ExitBootServices:
                // during the BIOS they are not needed and can cause >4GB
                // allocation of normal GPUs (black screen with offensive patches)
            }
            FreePool(mmioHandleBuffer);
        }
    }

    // 3. PRE-SCAN: find and hide devices with BAR >4GB (BEFORE PciBus)
    IoWrite8(POST_PORT, POST_PRESCAN_START);
    L_STR("[D3] PreScan start\n");
    PreScanAndHideDevices();
    L_STR("[D3] PreScan done, hidden count=");
    L_DEC(gHiddenDeviceCount); L_NL();

    if (gHiddenDeviceCount > 0) {
        UINTN hi;
        IoWrite8(POST_PORT, POST_LINK_DISABLE_DONE);
        for (hi = 0; hi < gHiddenDeviceCount; hi++) {
            L_STR("[D5] hidden["); L_DEC(hi); L_STR("] dev=");
            L_HEX(gHiddenDevices[hi].DevBus, 2); L_STR(":");
            L_HEX(gHiddenDevices[hi].DevDev, 2); L_STR(".");
            L_HEX(gHiddenDevices[hi].DevFunc, 1);
            L_STR(" bridge=");
            L_HEX(gHiddenDevices[hi].BridgeBus, 2); L_STR(":");
            L_HEX(gHiddenDevices[hi].BridgeDev, 2); L_STR(".");
            L_HEX(gHiddenDevices[hi].BridgeFunc, 1);
            L_STR(" bridgeVidDid=");
            L_HEX(gHiddenDevices[hi].BridgeVid, 4); L_STR(":");
            L_HEX(gHiddenDevices[hi].BridgeDid, 4);
            L_STR(" pcieCap=");
            L_HEX(gHiddenDevices[hi].PcieCapOffset, 2);
            L_STR(" origLnkCtrl=");
            L_HEX(gHiddenDevices[hi].OrigLinkCtrl, 4); L_NL();
        }
        DEBUG((DEBUG_INFO,
            "ReBarDXE: %d devices hidden via Link Disable [POST D5]\n",
            (UINT32)gHiddenDeviceCount));
    }

    // 4. Register ExitBootServices callback (re-enables PCIe links)
    status = gBS->CreateEvent(
        EVT_SIGNAL_EXIT_BOOT_SERVICES,
        TPL_CALLBACK,
        OnExitBootServices,
        NULL,
        &gExitBootServicesEvent);

    if (EFI_ERROR(status)) {
        DEBUG((DEBUG_ERROR,
            "ReBarDXE: ERROR creating ExitBootServices event: %r\n", status));
    }

    // 5. Read ReBarState
    status = gRT->GetVariable(L"ReBarState", &reBarStateGuid,
        &attributes, &bufferSize, &reBarState);
    if (status != EFI_SUCCESS)
        reBarState = 0;

    if (reBarState) {
        status = gRT->GetTime(&time, NULL);
        if (!EFI_ERROR(status) && time.Year < BUILD_YEAR) {
            reBarState = 0;
            bufferSize = 1;
            attributes = EFI_VARIABLE_NON_VOLATILE |
                         EFI_VARIABLE_BOOTSERVICE_ACCESS |
                         EFI_VARIABLE_RUNTIME_ACCESS;
            gRT->SetVariable(L"ReBarState", &reBarStateGuid,
                attributes, bufferSize, &reBarState);
        }
    }

    DEBUG((DEBUG_INFO, "ReBarDXE: ReBarState = %u\n", reBarState));

    // 6. PreprocessController hook: register a notification for when
    //    the ResourceAllocation protocol becomes available.
    //    If the module loads before PciBus, the protocol does not exist
    //    yet — the callback will intercept it at the right time.
    status = gBS->CreateEvent(
        EVT_NOTIFY_SIGNAL,
        TPL_CALLBACK,
        OnResAllocProtocolNotify,
        NULL,
        &gResAllocNotifyEvent);

    if (!EFI_ERROR(status)) {
        status = gBS->RegisterProtocolNotify(
            &gEfiPciHostBridgeResourceAllocationProtocolGuid,
            gResAllocNotifyEvent,
            &gResAllocNotifyReg);

        if (EFI_ERROR(status)) {
            DEBUG((DEBUG_ERROR, "ReBarDXE: ERROR RegisterProtocolNotify: %r\n", status));
        } else {
            DEBUG((DEBUG_INFO, "ReBarDXE: Waiting for ResourceAllocation protocol...\n"));
            // Try now in case the protocol already exists
            OnResAllocProtocolNotify(gResAllocNotifyEvent, NULL);
        }
    }

    IoWrite8(POST_PORT, POST_HOOK_REGISTERED);
    L_STR("[D6] rebarInit complete, awaiting ExitBootServices\n");
    // Early flush: safety net if the ExitBS flush fails
    // The second flush at OnExitBootServices overwrites with complete data
    LogFlushToNvram();
    DEBUG((DEBUG_INFO, "ReBarDXE: ===== INIT COMPLETE ===== [POST D6]\n"));
    return EFI_SUCCESS;
}

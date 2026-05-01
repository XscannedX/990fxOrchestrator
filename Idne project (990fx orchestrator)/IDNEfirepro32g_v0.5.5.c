/*
Copyright (c) 2022-2023 xCuri0 <zkqri0@gmail.com>
SPDX-License-Identifier: MIT

MODULE: IDNEfirepro32g (v0.5.5) -- fork of 990fxOrchestrator v9.8 (en-US release)
Target: Gigabyte GA-990FX-Gaming Rev 1.1 + AMD FX + AMD FirePro S9170 32GB
        (VID:DID 1002:67A0, subsys 1002:031F = modded W9100 identity)

================================================================================
PURPOSE
================================================================================

UEFI DXE driver that enables Resizable BAR (ReBAR) on the AMD FirePro
S9170/W9170 GPU on a Gigabyte GA-990FX-Gaming Rev 1.1 motherboard with
stock AMI Aptio IV BIOS. The board does not natively support Above-4G
Decoding, so this driver performs all work needed to expose the GPU's
full 32 GB of VRAM (or 16 GB clamped, see v0.5.3) through a 64-bit
prefetchable BAR mapped above 4 GB.

This is a hardware-specific fork; it does NOT generalize to other
motherboards without porting work. See git history of the upstream
xCuri0/ReBarUEFI project for the original 4-stage architecture.

================================================================================
ARCHITECTURE (5+1 stages)
================================================================================

Stage 1 -- DSDT patch (in-memory)
    Sets MALH = One in DSDT to enable a 64-bit MMIO window in ACPI.
    Without this, OS will not allocate above 4 GB even after the BIOS
    PCI enumerator has finished.

Stage 2 -- CPU NB MMIO Window 1 (D18F1)
    Programs Address Map function 1 (D18F1 reg 0x88/0x8C/0x188/0x18C)
    to route 0x100000000-0x4000000000 (4 GB to 256 GB) through the
    CPU Northbridge to PCIe. Done twice: once during DXE, again during
    OnExitBootServices (force=TRUE) because Aptio sometimes overwrites
    the DXE programming.

Stage 3 -- Pre-scan and Link Disable
    Scans bus 0 for endpoints with >4 GB BAR requests. Disables PCIe
    link on the upstream bridge to hide them from the BIOS PCI
    enumerator (which cannot place them otherwise). Re-enabled in
    OnExitBootServices.
    Special case (v0.5.1+): the W9170 (1002:67A0) is hidden
    PREVENTIVELY on any slot, regardless of BAR size, to prevent
    Aptio MMIO allocation failure on SR5690 root ports (PCIE1/PCIE2).

Stage 4 -- PreprocessController hook
    Backup path for any device discovered after the pre-scan, plus
    the 990FX bridge quirk (DID 0x5a16 / 0x5a1c) that some BIOS
    revisions misconfigure for 64-bit prefetch.

Stage 5 -- ExitBootServices
    a. NB MMIO Window 1 final programming (force=TRUE).
    b. SR5690 HT Gate (B0:D0.0 reg 0x94 = 0x03, opens 64-bit MMIO).
    c. 990FX bridge quirk for all bridges (64-bit prefetchable).
    d. Re-enable PCIe links on hidden devices (Link Disable cleared).
    e. Link training poll (500 ms timeout) + manual BAR programming
       above 4 GB + bridge windows that avoid PNP conflicts.
    All 64-bit routing is activated only at this point, just before
    OS handoff.

================================================================================
VERSION HISTORY (most recent first)
================================================================================

v0.5.5 (2026-05-02) -- English release for upstream
    Translation pass of the file header narrative. No functional
    changes from v0.5.4. See UPSTREAM_NOTES.md for the BUG 2
    regression report submitted to the Banjo5k fork.

v0.5.4 (2026-05-01) -- Revert BUG 2 fix (regression on Gigabyte 990FX)
    Reverted the "BUG 2 FIX" imported from the upstream Banjo5k v9.6
    fork. The fix is theoretically correct (PCI extended config space
    register addressing for offset >= 0x100), but on Gigabyte
    GA-990FX-Gaming Rev 1.1 the correctly-routed write to D18F1 reg
    0x188 clobbers a non-zero value pre-programmed by Aptio IV during
    early init, breaking BIOS POST (hangs at code AB / Setup Idle,
    USB input dead). The pre-fix code accidentally wrote to D18F2 reg
    0x88 (DRAM Controller area), which on this platform is benign.

    Empirical confirmation: idne053 (with fix) hangs at AB; idne054
    (without fix) boots normally.

    All other Banjo5k v9.6 fixes are kept (BUG 1 / 3 / 4 / 5 / 6).

v0.5.3 (2026-04-27 evening) -- REBAR clamp 16 GB (diagnostic)
    REBAR_CTRL forced to idx=0xE (16 GB) instead of idx=0xF (32 GB).
    Diagnostic build to test whether the kernel `ring gfx test failed
    -110` observed at full 32 GB BAR is BAR-size dependent.
    Trade-off: Vulkan staging buffers >16 GB will hit GTT overflow.
    Restoring 32 GB BAR requires either kernel patch (port radeon's
    gart_table_ram_alloc pattern) or VBIOS shrink (declared VRAM <
    BAR window).

v0.5.2 (2026-04-27 mid) -- 6 bug fixes from upstream Banjo5k v9.6
    Imported the deep-dive bug scan PR #1 from
    github.com/Banjo5k/990fxOrchestrator-personal:
      BUG 1 (HIGH):  gBS->Stall() in ExitBS callback chain (UEFI spec
                     violation) -> replaced with IdneBusyWaitUs()
                     based on port 0x80 reads (POST checkpoint port =
                     ~1 us per read, stable across CPU clock states)
      BUG 2 (MED):   PCI offset overflow in ProgramCpuMmioWindow for
                     extended config offsets (>= 0x100) -> REVERTED
                     in v0.5.4 due to platform-specific regression
      BUG 3 (LOW):   pciFindExtCapEcam() infinite loop on corrupt
                     cap-list -> added TTL counter
      BUG 4 (LOW):   pciRebar* helpers used uninitialized variables
                     on read failure -> init + EFI_ERROR check
      BUG 5 (LOW):   reBarSetupDevice() discarded HandleProtocol
                     return value -> status check + early return
      BUG 6 (TRIVIAL): Intel ReBAR log strings hardcoded ".0" for
                     root port function -> use rootFunc with %x

v0.5.1 (2026-04-27) -- Slot flexibility for W9170
    Added preventive Link Disable hide for the W9170 (VID:DID
    1002:67A0) on ANY slot, not just slots with detected >4 GB BAR.
    This is required for SR5690 root ports (PCIE1/PCIE2 = CPU-direct
    slots), where Aptio IV would attempt MMIO allocation before our
    DXE module programs NB MMIO Window 1 + HT gate, and fails.
    On the SB900 chipset bridge (riser slot, bus 0x23) the preventive
    hide is harmless -- that bridge has autonomous routing. New
    marker [DT] in NVRAM log shows where the W9170 was discovered.

v0.5 (2026-04-26 evening) -- Bridge extension + remove SCRATCH_7 seed
    W9170_BRIDGE_LIMIT extended from 0x28007FFFFF to 0x28FFFFFFFF
    (+1 GB above BAR2). New marker [DR] for post-write bridge limit
    readback. Removed the bit 9 SCRATCH_7 seed (was a workaround for
    S9170-native VBIOS broken-MC; with W9170-modded VBIOS in chip,
    kernel ATOM AsicInit programs MC correctly on its own).

v0.4 (2026-04-26) -- SCRATCH_7 offset corrected (Hawaii bif_4_1_d.h)
    Pre-v0.4: byte offset 0xF2C derived from mmBIOS_SCRATCH_0 = 0x3C4
    (SI/older ASIC) -- wrong for Hawaii, wrote to an unrelated register.
    v0.4: byte offset 0x1740 from mmBIOS_SCRATCH_7 = 0x5d0 dword
    (CIK/Hawaii per amd/include/asic_reg/bif/bif_4_1_d.h:151).
    Note: in v0.5+ this seed is no longer programmed; kept here as
    historical reference.

v0.3 (2026-04-23) -- Initial SCRATCH_7 seed attempt (wrong offset)
v0.2 (2026-04-22) -- Minimal silicon override to 32 GB
v0.1 (2026-04-22) -- First fork from 990fxOrchestrator v9.8
    Goal: enable full 32 GB ReBAR for FirePro S9170 with patched VBIOS
    (243673_fb2000.rom: FB_LOCATION 0xF400 -> 0x2000 to match the
    bridge window allocated by this DXE module).

================================================================================
HARDWARE NOTES
================================================================================

* Motherboard: Gigabyte GA-990FX-Gaming Rev 1.1
* Chipset: SR5690 + SP5100 (legacy AMD)
* CPU: AMD FX-8350 / FX-8320 / FX-6300 (Family 15h Bulldozer/Piledriver)
* GPU under test: AMD FirePro S9170 32 GB GDDR5 (Hawaii GL, OEM SSID 0x0735)
                  with VBIOS patched to declare W9100 identity (0x031F) +
                  FB_LOCATION = 0x2000 (matches our bridge target)

The Hawaii silicon PCIe IP accepts REBAR idx 0xF (32 GB) even though
VBIOS REBAR_CAP declares max idx 0xE (16 GB). Validated empirically
since v0.2 / v9.6 of the upstream module.

================================================================================
DIAGNOSTIC MARKERS (NVRAM log -- UEFI variable)
================================================================================

The driver writes a per-boot diagnostic log to a UEFI variable
(see L_STR / LogStr macros). Read from Linux via:
    sudo dd if=/sys/firmware/efi/efivars/ReBarBootLog-* bs=1 skip=4 \
        | strings | grep -E '\[D[0-9A-Z]\]'

Marker prefix legend:
    [D0..D9]  Initialization phases (DXE entry, DSDT patch, NB MMIO,
              etc.)
    [DA]      SR5690 HT gate quirk
    [DB]      990FX bridge pref64 quirk
    [DC]      Intel Arc ReBAR (BAR2 -> 16 GB)
    [DE]      W9170 ReBAR (BAR0 -> 32 GB or 16 GB depending on version)
    [DF]      W9170 ReBAR completion summary
    [DR]      v0.5+: bridge limit post-write readback
    [DT]      v0.5.1+: W9170 location at PreScan time

================================================================================
BUILD
================================================================================

EDK2 (Curiopersonalizzato fork), Visual Studio 2026 Insiders + VS2022
toolchain. Build script: build_idne.bat (in build/ directory).
FFS assembly: gen_ffs.py. Final injection into BIOS image: MMTool
4.50.0.23 (Replace by GUID adf0508f-a992-4a0f-8b54-0291517c21aa).

CRITICAL: never use UEFIPatch output directly for flashing -- it tends
to corrupt the Pad File following the modified module. Always re-inject
via MMTool 4.50.0.23.

================================================================================
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
// CUSTOM POST CODES — visible on the motherboard LED display
// =====================================================================
// Range 0xD0-0xDF reserved for our module. Do not conflict with
// the standard AMI Aptio IV codes. Written to I/O port 0x80.
//
// Normal boot sequence:
//   D0 → D1 → D2 → D3 → D5 → ... → D8 → D9 → DA → DB → DC → DD
//
// If the display stays stuck on a code, that step has failed.
// The Dx codes are overwritten by the BIOS after our module.
//
#define POST_PORT               0x80

#define POST_MODULE_LOADED      0xD0  // rebarInit() started
#define POST_DSDT_PATCH         0xD1  // patching MALH in DSDT
#define POST_NB_MMIO_WINDOW     0xD2  // CPU NB D18F1 Window 1 programming
#define POST_PRESCAN_START      0xD3  // PCI bus pre-scan start
#define POST_LINK_DISABLE_DONE  0xD5  // devices hidden via Link Disable
#define POST_HOOK_REGISTERED    0xD6  // PreprocessController hook ready
#define POST_EXITBS_ENTER       0xD8  // ExitBootServices callback
#define POST_EXITBS_NB_WINDOW   0xD9  // NB MMIO Window forced at ExitBS
#define POST_EXITBS_HT_GATE     0xDA  // SR5690 HT gate opened
#define POST_EXITBS_LINK_EN     0xDB  // PCIe links re-enabled
#define POST_EXITBS_BAR_PROG    0xDC  // BARs programmed above 4 GB
#define POST_EXITBS_DONE        0xDD  // handoff to Linux complete
#define POST_FULL_COMPLETE      0xFC  // FC = "Full Complete" — module active, all OK
// POST_EXITBS_AMD_REBAR (0xDE) e POST_AMD_REBAR_DONE (0xDF) — new in v9
#define POST_EXITBS_AMD_REBAR   0xDE  // v9: ResizeAmdGpuBars enter (W9170)
#define POST_AMD_REBAR_DONE     0xDF  // v9: ResizeAmdGpuBars exit (success or fail, see log)
#define POST_ERROR_GENERIC      0xEE  // generic module error
#define POST_ERROR_LINK_TRAIN   0xEF  // device not responding after link training

// v9: ENABLE_BEEP_MELODY = 1 → play PlayFcMelody() before POST 0xFC.
// Requires populated SPEAKER header on the motherboard (GA-990FX-Gaming: SPK_1 4-pin).
#define ENABLE_BEEP_MELODY      1

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
#define PCIE_LINK_CONTROL_OFFSET    0x10   // offset dal PCIe cap
#define PCIE_LINK_DISABLE           (1 << 4)

#define MAX_HIDDEN_DEVICES          16

// ECAM (Enhanced Configuration Access Mechanism) per extended config space
// On AMD 990FX the ECAM base is at 0xE0000000 (confirmed by dmesg/PNP)
// Used to access the PCIe Extended Capabilities (offset > 0xFF)
// such as Resizable BAR (Cap ID 0x0015)
#define ECAM_BASE                   0xE0000000ULL
// PCI_EXT_CAP_ID_REBAR gia' definito in pciRegs.h

// =====================================================================
// v9.3 LAYOUT MMIO64 — PLACEMENT ESPLICITO (noi decidiamo)
// =====================================================================
// Constant target addresses for every device the module handles.
// Rationale: avoid any overlap with BIOS allocations
// for the Tesla P100 cards (which work and we don't touch) + bit-for-bit determinism
// across reboots. All addresses fall within the DSDT M2MX window
// (0 - 0x3FFFFFFFFF, 256 GB — requires PatchM2MX.txt v2 on the BIOS).
//
//   [BIOS, untouched]  P100 #1 BAR1  0x800000000  .. 0xBFFFFFFFF   (16GB)
//   [BIOS, untouched]  P100 #1 BAR3  0xC00000000  .. 0xC01FFFFFF   (32MB)
//   [BIOS, untouched]  P100 #2 BAR1  0x1000000000 .. 0x13FFFFFFFF  (16GB)
//   [BIOS, untouched]  P100 #2 BAR3  0x1400000000 .. 0x1401FFFFFF  (32MB)
//   [OURS]             Arc A770 BAR2 0x1800000000 .. 0x1BFFFFFFFF  (16GB)
//   [gap v9.7]                         0x1C00000000 .. 0x1FFFFFFFFF  (4GB, R/O bit padding)
//   [OURS v0.2]        W9170 BAR0    0x2000000000 .. 0x27FFFFFFFF  (32GB, 32-GB-aligned)
//   [OURS v0.2]        W9170 BAR2    0x2800000000 .. 0x28007FFFFF  (8MB)
//   [spare]                            0x2800800000 .. 0x3FFFFFFFFF  (~100GB)
//
// Historical note v9.2: ARC was placed at 0xC00000000 e W9170 a 0x1400000000,
// both collided with the BAR3s (32MB 64-bit pref) delle due Tesla che
// occupano slot 16GB-allineati. Fix in v9.3: the whole OURS cluster moves up
// above 0x1800000000, where the BIOS naturally places the Arc.
//
// NOTE v9.6: W9170 BAR0 goes from 16 GB to 32 GB (the Hawaii silicon PCIe IP is
// 32 GB capable nonostante il VBIOS dichiari max 16 GB nel CAP bitmap —
// verified via setpci/readback 2026-04-22).
//
// NOTE v9.7: il target v9.6 0x1C00000000 era 16-GB-aligned ma NON
// 32-GB-aligned. PCIe spec: per BAR size=2^N i bits [N-1:0] del BAR
// register sono R/O (hard-wired 0). Per 32 GB (N=35), bit 34 is R/O.
// Scrivere 0x1C00000000 (bit 34 set) fa sì che l'hw lo clearì al
// readback -> kernel reads 0x1800000000 -> Arc bridge collision ->
// kernel fallback 16 GB. Fix: sposta a 0x2000000000 (bit 34 clear,
// first 32-GB boundary above Arc). BAR2 follows at 0x2800000000.
//
#define ARC_BAR2_TARGET          0x1800000000ULL   // Arc A770 BAR2 (VRAM) 16GB
#define W9170_BAR0_TARGET        0x2000000000ULL   // W9170 BAR0 32GB (v9.7: 32GB-aligned)
#define W9170_BAR2_TARGET        0x2800000000ULL   // W9170 BAR2 8MB (v9.7: shift da 0x2400000000)
// Bridge (root port verso W9170) must coprire BAR0 32GB + BAR2 8MB:
// base allineata a 1MB: 0x2000000000 -> base reg = 0x0000|0x01
// limit inclusiva 1MB:  0x28007FFFFF -> limit reg = 0x0070|0x01 (v9.7)
//   upper32 base  = 0x20, upper32 limit = 0x28 (v9.7: era 0x1C/0x24)
#define W9170_BRIDGE_BASE        0x2000000000ULL
// v0.5: extended +1GB above BAR2 to de-marginalize the GART table
// (a 0x27FFE00000 = top BAR0 - 2MB) dal limite assoluto del bridge.
// Hypothesis: SR5690 prefetch past GART at the bridge edge causes
// UR -> MC freeze. v0.4 limit was 0x28007FFFFF (8 MB headroom).
// v0.5 limit 0x28FFFFFFFF gives 1 GB + 8 MB of prefetch buffer above BAR2.
#define W9170_BRIDGE_LIMIT       0x28FFFFFFFFULL   // v0.5: extended bridge for GART safety

// Fallback-only cursors: used ONLY by ProgramHiddenDeviceBars for
// non-whitelist devices (those hidden via Link Disable and not
// handled by the specific Resize* functions). For Arc and W9170 we use the constants
// above, NOT these cursors.
static UINT64 gNextPrefAddr = 0x800000000ULL;   // fallback 32GB start
static UINT64 gNextMmioAddr = 0x410000000ULL;   // fallback ~16.25GB start

// =====================================================================
// V6 LOGGER — writes a boot log to a UEFI NVRAM variable ReBarBootLog
// =====================================================================
// 16KB RAM buffer, single flush at end of OnExitBootServices via SetVariable.
// Readable from Linux: sudo cat /sys/firmware/efi/efivars/ReBarBootLog-<GUID>
// Minimal implementation without PrintLib to avoid touching dependencies .inf
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

// PCI config space dump (first 64 bytes) in format "BB:DD.F: XX XX XX ..."
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

// Flush buffer to the NVRAM variable. Call BEFORE disabling runtime services.
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
// STRUCTS AND GLOBAL VARIABLES
// =====================================================================

// For every hidden device, we save the parent bridge and its Link Control
typedef struct {
    UINT8                            DevBus;   // device bus (for log)
    UINT8                            DevDev;
    UINT8                            DevFunc;
    UINT8                            BridgeBus;  // upstream bridge
    UINT8                            BridgeDev;
    UINT8                            BridgeFunc;
    UINT8                            PcieCapOffset;  // bridge's PCIe cap
    UINT16                           OrigLinkCtrl;   // original Link Control value
    UINT16                           BridgeVid;      // bridge VID (for quirk at ExitBS)
    UINT16                           BridgeDid;      // bridge DID (for quirk at ExitBS)
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
// v0.5.2 BUG 1 FIX: forward decl of IdneBusyWaitUs/Ms -- used by
// ProgramHiddenDeviceBars (line ~1587) before their definition
// (line ~2317). Implementation: port-IO busy-loop, no boot-services dep.
static VOID IdneBusyWaitUs(UINTN us);
static VOID IdneBusyWaitMs(UINTN ms);

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

// Versions that take an explicit rootBridgeIo (for the pre-scan)
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
    UINT32 ctrl = 0;  // v0.5.2 BUG 4 FIX: init before the read

    // v0.5.2 BUG 4 FIX: check pciReadConfigDword return.
    // If it fails, ctrl stays 0 -> nbars=0 -> loop skipped and return -1.
    if (EFI_ERROR(pciReadConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl)))
        return -1;
    nbars = (ctrl & PCI_REBAR_CTRL_NBAR_MASK) >> PCI_REBAR_CTRL_NBAR_SHIFT;

    for (i = 0; i < nbars; i++, pos += 8) {
        UINTN bar_idx;
        if (EFI_ERROR(pciReadConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl)))
            return -1;
        bar_idx = ctrl & PCI_REBAR_CTRL_BAR_IDX;
        if (bar_idx == bar)
            return pos;
    }
    return -1;
}

UINT32 pciRebarGetPossibleSizes(UINTN pciAddress, UINTN epos, UINT16 vid, UINT16 did, UINT8 bar)
{
    INTN pos;
    UINT32 cap = 0;  // v0.5.2 BUG 4 FIX: init before the read

    pos = pciRebarFindPos(pciAddress, (INTN)epos, bar);
    if (pos < 0) return 0;

    // v0.5.2 BUG 4 FIX: check return; if it fails return 0 (no sizes).
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
    UINT32 ctrl = 0;  // v0.5.2 BUG 4 FIX: init before the read

    pos = pciRebarFindPos(pciAddress, (INTN)epos, bar);
    if (pos < 0) return pos;

    // v0.5.2 BUG 4 FIX: check return; if it fails return -1 instead of
    // writing modified garbage.
    if (EFI_ERROR(pciReadConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl)))
        return -1;
    ctrl &= (UINT32)~PCI_REBAR_CTRL_BAR_SIZE;
    ctrl |= (UINT32)size << PCI_REBAR_CTRL_BAR_SHIFT;

    pciWriteConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl);
    return 0;
}

// =====================================================================
// DEVICE HIDDEN CHECK (per PreprocessController)
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
// PRE-SCAN: FIND AND HIDE DEVICES WITH BAR > 4 GB
// =====================================================================
//
// Scan all PCI buses BEFORE PciBus performs enumeration.
// For every device found with a 64-bit prefetchable BAR > 4 GB:
//   1. Disable PCIe link on the upstream bridge (Link Disable)
//   2. The device physically disappears (VID=0xFFFF at hardware level)
//   3. Add it to the gHiddenDevices list for later restore
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

        // Upper DWORD has significant bits -> BAR > 4 GB
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
// FIND THE PCIe UPSTREAM BRIDGE OF A DEVICE
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
// Disable the PCIe link on the upstream bridge: the device disappears
// physically from the PCI bus. Both UEFI and legacy PCI BIOS (CSM via
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
            "ReBarDXE: Device %02x:%02x.%x - bridge upstream non trovato!\n",
            bus, dev, func));
        return;
    }

    bridgeAddr = EFI_PCI_ADDRESS(bridgeBus, bridgeDev, bridgeFunc, 0);

    // Read the bridge's VID/DID (needed for quirk at ExitBootServices)
    if (EFI_ERROR(pciReadDword(rbIo, bridgeAddr, &bridgeVidDid)))
        return;

    // Find the PCIe capability on the bridge
    pcieCap = pciFindCapabilityEx(rbIo, bridgeAddr, PCI_CAP_ID_PCIE);
    if (pcieCap == 0) {
        DEBUG((DEBUG_WARN,
            "ReBarDXE: Bridge %02x:%02x.%x senza PCIe cap\n",
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

    // Save info for restore
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
// Before PciBus, the PCIe bridges on bus 0 have no secondary bus
// assigned (AGESA PEI does no PCIe enumeration on desktop 990FX).
// Without bus numbers, devices behind the bridges are unreachable.
// We assign temporary bus numbers so we can scan devices behind them.
// PciBus will overwrite them during its enumeration.

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
                    "ReBarDXE: Bridge 00:%02x.%x gia' configurato sec=%d sub=%d\n",
                    dev, func, secBus, subBus));
                continue;
            }

            // Bridge with no secondary bus -- assign a temporary one
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

                // Program the bridge with the temporary bus
                pciWriteByte(rbIo, addr + 0x18, &priZero);   // primary = 0
                pciWriteByte(rbIo, addr + 0x19, &tempSec);   // secondary
                pciWriteByte(rbIo, addr + 0x1A, &tempSub);   // subordinate

                DEBUG((DEBUG_INFO,
                    "ReBarDXE: Bridge 00:%02x.%x -> bus temporaneo %d "
                    "(VID:DID=%04x:%04x)\n",
                    dev, func, tempSec,
                    (UINT16)(vidDid & 0xFFFF), (UINT16)(vidDid >> 16)));

                nextBus++;
            }
        }
    }

    return (UINT16)nextBus;
}

// Restore the original buses (0) on the bridges -- PciBus will reprogram them
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

    DEBUG((DEBUG_INFO, "ReBarDXE: Pre-scan avviata, %d root bridge trovati\n",
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
            "ReBarDXE: Enumerati %d bridge, scanning bus 0-%d\n",
            (UINT32)tempBridgeCount, maxBus - 1));

        // PHASE 2: Scan the assigned buses for devices with BAR > 4 GB
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
                        "ReBarDXE: Pre-scan trovato %02x:%02x.%x VID=%04x DID=%04x\n",
                        (UINT8)bus, dev, func, vid, (UINT16)(vidDid >> 16)));

                    // Display protection: do NOT hide the Intel GPU (Arc A770)
                    // The Arc is needed for video output during POST and in Linux.
                    // If we hide it, the BIOS has no VGA -> halt at 0d.
                    // NVIDIA (P100) and AMD (FirePro) GPUs get hidden.
                    if (vid == 0x8086) {
                        DEBUG((DEBUG_INFO,
                            "ReBarDXE: GPU Intel %02x:%02x.%x PROTETTA "
                            "(display card, non nascosta)\n",
                            (UINT8)bus, dev, func));
                        continue;
                    }

                    // v0.5.1: preventively hide W9170/S9170 (1002:67A0) on
                    // ANY slot. Required for CPU-direct slots
                    // (PCIE1/2 via SR5690 root port) where the Aptio BIOS may
                    // fail to allocate 64-bit pref MMIO before our
                    // DXE programs NB MMIO Window 1 + HT gate.
                    // On the SB900 riser this was unnecessary (the chipset bridge has
                    // autonomous routing) but is harmless.
                    if (vid == 0x1002 &&
                        (UINT16)(vidDid >> 16) == 0x67A0) {
                        L_STR("[DT] W9170 trovata @");
                        L_HEX((UINT8)bus, 2); L_STR(":");
                        L_HEX(dev, 2); L_STR(".");
                        L_HEX(func, 1);
                        L_STR(" -> hide preventiva (any-slot support v0.5.1+)\n");
                        DEBUG((DEBUG_INFO,
                            "ReBarDXE: W9170 trovata %02x:%02x.%x, "
                            "hide preventiva (any-slot support)\n",
                            (UINT8)bus, dev, func));
                        HideDeviceViaLinkDisable(rbIo, (UINT8)bus, dev, func);
                        continue;
                    }

                    // Check whether it has a BAR > 4 GB
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

    DEBUG((DEBUG_INFO, "ReBarDXE: Pre-scan completata, %d device nascosti\n",
        (UINT32)gHiddenDeviceCount));
}


// =====================================================================
// BAR AND BRIDGE PROGRAMMING POST LINK-ENABLE
// =====================================================================
// After restoring the PCIe link, the device appears with BARs at zero
// (never programmed by the BIOS because it was hidden). If Linux sees a BAR
// at 0x00000000 with size 16 GB, it creates a PNP conflict with system
// resources (RAM, ECAM, APIC). PNP resources get disabled and
// the NVIDIA driver fails with rm_init_adapter.
//
// This function programs BARs to valid addresses above 4 GB and
// configures the upstream bridge's memory windows. Linux pci=realloc
// can then move things wherever it wants, but without PNP conflicts.

// Multi-GPU address layout (above 4GB, within NB Window 256GB):
//   0x410000000 -  0x4FFFFFFFF : non-prefetchable (BAR0 regs, ~4 GB)
//   0x800000000 -  0xBFFFFFFFF : P100 #1 BAR1 16GB pref
//   0xC00000000 -  0xFFFFFFFFF : P100 #2 BAR1 16GB pref
//   0x1000000000 - 0x13FFFFFFFF : Arc A770 BAR 16GB pref
//   0x1400000000 - 0x1BFFFFFFFF : FirePro BAR 32GB pref
//   (the static cursors assign sequentially, aligned to size)
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

    // Uses global cursors (shared with ResizeIntelGpuBars)

    UINT64 prefLow = 0, prefHigh = 0;
    UINT64 mmioLow = 0, mmioHigh = 0;
    BOOLEAN hasPref = FALSE, hasMmio = FALSE;

    // Read current secondary bus (assigned by PciBus during enumeration)
    if (EFI_ERROR(pciReadByte(rbIo, bridgeAddr + 0x19, &secBus)) || secBus == 0) {
        DEBUG((DEBUG_WARN,
            "ReBarDXE: Bridge %02x:%02x.%x sec bus=%d, BAR programming skipped\n",
            entry->BridgeBus, entry->BridgeDev, entry->BridgeFunc, secBus));
        return;
    }

    devAddr = EFI_PCI_ADDRESS(secBus, entry->DevDev, entry->DevFunc, 0);

    // Wait for link training -- poll VID with 500 ms timeout
    // v0.5.2 BUG 1 FIX: gBS->Stall not guaranteed post-ExitBootServices.
    // This function is called from WakeAllHiddenDevices in OnExitBS.
    for (retries = 0; retries < 500; retries++) {
        IdneBusyWaitMs(1);  // 1 ms busy-wait, no boot-services dependency
        if (!EFI_ERROR(pciReadDword(rbIo, devAddr, &vidDid))) {
            if ((vidDid & 0xFFFF) != 0xFFFF && (vidDid & 0xFFFF) != 0)
                break;
        }
    }

    if ((vidDid & 0xFFFF) == 0xFFFF || (vidDid & 0xFFFF) == 0) {
        IoWrite8(POST_PORT, POST_ERROR_LINK_TRAIN);
        DEBUG((DEBUG_WARN,
            "ReBarDXE: Device %02x:%02x.%x non risponde dopo %d ms! "
            "[POST DF]\n",
            secBus, entry->DevDev, entry->DevFunc, (UINT32)retries));
        return;
    }

    IoWrite8(POST_PORT, POST_EXITBS_BAR_PROG);
    DEBUG((DEBUG_INFO,
        "ReBarDXE: Device %02x:%02x.%x online dopo %d ms "
        "(VID:DID=%04x:%04x)\n",
        secBus, entry->DevDev, entry->DevFunc, (UINT32)retries,
        (UINT16)(vidDid & 0xFFFF), (UINT16)(vidDid >> 16)));

    // Scan and program each BAR
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

            // Size probe -- low DWORD
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

            // Assign address above 4 GB, aligned to size
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

            // Write BAR with assigned address
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

    // Program the bridge's prefetchable window (64-bit)
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

    // Program the bridge's non-prefetchable window (if used)
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
        "ReBarDXE: Device %02x:%02x.%x BAR programming completo\n",
        secBus, entry->DevDev, entry->DevFunc));
}


// =====================================================================
// RE-ENABLE PCIe LINK ON HIDDEN DEVICES
// =====================================================================
// Restore the original Link Control value on the upstream bridge,
// re-enabling the PCIe link. After link training, program BARs
// and bridge windows to avoid PNP conflicts on Linux.

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

        // 990FX bridge quirk: ensure the upstream bridge supports
        // 64-bit prefetchable BEFORE re-enabling the link.
        // PciBus may have overwritten the bit during enumeration.
        // Linux needs to see 64-bit prefetchable to allocate above 4 GB.
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
                "ReBarDXE: Quirk 990FX bridge %02x:%02x.%x riapplicato "
                "(prefetchable 64-bit)\n",
                gHiddenDevices[i].BridgeBus, gHiddenDevices[i].BridgeDev,
                gHiddenDevices[i].BridgeFunc));
        }

        // Restore original Link Control (without Link Disable bit)
        pciWriteWord(rbIo,
            bridgeAddr + gHiddenDevices[i].PcieCapOffset + PCIE_LINK_CONTROL_OFFSET,
            &linkCtrl);

        DEBUG((DEBUG_INFO,
            "ReBarDXE: === LINK RIABILITATO === device %02x:%02x.%x "
            "via bridge %02x:%02x.%x (LinkCtrl -> 0x%04x)\n",
            gHiddenDevices[i].DevBus, gHiddenDevices[i].DevDev,
            gHiddenDevices[i].DevFunc,
            gHiddenDevices[i].BridgeBus, gHiddenDevices[i].BridgeDev,
            gHiddenDevices[i].BridgeFunc, linkCtrl));

        // Wait for link training and program BAR/bridge to avoid
        // Linux seeing BAR at 0x00000000 (fatal PNP conflict)
        ProgramHiddenDeviceBars(&gHiddenDevices[i]);
    }
}

// =====================================================================
// ECAM ACCESS -- EXTENDED CONFIG SPACE (OFFSET > 0xFF)
// =====================================================================
// The PCI Root Bridge IO protocol only supports offsets 0-255 (standard
// config space). Le PCIe Extended Capabilities (such as Resizable BAR)
// are at offset 0x100+. We access them via ECAM (memory-mapped config space).

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
    // v0.5.2 BUG 3 FIX: TTL counter to prevent infinite loop on a cap-list
    // corrupted (cycle e.g. cap@0x100 next=0x200, cap@0x200 next=0x100).
    // Realistic max: (PCI_CFG_SPACE_EXP_SIZE - PCI_CFG_SPACE_SIZE) / 8
    // = (0x1000 - 0x100) / 8 = 480 theoretical entries.
    INTN ttl = (0x1000 - 0x100) / 8;

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
// REBAR: RESIZE BAR ON INTEL GPU (ARC A770) A EXITBOOTSERVICES
// =====================================================================
// The Arc A770 is not hidden (Intel VID filter) -- the BIOS allocates it
// with a 256 MB BAR below 4 GB (display works during POST). At ExitBS
// we use the PCIe Resizable BAR capability to expand the BAR to 16 GB
// and relocate it above 4 GB. Linux will see the BAR at full size.
//
// TOPOLOGY v9.5 (post-swap Arc -> riser, 2026-04-21):
//   00:15.3 (SB900 SR5690 root port, DID 0x43A3, **function 3**)
//     23:00.0 (Intel PCIe switch upstream 4fa0)
//       24:01.0 (Intel PCIe switch downstream 4fa4)
//         25:00.0 (Arc A770, VID 8086:56a0)       <- ReBAR endpoint @ 0x420
//
// v9.4 BUG: the outer root-port loop scanned only **function 0** di
// of each dev (EFI_PCI_ADDRESS(0, rootDev, 0, 0)), therefore 00:15.3 (func 3)
// was never visited. the Arc on the riser stayed invisible to ReBAR,
// BAR 256MB. Same bug already fixed in v7 for Apply990FxBridgeQuirkAll
// and in v9.4 for ResizeAmdGpuBars.
//
// FIX v9.5: outer loop now scans dev 0..31 x func 0..7, with early-exit
// standard multi-function (header bit 0x80).
//
// The bridge chain (root port + intermediate switches) is reprogrammed
// with 64-bit prefetchable windows (pref base/limit bit0=1 + upper32).

static VOID ResizeIntelGpuBars(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo)
{
    UINT8 rootDev, rootFunc;
    BOOLEAN gpuProcessed = FALSE;

    DEBUG((DEBUG_INFO, "ReBarDXE: === Ricerca GPU Intel per ReBAR v9.5 ===\n"));
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
            if (rootFunc == 0) break;  // device absent, salta everything dev
            continue;
        }
        if (EFI_ERROR(pciReadByte(rbIo, rpAddr + 0x0E, &rpHdr)))
            continue;
        if ((rpHdr & 0x7F) != 1) {
            // Non-bridge: se func0 non multi-function, salta dev
            if (rootFunc == 0 && (rpHdr & 0x80) == 0) break;
            continue;
        }
        pciReadByte(rbIo, rpAddr + 0x19, &rpSecBus);
        pciReadByte(rbIo, rpAddr + 0x1A, &rpSubBus);
        if (rpSecBus == 0)
            continue;

        // Scan ALL the buses under this root port (sec..sub)
        // to find Intel display endpoints with ReBAR
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
                    continue;  // Only Intel

                pciReadByte(rbIo, devAddr + 0x0E, &dHdr);
                if ((dHdr & 0x7F) != 0)
                    continue;  // Only endpoint (tipo 0), salta bridge/switch

                pciReadByte(rbIo, devAddr + 0x0B, &cls);
                if (cls != 0x03)
                    continue;  // Only display controller (classe 03)

                // GPU Intel endpoint trovata!
                rebarOff = pciFindExtCapEcam(rbIo, bus, d, 0,
                    PCI_EXT_CAP_ID_REBAR);
                if (rebarOff == 0) {
                    DEBUG((DEBUG_WARN,
                        "ReBarDXE: GPU Intel %02x:%02x.0 senza ReBAR\n",
                        bus, d));
                    continue;
                }

                // v0.5.2 BUG 6 FIX: root port "00:%02x.0" hardcoded
                // function 0, wrong post-v9.5 with multi-function support
                // (e.g. SB900 riser is 00:15.3). Now uses the real rootFunc.
                DEBUG((DEBUG_INFO,
                    "ReBarDXE: GPU Intel %02x:%02x.0 (DID=%04x) "
                    "ReBAR @ 0x%x — catena da root port 00:%02x.%x\n",
                    bus, d, (UINT16)(dVidDid >> 16), rebarOff,
                    rootDev, rootFunc));

                // Numero BAR ridimensionabili (bits [7:5] del primo ctrl)
                if (EFI_ERROR(pciReadDwordEcam(rbIo, bus, d, 0,
                        rebarOff + 0x08, &rebarCtrl0)))
                    continue;
                numBars = (UINT8)((rebarCtrl0 >> 5) & 0x07);
                if (numBars == 0)
                    numBars = 1;

                DEBUG((DEBUG_INFO,
                    "ReBarDXE: %d BAR ridimensionabili\n", numBars));

                // 1. Disable Memory Space sul device
                {
                    UINT16 cmd = 0;
                    UINT16 cmdOff;
                    pciReadWord(rbIo, devAddr + 0x04, &cmd);
                    cmdOff = cmd & 0xFFF9;  // Clear MemSpace(1) + BusMaster(2)
                    pciWriteWord(rbIo, devAddr + 0x04, &cmdOff);
                }

                // 2. Per each BAR ridimensionabile: finds max, resize, assegna
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

                    // Trova size massima supported
                    // cap bits[31:4]: bit N → size 2^(N+20)
                    // N=8 → 256MB, N=14 → 16GB
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

                    // Program BAR with address above 4GB
                    barOff = 0x10 + barIndex * 4;
                    pciReadDword(rbIo, devAddr + barOff, &origLo);

                    if ((origLo & 0x06) != 0x04) {
                        DEBUG((DEBUG_WARN,
                            "ReBarDXE: BAR%d e' 32-bit, skip\n", barIndex));
                        continue;
                    }

                    pciReadDword(rbIo, devAddr + barOff + 4, &origHi);

                    // v9.3: placement esplicito. La VRAM della Arc is BAR2
                    // (non BAR0: BAR0 sono MMIO register 16MB). Spostiamo
                    // only BAR2 in alto a ARC_BAR2_TARGET (16GB). Altri BAR
                    // stay with the BIOS assignment.
                    // NOTA v9.3: this fix il BUG-C di v9.2 (filtro
                    // barIndex != 0 saltava la vera VRAM).
                    if (barIndex != 2) {
                        DEBUG((DEBUG_INFO,
                            "ReBarDXE: Arc BAR%d skip resize "
                            "(v9.3: solo BAR2 gestita esplicitamente)\n",
                            barIndex));
                        continue;
                    }
                    if ((origLo & 0x08) == 0) {
                        DEBUG((DEBUG_WARN,
                            "ReBarDXE: Arc BAR2 non e' pref — skip\n"));
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

                    // Traccia range prefetchable per bridge
                    if (origLo & 0x08) {
                        if (prefStart == 0 || assignAddr < prefStart)
                            prefStart = assignAddr;
                        if (assignAddr + newSize > prefEnd)
                            prefEnd = assignAddr + newSize;
                    }
                }

                // 3. Riabilita Memory Space + Bus Master sul device
                {
                    UINT16 cmd = 0;
                    pciReadWord(rbIo, devAddr + 0x04, &cmd);
                    cmd |= 0x0006;
                    pciWriteWord(rbIo, devAddr + 0x04, &cmd);
                }

                // ====================================================
                // 4. RIPROGRAMMA TUTTA LA CATENA DI BRIDGE
                // ====================================================
                // Ogni bridge dal root port al GPU must avere:
                //   - Prefetchable base/limit tipo 64-bit (bit 0 = 1)
                //   - Range che copre all i BAR prefetchable assegnati
                //   - Bus Master + Memory Space abilitati
                //
                // Criteri per identificare i bridge nel percorso:
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
                        "ReBarDXE: Root 00:%02x.0 pref -> "
                        "0x%lx - 0x%lx (64-bit)\n",
                        rootDev, prefStart, prefEnd - 1));

                    // 4b. Bridge intermedi (bus rpSecBus .. gpuBus-1)
                    // Scan each bus nell'intervallo per bridge
                    // il cui range sec..sub contiene il bus del GPU
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

                                // Only bridge nel percorso verso il GPU
                                if (bus < bSec || bus > bSub)
                                    continue;

                                // Finestra prefetchable 64-bit
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
                    "ReBarDXE: GPU Intel %02x:%02x.0 ReBAR completato "
                    "(root 00:%02x.%x)!\n", bus, d, rootDev, rootFunc));
            }
        }
    }
    }  // close rootFunc

    if (!gpuProcessed) {
        L_STR("[DC] GPU Intel (8086 cls 03) NOT FOUND su alcun root port/func\n");
    }
}


static VOID Apply990FxBridgeQuirkAll(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL* rbIo)
{
    // Scan bus 0 (ALL functions, not only 0) to find ALL the
    // bridge AMD 1002: SR5690 NB root port (5a16/5a18/5a1c/5a1f) e
    // SB900 SB root port (43A3 -- v7, used by the W9100 riser) and enable
    // prefetchable 64-bit. Serve per Linux pci=realloc.
    //
    // v7: il SB900 root port vive a 00:15.3 (function 3), NON function 0 —
    // il loop precedente saltava this bridge.
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
                // Nessun device a this addr: se func==0, salta everything il dev
                if (func == 0) break;
                continue;
            }
            if (vid != 0x1002) {
                // Non-AMD: se func0 non è multi-function, salta il dev
                if (func == 0) {
                    if (EFI_ERROR(pciReadByte(rbIo, addr + 0x0E, &headerType)))
                        break;
                    if ((headerType & 0x80) == 0) break;  // non multi-function
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

            // Verify che sia un bridge (header type 1)
            if (EFI_ERROR(pciReadByte(rbIo, addr + 0x0E, &headerType)))
                continue;
            if ((headerType & 0x7F) != 1)
                continue;

            if (did == 0x43A3) {
                // v9: SB900 root port (00:15.3) — qui NON tocchiamo base/limit/upper32.
                // The v8 branch (0x14/0x1B -> window 0x1400000000) caused conflict with
                // Arc a 0x1800000000 (kernel decodificava la base come 0x1440000000,
                // dmesg: "address conflict with PCI Bus 0000:14").
                // In v9 la window 00:15.3 è programmata da ResizeAmdGpuBars() AFTER
                // ResizeIntelGpuBars, a gNextPrefAddr=0x1C00000000 (fine Arc).
                // The 0x43A3 entry stays whitelisted because other parts of the module
                // (ReEnableAllHiddenDevices, reBarSetupDevice) use la same lista.
                L_STR("[DB] v9 00:15.3 43a3 detected — window delegata a ResizeAmdGpuBars\n");
            } else {
                // v9.1: per 5a16/5a1c marchiamo only bit0=1 su Base/Limit
                // (dichiarazione "support prefetch 64-bit").
                // NON azzeriamo piu' upper32 (0x28/0x2C) — farlo schiacciava la
                // pref window below 4G and caused "can't claim" failures on BARs
                // of the P100/Arc above 4G without pci=realloc.
                // Preserviamo cio' che BIOS/AGESA (o ResizeIntelGpuBars per Arc)
                // hanno gia' scritto in upper32.
                (VOID)pUpperZero;
                pciReadWord(rbIo, addr + 0x24, &pBase);
                pciReadWord(rbIo, addr + 0x26, &pLimit);
                pBase  = (pBase  & 0xFFF0) | 0x0001;   // bit0=1 -> 64-bit
                pLimit = (pLimit & 0xFFF0) | 0x0001;
                pciWriteWord(rbIo, addr + 0x24, &pBase);
                pciWriteWord(rbIo, addr + 0x26, &pLimit);
                // upper32 intenzionalmente NON toccati (preserve-BIOS)
            }

            L_STR("[DB] Quirk bridge 00:");
            L_HEX(dev, 2); L_STR(".");
            L_HEX(func, 1); L_STR(" DID=");
            L_HEX(did, 4); L_STR(" pref64=OK\n");

            DEBUG((DEBUG_INFO,
                "ReBarDXE: ExitBS quirk AMD bridge 00:%02x.%x (DID 0x%x) "
                "-> prefetchable 64-bit\n", dev, func, did));

            // Se func0 non è multi-function, esci after havingla processata
            if (func == 0 && (headerType & 0x80) == 0) break;
        }
    }
}

// =====================================================================
// v0.5.2 FIX BUG 1: busy-wait safe per ExitBootServices callback chain
// =====================================================================
// gBS->Stall() is not guaranteed to work after ExitBootServices
// signaled (UEFI spec). We replace it with a TSC-based busy-loop, no
// dipendenza da boot services. Approssimazione frequenza CPU 3.6 GHz
// per AMD FX (off by 20% ok per our use case: link-training
// poll 1ms + audio melody timing).
//
// Non we use MicroSecondDelay (TimerLib): non incluso nel DSC corrente
// and depends on a platform-configurable PCD.

static VOID IdneBusyWaitUs(UINTN us)
{
    // v0.5.2 hardened: port-IO 0x80 (POST checkpoint port) read prende
    // ~1us su any chipset i440-compatible (standard ad-hoc us
    // delay da decenni in BIOS code). Timing STABILE indipendente da:
    //   - CPU frequency (FX-8350 4.0GHz vs FX-6300 3.5GHz)
    //   - CPU C-states / power management
    //   - TSC calibration / TSC stop in C6
    // Trade-off: precisione assoluta ~833ns - 1.2us per IoRead8(0x80)
    // a second del chipset. Per our use case (link-training poll
    // 1ms x 500 = 500ms timeout, audio melody 80-400ms gaps) la
    // precisione +/-20% is irrilevante.
    for (UINTN i = 0; i < us; i++) {
        IoRead8(0x80);
    }
}

static VOID IdneBusyWaitMs(UINTN ms)
{
    IdneBusyWaitUs(ms * 1000);
}

// =====================================================================
// v9: DIAGNOSTIC BEEP MELODY via PC SPEAKER (PIT 8253/8254 + port 0x61)
// =====================================================================
// Hardware path:
//   - Port 0x43: PIT Mode/Command register
//   - Port 0x42: PIT Channel 2 data (generatore onda quadra per speaker)
//   - Port 0x61: System Control. Bit0=Timer2 gate, Bit1=Speaker data.
//     Entrambi a 1 → speaker suona alla frequenza programmata sul ch2.
// Frequenza: f = 1193180 / divisor. divisor UINT16 → f >= ~18 Hz.
// Timing: gBS->Stall(usec) — preciso a ms via TSC, non al PIT same.
//
// Salviamo/ripristiniamo port 0x61 per preservare bit alti (NMI/parity/IOCHK).

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
    // v0.5.2 BUG 1 FIX: gBS->Stall replaced with a safe busy-wait for
    // ExitBootServices callback chain.
    V9BeepOn(hz);
    IdneBusyWaitMs(ms);
    V9BeepOff();
    IdneBusyWaitMs(80);       // gap fra bip
}

// Melody "V" (morse "V" = · · · —, qui invertita in 2·2—2·):
//   2 corti (120 ms) · 2 lunghi (400 ms) · 2 corti (120 ms) a 880 Hz
// Duration totale ~2.1 s. Chiamata BEFORE di POST 0xFC per sync ear+LED.
// Save/restore di port 0x61 per preservare i bit alti (NMI/parity/IOCHK).
static VOID V9PlayFcMelody(VOID)
{
    UINT8 orig61 = IoRead8(0x61);

    // v0.5.2 BUG 1 FIX: gBS->Stall replaced with IdneBusyWaitMs (safe post-ExitBS).
    V9Beep(880, 120); V9Beep(880, 120);           // .. corti
    IdneBusyWaitMs(150);
    V9Beep(880, 400); V9Beep(880, 400);           // -- lunghi
    IdneBusyWaitMs(150);
    V9Beep(880, 120); V9Beep(880, 120);           // .. corti

    IoWrite8(0x61, orig61);                        // restores bit alti
}

#else
#define V9PlayFcMelody()  do { } while (0)
#endif  // ENABLE_BEEP_MELODY

// =====================================================================
// v0.2: RESIZEAMDGPUBARS — W9170/S9170 BAR0 32GB + BAR2 8MB, BUS-AGNOSTIC
// =====================================================================
// Target: FirePro W9170 / S9170 32GB (Hawaii XT GL, VID:DID = 0x1002:0x67A0).
// ReBAR capability: offset 0x200 (cap ID 0x0015).
//
// VBIOS CAP vs SILICIO (discovered v9.6, validated sul field v9.7, 2026-04-22):
//   - VBIOS W9170 dichiara REBAR_CAP = 0x0007F000 (bit 12..18 set)
//     → max size index declared = 14 → 16 GB. Valore conservative
//     (probably inherited from the 16GB W9100 that shares silicon).
//   - Silicio Hawaii PCIe IP ACCETTA REBAR_CTRL.size = 15 → 32 GB
//     (verified empiricamente: setpci ECAP_REBAR+08.L=00000f00
//     restituisce readback 0x00000f20 su BDF 23:00.0).
//   - The module scavalca il CAP bitmap e forces idx=15 (32 GB).
//   - BONUS: BAR = VRAM = 32 GB elimina il partial-ReBAR bug di amdgpu
//     on Hawaii (fast path assumes BAR >= VRAM; with 16 GB BAR on 32 GB
//     VRAM il driver collassa su modelli LLM >16 GB).
//
// Changes storici:
//   v9.4 (2026-04-21): discovery bus-agnostic (scan root port per VID:DID
//     endpoint) al posto di hardcoded bridge 00:15.3 DID 0x43A3.
//   v9.6 (2026-04-22): override idx=15 (32 GB) + shift BAR2 a 0x2400000000.
//   v9.7 (2026-04-22): fix alignment 32 GB — target 0x1C00000000 violava
//     PCIe spec R/O bits (bit 34 hard-wired 0 per BAR size 32 GB → readback
//     kernel 0x1800000000 → collision Arc). Sposta BAR0 a 0x2000000000
//     (32-GB-aligned) e BAR2 a 0x2800000000.
//
// Sequenza (v0.2):
//   1. Scan bus 0 → root port (hdr tipo 1) → secondary/sub bus
//   2. For each bus under the root port, look for type-0 endpoint 1002:67A0
//   3. When found: bridgeAddr = quel root port, epAddr = endpoint
//   4. pciFindExtCapEcam(cap_id=0x0015) → atteso offset 0x200
//   5. Bridge window pref64 [W9170_BRIDGE_BASE..W9170_BRIDGE_LIMIT]
//      (0x2000000000 - 0x28007FFFFF: copre BAR0 32GB + BAR2 8MB, 32-GB-aligned)
//   6. REBAR_CTRL (cap+0x08) size index 15 (→ 32 GB, silicon override VBIOS CAP)
//   7. BAR0 = W9170_BAR0_TARGET (0x2000000000), 64-bit pref, 32 GB aligned
//   8. BAR2 = W9170_BAR2_TARGET (0x2800000000), 64-bit pref, 8MB non-resizable
//   9. Log [DF] + POST 0xDF
//
// v0.2: nessun cursore, nessun bridge-DID hardcoded. Indirizzi costanti
// #define, position-indipendenti. Fail paths loggano [DE] + POST 0xDE e
// return without writing anything incompatible (implicit rollback).
// If the REBAR_CTRL readback does not confirm idx=14 (silicon refuses),
// the function bails out cleanly -- it does not leave inconsistent state behind.

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
    L_STR("[DE] ResizeAmdGpuBars v0.5.5 enter (16GB clamp + BUG2 reverted)\n");

    // v9.4: discovery bus-agnostic — scans all i root port bus 0 e
    // look for endpoint 1002:67A0 (W9170 Hawaii XT GL) underneath. This way the module
    // finds la W9170 sia sul riser (00:15.3/43A3) che nello slot 3
    // (00:0b.0/5A1F) or any other slot.
    for (rootDev = 0; rootDev < 32 && !foundEndpoint; rootDev++) {
        for (rootFunc = 0; rootFunc < 8 && !foundEndpoint; rootFunc++) {
            UINT64  rpAddr = EFI_PCI_ADDRESS(0, rootDev, rootFunc, 0);
            UINT32  rpVidDid;
            UINT8   rpHdr, rpSecBus, rpSubBus;
            UINT8   bus, d;

            if (EFI_ERROR(pciReadDword(rbIo, rpAddr, &rpVidDid))) break;
            if ((rpVidDid & 0xFFFF) == 0xFFFF) {
                if (rootFunc == 0) break;  // device absent, salta
                continue;
            }
            if (EFI_ERROR(pciReadByte(rbIo, rpAddr + 0x0E, &rpHdr))) continue;
            // Only bridge (tipo 1). Se func=0 e non multi-function, salta dev.
            if ((rpHdr & 0x7F) != 1) {
                if (rootFunc == 0 && (rpHdr & 0x80) == 0) break;
                continue;
            }
            pciReadByte(rbIo, rpAddr + 0x19, &rpSecBus);
            pciReadByte(rbIo, rpAddr + 0x1A, &rpSubBus);
            if (rpSecBus == 0) continue;

            // Scan all the buses under the root port
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
                    if ((hdr & 0x7F) != 0) continue;  // only endpoint tipo 0

                    // Match W9170!
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
        L_STR("[DE] W9170 (1002:67A0) NOT FOUND su alcun root port — skip\n");
        IoWrite8(POST_PORT, POST_AMD_REBAR_DONE);
        return;
    }

    L_STR("[DE] W9170 @ "); L_HEX(secBus, 2); L_STR(":");
    L_HEX(epDev, 2); L_STR(".0 sotto bridge 00:");
    L_HEX(bridgeDev, 2); L_STR("."); L_HEX(bridgeFunc, 1);
    L_STR(" (bridgeVidDid=0x"); L_HEX(bridgeVidDid, 8); L_STR(")\n");

    // Endpoint header already validated as type 0 in the discovery loop
    pciReadByte(rbIo, epAddr + 0x0E, &epHdr);
    (void)epHdr;  // already verified during the scan

    // 4. Trova ReBAR ext cap (atteso offset 0x200)
    rebarOff = pciFindExtCapEcam(rbIo, secBus, epDev, 0, PCI_EXT_CAP_ID_REBAR);
    if (rebarOff == 0) {
        L_STR("[DE] W9170 senza ReBAR cap — skip\n");
        IoWrite8(POST_PORT, POST_AMD_REBAR_DONE);
        return;
    }
    L_STR("[DE] W9170 ReBAR @ 0x"); L_HEX(rebarOff, 4); L_NL();

    // 5. Read REBAR_CAP (cap+0x04) to determine max supported size
    if (EFI_ERROR(pciReadDwordEcam(rbIo, secBus, epDev, 0, rebarOff + 0x04, &rebarCap))) {
        L_STR("[DE] REBAR_CAP read FAIL\n");
        IoWrite8(POST_PORT, POST_AMD_REBAR_DONE);
        return;
    }
    L_STR("[DE] REBAR_CAP=0x"); L_HEX(rebarCap, 8); L_NL();

    // Size mask bits[31:4], bit N = size 2^(N+20).
    // Per W9170 atteso 0x0007F000 → bit 12-18 set → max N=18 → 256 MB..16 GB
    // In REBAR_CAP bit[4] appears to be reserved/padding on some implementations.
    // We use (rebarCap >> 4) & 0x0FFFFFFF come in ResizeIntelGpuBars.
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
    L_STR("[DE] VBIOS declared max idx="); L_HEX(maxSizeIdx, 2);
    L_STR(" = "); L_HEX((UINT32)((1ULL << (maxSizeIdx + 20)) >> 20), 8); L_STR(" MB\n");

    // v0.2: SILICON OVERRIDE a idx=15 (32 GB).
    // The VBIOS W9170/S9170 CAP bitmap is 0x0007F000 (max idx=14, 16 GB)
    // because conservative - probably inherited from the W9100 line at
    // 16 GB. Il the Hawaii silicon PCIe IP is pero' 32-GB-capable: verified
    // empiricamente in v0.1 boot log (REBAR_CTRL post=0x00000f20, BAR0
    // [size=32G] visibile in lspci).
    //
    // v0.2 NON replica gli errori di v0.1:
    //   - NESSUNA write di BIOS_SCRATCH_7 bit 9: amdgpu must avere
    //     need_post=true cosi' runs il suo ATOM ASIC_Init che popola
    //     CONFIG_MEMSIZE e FB_LOCATION correttamente dal VRAM_Info della
    //     card (32 GB declared in entrambi i VBIOS orig+modded).
    //   - NESSUNA write di MC_VM_FB_LOCATION: lo fa ATOM POST da only,
    //     leggendo BAR0 dal config space (ora 0x2000000000).
    //
    // The only write is this: forces REBAR_CTRL.size=15 in hardware,
    // che accetta nonostante il CAP bitmap dichiari max=14. Il resto del
    // flow (bridge window, command reg, BAR write) is identico a v9.8.
    // v0.5.3: clamp REBAR to idx=0xE (16 GB) instead of idx=0xF (32 GB).
    // Reason: test hypothesis "ring gfx -110 is 32GB-specific". A 16GB
    // BAR the GART table lands at 0x23FFE00000 (top BAR 16GB - 2MB), with
    // ~17 GB of headroom from the bridge top (0x28FFFFFFFF). Same config
    // di v9.5/v9.8 working baseline (16GB BAR + amdgpu OK).
    // If this test passes: 32GB-specific bug confirmed.
    // If it fails: bug is elsewhere (kernel ram_alloc port required).
    // Trade-off: Vulkan staging capped at ~16 GB (memo w9100_crash_vulkan.md
    // Type B). Acceptable for this diagnostic test.
    if (maxSizeIdx > 14) {
        L_STR("[DE] v0.5.3 16GB clamp: idx ");
        L_HEX(maxSizeIdx, 2);
        L_STR(" (VBIOS CAP) -> idx 0E (16 GB, GART space test)\n");
        maxSizeIdx = 14;
    } else if (maxSizeIdx < 14) {
        // VBIOS CAP declares less than 14 (unlikely, S9170/W9170 say 14).
        // Force to 14 for consistency with the test target.
        L_STR("[DE] v0.5.3 force min 16GB: idx ");
        L_HEX(maxSizeIdx, 2);
        L_STR(" -> idx 0E (16 GB)\n");
        maxSizeIdx = 14;
    }
    newSize = 1ULL << (maxSizeIdx + 20);
    L_STR("[DE] final max idx="); L_HEX(maxSizeIdx, 2);
    L_STR(" = "); L_HEX((UINT32)(newSize >> 20), 8); L_STR(" MB\n");

    // 6. Read REBAR_CTRL corrente (cap+0x08)
    if (EFI_ERROR(pciReadDwordEcam(rbIo, secBus, epDev, 0, rebarOff + 0x08, &rebarCtrl))) {
        L_STR("[DE] REBAR_CTRL read FAIL\n");
        IoWrite8(POST_PORT, POST_AMD_REBAR_DONE);
        return;
    }
    L_STR("[DE] REBAR_CTRL (pre)=0x"); L_HEX(rebarCtrl, 8); L_NL();

    // 7. v9.4: target BAR0 hard-coded (nessun cursore, position-indipendente).
    assignAddr = W9170_BAR0_TARGET;
    L_STR("[DE] W9170 BAR0 target=0x"); L_HEX(assignAddr, 16); L_NL();

    // 8. Bridge window pref64 v0.2: copre BAR0 (32GB @ 0x2000000000)
    //    + BAR2 (8MB @ 0x2800000000). Window nominale 32GB + 8MB, nessun
    //    gap. Window is gia' 32-GB-capable da v9.7 (era sovra-dimensionata
    //    in v9.8), only the cap on the actual BAR changes from 16 to 32 GB.
    //    Il bridge è quello discovered al punto 1 (qualunque root port sia).
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

    // v0.5: readback of the bridge limit registers to confirm the
    // extension to 0x28FFFFFFFF was applied (some chipsets
    // have hard-wired bits that may reject the value).
    {
        UINT16 rbBase = 0, rbLimit = 0;
        UINT32 rbBaseUp = 0, rbLimitUp = 0;
        pciReadWord (rbIo, bridgeAddr + 0x24, &rbBase);
        pciReadWord (rbIo, bridgeAddr + 0x26, &rbLimit);
        pciReadDword(rbIo, bridgeAddr + 0x28, &rbBaseUp);
        pciReadDword(rbIo, bridgeAddr + 0x2C, &rbLimitUp);
        UINT64 effBase  = ((UINT64)rbBaseUp  << 32) | (((UINT64)(rbBase  & 0xFFF0)) << 16);
        UINT64 effLimit = ((UINT64)rbLimitUp << 32) | (((UINT64)(rbLimit & 0xFFF0)) << 16) | 0xFFFFFULL;
        L_STR("[DR] bridge readback PB="); L_HEX(rbBase, 4);
        L_STR(" PL="); L_HEX(rbLimit, 4);
        L_STR(" PBu="); L_HEX(rbBaseUp, 8);
        L_STR(" PLu="); L_HEX(rbLimitUp, 8); L_NL();
        L_STR("[DR] bridge effective base=0x"); L_HEX(effBase, 16);
        L_STR(" limit=0x"); L_HEX(effLimit, 16); L_NL();
        if (effLimit >= 0x28FFFFFFFFULL) {
            L_STR("[DR] extension OK (limit >= 0x28FFFFFFFF, +1GB sopra BAR2)\n");
        } else if (effLimit >= 0x28007FFFFFULL) {
            L_STR("[DR] extension PARTIAL (limit accepted ma < target, hw clamp)\n");
        } else {
            L_STR("[DR] extension FAIL (limit < v0.4 baseline)\n");
        }
    }

    // Enable Memory+BusMaster sul bridge
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
        // Ripristina command e ritorna
        pciWriteWord(rbIo, epAddr + 0x04, &cmd);
        IoWrite8(POST_PORT, POST_AMD_REBAR_DONE);
        return;
    }
    pciReadDwordEcam(rbIo, secBus, epDev, 0, rebarOff + 0x08, &readback);
    L_STR("[DE] REBAR_CTRL (post)=0x"); L_HEX(readback, 8); L_NL();
    if (((readback >> 8) & 0x3F) != maxSizeIdx) {
        L_STR("[DE] REBAR_CTRL readback mismatch — hw rifiuta il resize, skip BAR write\n");
        pciWriteWord(rbIo, epAddr + 0x04, &cmd);
        IoWrite8(POST_PORT, POST_AMD_REBAR_DONE);
        return;
    }

    // 11. Write BAR0 low/high: addr[31:4] + tipo 64-bit pref (bits 0x0C)
    newLo = (UINT32)(assignAddr & 0xFFFFFFF0) | 0x0C;  // 64-bit pref
    newHi = (UINT32)(assignAddr >> 32);
    pciWriteDword(rbIo, epAddr + 0x10, &newLo);
    pciWriteDword(rbIo, epAddr + 0x14, &newHi);
    L_STR("[DE] W9170 BAR0 low=0x"); L_HEX(newLo, 8);
    L_STR(" high=0x"); L_HEX(newHi, 8); L_NL();

    // 11b. v9.3: program BAR2 (8MB MMIO regs, 64-bit pref, not resizable).
    //      Verify che la config corrente indichi 64-bit pref, poi
    //      writes gli 8 byte di BAR2 (0x18 low / 0x1C high) al target.
    //      This evita che il kernel lasci BAR2 a 0 ("can't claim"):
    //      we explicitly assign a home inside the bridge window.
    {
        UINT32 bar2Lo = 0, bar2LoNew, bar2HiNew;
        pciReadDword(rbIo, epAddr + 0x18, &bar2Lo);
        L_STR("[DE] W9170 BAR2 origLo=0x"); L_HEX(bar2Lo, 8); L_NL();
        // bit0 == 0 (memory), bits[2:1] == 10 (64-bit), bit3 = pref (should be 1)
        if ((bar2Lo & 0x01) == 0 && (bar2Lo & 0x06) == 0x04) {
            bar2LoNew = (UINT32)(W9170_BAR2_TARGET & 0xFFFFFFF0)
                | (bar2Lo & 0x0F);   // preserva flags (64-bit, pref)
            bar2HiNew = (UINT32)(W9170_BAR2_TARGET >> 32);
            pciWriteDword(rbIo, epAddr + 0x18, &bar2LoNew);
            pciWriteDword(rbIo, epAddr + 0x1C, &bar2HiNew);
            L_STR("[DE] W9170 BAR2 low=0x"); L_HEX(bar2LoNew, 8);
            L_STR(" high=0x"); L_HEX(bar2HiNew, 8); L_NL();
        } else {
            L_STR("[DE] W9170 BAR2 non 64-bit pref — skip programmazione\n");
        }
    }

    // 12. Riabilita Memory+BusMaster sul device
    cmd |= 0x0006;
    pciWriteWord(rbIo, epAddr + 0x04, &cmd);

    // 13. v0.5: SCRATCH_7 SEED RIMOSSO (era step 13 in v0.4).
    //      Rationale: with the W9170-modded VBIOS (FB2000 patched) flashed
    //      sulla GPU 26/04 sera, kernel chiama amdgpu_atom_asic_init
    //      naturally because need_post=TRUE (SCRATCH_7 bit9=0 at boot).
    //      AsicInit in the kernel programs MC, CONFIG_MEMSIZE and all the
    //      registers MC correttamente (verified netconsole 26/04 sera).
    //      Settare bit9 da DXE (come faceva v0.4) impediva il kernel POST
    //      e lasciava MC vuoto -> regression. v0.5 lascia bit9=0.
    //      Il vero problem rimanente (GART table al margine bridge)
    //      is affrontato a livello bridge extension (W9170_BRIDGE_LIMIT
    //      esteso a 0x28FFFFFFFF), non lato kernel/quirk.

    L_STR("[DF] ResizeAmdGpuBars OK v0.5.5 -- BAR0 16GB @ 0x");
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
        "ReBarDXE: === ExitBootServices === %d device nascosti [POST D8]\n",
        (UINT32)gHiddenDeviceCount));

    // 0. CPU NB MMIO Window 1: programmazione DEFINITIVA
    //    The write at DXE dispatch (step 2) is lost — something la zeroes
    //    first di Linux. The HT gate (step 1 below) survives because
    //    it is on the SR5690, not on the CPU NB. We reprogram with force=TRUE.
    //    Window 1: from above-RAM to 256GB (covers 4 GPUs: 2xP100+A770+FirePro)
    if (gSavedRbIo != NULL) {
        IoWrite8(POST_PORT, POST_EXITBS_NB_WINDOW);
        // Readback BEFORE della riprogrammazione forzata
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
                "ReBarDXE: ExitBS NB MMIO Window 1 FORZATA (256GB) [POST D9]\n"));
        } else {
            L_STR("[D9] ProgramCpuMmioWindow FORCE: FAIL\n");
            DEBUG((DEBUG_ERROR,
                "ReBarDXE: ExitBS ERRORE programmazione NB Window!\n"));
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

    // 1. Apri il gate HT 64-bit sul SR5690 (era setpci -s 00:00.0 94.L=3)
    if (gSavedRbIo != NULL) {
        IoWrite8(POST_PORT, POST_EXITBS_HT_GATE);
        // Readback pre-gate
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

    // 2. Applica quirk 990FX bridge su ALL i bridge (64-bit prefetchable)
    if (gSavedRbIo != NULL) {
        L_STR("[DB] Apply990FxBridgeQuirkAll\n");
        Apply990FxBridgeQuirkAll(gSavedRbIo);
    }

    // 3. Riabilita i link PCIe dei devices hidden
    IoWrite8(POST_PORT, POST_EXITBS_LINK_EN);
    L_STR("[DB] ReEnableAllHiddenDevices\n");
    ReEnableAllHiddenDevices();

    // 4. Resizable BAR su GPU Intel (Arc A770) — ridimensiona da 256MB a max
    //    Must run AFTER bridge quirk (step 2) because 64-bit prefetchable is needed
    //    Uses shared global cursors with ProgramHiddenDeviceBars
    if (gSavedRbIo != NULL) {
        L_STR("[DC] ResizeIntelGpuBars enter\n");
        ResizeIntelGpuBars(gSavedRbIo);
        L_STR("[DC] ResizeIntelGpuBars exit, nextPref=");
        L_HEX(gNextPrefAddr, 16); L_STR(" nextMmio=");
        L_HEX(gNextMmioAddr, 16); L_NL();
    }

    // 5. v9.8: Resizable BAR su FirePro W9170 (bus-agnostic) — 256 MB -> 16 GB.
    //    v9.8 rollback: 32 GB (v9.7) rompeva amdgpu ring test su CIK/Hawaii
    //    per full-BAR mode bug. Si resta in partial-BAR (16 GB su 32 GB VRAM).
    //    Discovery indipendente dalla topologia (scan root port per VID:DID
    //    1002:67A0 da v9.4). Addresses hard-coded via W9170_*_TARGET
    //    (32-GB-aligned inherited da v9.7, validi also per 16 GB BAR).
    //    Unico owner della window del bridge di
    //    upstream of the W9170 (the v8 branch in Apply990FxBridgeQuirkAll was
    //    removed).
    if (gSavedRbIo != NULL) {
        ResizeAmdGpuBars(gSavedRbIo);
        L_STR("[DF] ExitBS after AMD resize, nextPref=");
        L_HEX(gNextPrefAddr, 16); L_NL();
    }

    IoWrite8(POST_PORT, POST_EXITBS_DONE);
    L_STR("[DD] ExitBS sequence complete, flushing log to NVRAM\n");
    DEBUG((DEBUG_INFO, "ReBarDXE: === Pronto per Linux pci=realloc === [POST DD]\n"));

    // --- V6 FLUSH: scrivi il buffer in NVRAM UEFI variable ---
    // Must be done here because after the FC (delay) sequence we are effectively
    // oltre ExitBootServices e SetVariable may diventare inaffidabile
    LogFlushToNvram();

    // v9: audible melody "2 short . 2 long . 2 short" a 880 Hz first del
    // POST 0xFC — ear+LED sync. Useful when the Arc loses display with
    // BAR >4G: you know you reached the end of ExitBS even with a black monitor.
    // Duration ~2.1 s; the FC delay below becomes largely superfluous but we
    // lasciamo come buffer.
    V9PlayFcMelody();

    // FC = "Full Complete" — visibile ~2 secondi sul LED della card madre
    // before the BIOS overwrites it with AA
    IoWrite8(POST_PORT, POST_FULL_COMPLETE);
    {
        volatile UINT64 delay;
        for (delay = 0; delay < 200000000ULL; delay++) {}
    }
    IoWrite8(POST_PORT, POST_FULL_COMPLETE);  // riscrittura finale
}

// =====================================================================
// DSDT PATCH: SBLOCCO FINESTRA 64-BIT
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
        DEBUG((DEBUG_ERROR, "ReBarDXE: RSDP non trovato\n"));
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

    DEBUG((DEBUG_ERROR, "ReBarDXE: DSDT non trovato\n"));
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
        DEBUG((DEBUG_ERROR, "ReBarDXE: DSDT firma non valida: 0x%x\n", Dsdt->Signature));
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

            DEBUG((DEBUG_INFO, "ReBarDXE: DSDT pattern trovato a offset 0x%x\n", i));

            DsdtBytes[i + 5]  = AML_ONE_OP;    // MALH = One (enables finestra 64-bit)
            // MAML e MAMH non toccati: CRS1 (CPRB=One) uses values hardcoded,
            // CRS2 is not reached on this card.

            FixDsdtChecksum(Dsdt);

            DEBUG((DEBUG_INFO, "ReBarDXE: DSDT PATCH APPLICATA! Finestra 64-bit attiva\n"));
            return TRUE;
        }
    }

    DEBUG((DEBUG_WARN, "ReBarDXE: DSDT pattern non trovato (gia' patchato?)\n"));
    return FALSE;
}

// =====================================================================
// CPU FX NORTHBRIDGE: PROGRAM 64-BIT MMIO WINDOW
// =====================================================================
// I registers Address Map del AMD FX (Family 15h) sono a Bus0:Dev24(0x18):Func1.
// Desktop AGESA never programs the MMIO windows above 4GB.
// Noi programmiamo Window 1 (D18F1x88/8C) per route transazioni MMIO
// from above the RAM up to 64GB-1 toward the SR5690 (DstNode=0).
//
// Formato registers (BKDG Family 15h):
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

// DRAM Limit register (to find top-of-memory dynamically)
#define DRAM_LIMIT_LOW_0  0x44
#define DRAM_LIMIT_HIGH_0 0x144

// force=FALSE: skip se gia' attiva (per chiamata a DXE dispatch)
// force=TRUE:  always program (for ExitBootServices -- the DXE value is lost)
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

    // Se non forzato, checks se Window 1 is gia' programmata.
    // v0.5.4: REVERTED BUG 2 FIX. The pciAddrOffset() "fix" from the Banjo5k fork
    // caused a BIOS Aptio regression in idne053 (hang post-AB Setup
    // Idle, USB morto). Original "buggy" behavior (direct add) was
    // latent harmless -- restored for Aptio IV stability.
    if (!force) {
        if (EFI_ERROR(pciReadDword(rbIo, cpuNbAddr + MMIO_BASE_LOW_1, &existingBase)))
            return FALSE;

        if (existingBase & 0x03) {
            DEBUG((DEBUG_INFO,
                "ReBarDXE: CPU MMIO Window 1 gia' attiva (0x%08x), skip\n",
                existingBase));
            return TRUE;
        }
    }

    // Read DRAM Limit to find where the RAM ends.
    // v0.5.4: REVERTED BUG 2 FIX (see above). Direct addition as in pre-v0.5.2.
    if (EFI_ERROR(pciReadDword(rbIo, cpuNbAddr + DRAM_LIMIT_LOW_0, &dramLimitLow)))
        return FALSE;
    if (EFI_ERROR(pciReadDword(rbIo, cpuNbAddr + DRAM_LIMIT_HIGH_0, &dramLimitHigh)))
        return FALSE;

    // DRAM Limit formato: bits[31:21] = DramLimit[31:21]
    // DramLimit High: bits[7:0] = DramLimit[39:32]
    topOfMem = ((UINT64)(dramLimitHigh & 0xFF) << 32) |
               ((UINT64)(dramLimitLow & 0xFFE00000));
    // Aggiungi il blocco finale (granularity 2MB)
    topOfMem += 0x200000;

    DEBUG((DEBUG_INFO,
        "ReBarDXE: DRAM Top of Memory = 0x%lx\n", topOfMem));

    // MMIO Base: just above the RAM, aligned to 64KB (minimum granularity)
    // Aggiungiamo 256MB di margine per sicurezza
    mmioBase = (topOfMem + 0x10000000ULL) & ~0xFFFFULL;

    // MMIO Limit: 256GB - 1
    // Dimensionata per configuration multi-GPU:
    //   P100 #1:    16GB pref (slot 1)
    //   P100 #2:    16GB pref (slot 2, NVLink)
    //   Arc A770:   16GB pref (slot 3, display)
    //   FirePro:    32GB pref (via PCIe x1 riser)
    //   + BAR0/BAR3 non-pref per each GPU (~200MB totali)
    // Totale: ~80GB+ di BAR space, 256GB da' ampio margine
    mmioLimit = 0x3FFFFFFFFFULL;

    DEBUG((DEBUG_INFO,
        "ReBarDXE: Programmando CPU MMIO Window 1: 0x%lx - 0x%lx (force=%d)\n",
        mmioBase, mmioLimit, (UINT32)force));

    // Calcola values registers
    // Base Low: bits[31:8] = MmioBase[39:16], bit[1]=WE, bit[0]=RE
    baseLow = (UINT32)(((mmioBase >> 16) & 0xFFFFFF) << 8) | 0x03;

    // Limit Low: bits[31:8] = MmioLimit[39:16], bits[2:0]=DstNode(0)
    limitLow = (UINT32)(((mmioLimit >> 16) & 0xFFFFFF) << 8) | 0x00;

    // Base High: bits[7:0] = MmioBase[47:40]
    baseHigh = (UINT32)((mmioBase >> 40) & 0xFF);

    // Limit High: bits[7:0] = MmioLimit[47:40]
    limitHigh = (UINT32)((mmioLimit >> 40) & 0xFF);

    DEBUG((DEBUG_INFO,
        "ReBarDXE: Registri: Base=0x%08x/%08x Limit=0x%08x/%08x\n",
        baseHigh, baseLow, limitHigh, limitLow));

    // WRITE: first the high registers, then the low ones (atomic activation)
    // v0.5.4: REVERTED BUG 2 FIX. Direct addition as in pre-v0.5.2.
    // The overflow "bug" (offset 0x188/0x18C/0x144 added directly
    // route to D18F2 reg 0x88/0x8C/0x44 instead of the correct D18F1 regs)
    // is latent harmless in our flow (we write 0 to default 0).
    // The pciAddrOffset "fix" caused a regression BIOS Aptio in
    // idne053 — likely clobbering non-zero registers that Aptio had
    // pre-programmed and expected to find intact.
    if (EFI_ERROR(pciWriteDword(rbIo, cpuNbAddr + MMIO_BASE_HIGH_1, &baseHigh)))
        return FALSE;
    if (EFI_ERROR(pciWriteDword(rbIo, cpuNbAddr + MMIO_LIMIT_HIGH_1, &limitHigh)))
        return FALSE;
    if (EFI_ERROR(pciWriteDword(rbIo, cpuNbAddr + MMIO_LIMIT_LOW_1, &limitLow)))
        return FALSE;
    // Base Low va per ultimo (contiene RE/WE che attivano la finestra)
    if (EFI_ERROR(pciWriteDword(rbIo, cpuNbAddr + MMIO_BASE_LOW_1, &baseLow)))
        return FALSE;

    DEBUG((DEBUG_INFO,
        "ReBarDXE: CPU MMIO Window 1 PROGRAMMATA! 256GB routing attivo\n"));

    return TRUE;
}

// =====================================================================
// SR5690 HT HOST BRIDGE: OPEN THE 64-BIT MMIO GATE
// =====================================================================
// Il SR5690 (B0:D0.0) checks il forwarding delle transazioni MMIO
// 64-bit sul link HyperTransport tra CPU e chipset. Without this gate
// aperto, le transazioni MMIO above 4GB programmate nella CPU MMIO
// Window non raggiungono il fabric PCIe del SR5690.
//
// Registro 0x94 bit[1:0]:
//   bit 0 = Read Enable  (RE)
//   bit 1 = Write Enable (WE)
//   0x03  = gate completamente aperto
//
// Equivale al comando Linux: setpci -s 00:00.0 94.L=00000003
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

    // Verify che B0:D0.0 sia effettivamente il SR5690 (VID AMD/ATI)
    if (EFI_ERROR(pciReadDword(rbIo, htAddr, &vidDid)))
        return FALSE;

    if ((vidDid & 0xFFFF) != SR5690_VID) {
        DEBUG((DEBUG_WARN,
            "ReBarDXE: B0:D0.0 VID=0x%04x, non SR5690 (atteso 0x%04x)\n",
            (UINT16)(vidDid & 0xFFFF), SR5690_VID));
        return FALSE;
    }

    DEBUG((DEBUG_INFO,
        "ReBarDXE: SR5690 trovato B0:D0.0 VID:DID=%04x:%04x\n",
        (UINT16)(vidDid & 0xFFFF), (UINT16)(vidDid >> 16)));

    // Read current value of the gate register
    if (EFI_ERROR(pciReadDword(rbIo, htAddr + SR5690_MMIO64_GATE_REG, &gateVal)))
        return FALSE;

    DEBUG((DEBUG_INFO,
        "ReBarDXE: SR5690 reg 0x94 attuale = 0x%08x\n", gateVal));

    // Apri il gate: setta bit 0 (RE) e bit 1 (WE)
    newGateVal = gateVal | 0x03;

    if (newGateVal == gateVal) {
        DEBUG((DEBUG_INFO, "ReBarDXE: SR5690 HT gate gia' aperto\n"));
        return TRUE;
    }

    if (EFI_ERROR(pciWriteDword(rbIo, htAddr + SR5690_MMIO64_GATE_REG, &newGateVal)))
        return FALSE;

    DEBUG((DEBUG_INFO,
        "ReBarDXE: SR5690 HT GATE APERTO! reg 0x94: 0x%08x -> 0x%08x\n",
        gateVal, newGateVal));

    return TRUE;
}

// =====================================================================
// DEVICE SETUP (chiamato da PreprocessController come backup)
// =====================================================================

VOID reBarSetupDevice(EFI_HANDLE handle, EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_PCI_ADDRESS addrInfo)
{
    UINTN epos;
    UINT16 vid, did;
    UINTN pciAddress;
    EFI_STATUS status;

    // v0.5.2 BUG 5 FIX: HandleProtocol return discarded before v0.5.2.
    // If it fails, pciRootBridgeIo stays stale or NULL -> crash
    // in pciReadConfigWord. Check status + early return.
    status = gBS->HandleProtocol(handle, &gEfiPciRootBridgeIoProtocolGuid,
                                  (void**)&pciRootBridgeIo);
    if (EFI_ERROR(status) || pciRootBridgeIo == NULL) {
        DEBUG((DEBUG_WARN,
            "ReBarDXE: reBarSetupDevice HandleProtocol failed: %r\n", status));
        return;
    }

    pciAddress = EFI_PCI_ADDRESS(addrInfo.Bus, addrInfo.Device, addrInfo.Function, 0);
    pciReadConfigWord(pciAddress, 0, &vid);
    pciReadConfigWord(pciAddress, 2, &did);

    if (vid == 0xFFFF)
        return;

    DEBUG((DEBUG_INFO, "ReBarDXE: PreprocessCtrl device vid:%04x did:%04x %02x:%02x.%x\n",
        vid, did, addrInfo.Bus, addrInfo.Device, addrInfo.Function));

    // Quirk 990FX bridge: spostato a ExitBootServices.
    // During il BIOS NON tocchiamo i bridge — le hex patch aggressive (7/8)
    // forzerebbero l'allocazione >4GB also per GPU normal (1660 Super),
    // causando screen black. Il quirk serve only per Linux.
    if (vid == 0x1002 && (did == 0x5a16 || did == 0x5a1c || did == 0x43A3)) {
        DEBUG((DEBUG_INFO,
            "ReBarDXE: 990FX/SB900 bridge %02x:%02x.%x (DID 0x%x) — quirk 64-bit "
            "rinviato a ExitBootServices\n",
            addrInfo.Bus, addrInfo.Device, addrInfo.Function, did));
    }

    // ReBAR (only se ReBarState > 0 e device non is hidden)
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
// PROTOCOL NOTIFY: INSTALLA HOOK QUANDO IL PROTOCOLLO DIVENTA DISPONIBILE
// =====================================================================
// When the module loads before PciBus, the protocol
// PciHostBridgeResourceAllocation does not exist yet. We use a
// notification callback to catch it as soon as it is installed.

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

    DEBUG((DEBUG_INFO, "ReBarDXE: Protocollo ResourceAllocation trovato, installo hook PreprocessController\n"));
    o_PreprocessController = pciResAlloc->PreprocessController;
    pciResAlloc->PreprocessController = &PreprocessControllerOverride;
    FreePool(handleBuffer);

    // Chiudi l'evento — hook installed, non serve piu'
    gBS->CloseEvent(gResAllocNotifyEvent);
    gResAllocNotifyEvent = NULL;
}

// =====================================================================
// CALLBACK READY TO BOOT — DSDT PATCH
// =====================================================================
// The DSDT is installed by the AMI ACPI driver, which runs AFTER our
// module. Patchiamo il DSDT al ReadyToBoot, when all le tabelle ACPI
// are certainly loaded but BEFORE the bootloader reads them.

static EFI_EVENT gReadyToBootEvent = NULL;

VOID EFIAPI OnReadyToBoot(
    IN EFI_EVENT Event,
    IN VOID*     Context
)
{
    EFI_SYSTEM_TABLE* SystemTable = (EFI_SYSTEM_TABLE*)Context;

    DEBUG((DEBUG_INFO, "ReBarDXE: ReadyToBoot — tentativo patch DSDT\n"));

    if (PatchDsdtMalh(SystemTable)) {
        DEBUG((DEBUG_INFO, "ReBarDXE: DSDT PATCH APPLICATA in ReadyToBoot!\n"));
    } else {
        DEBUG((DEBUG_WARN, "ReBarDXE: DSDT patch fallita anche in ReadyToBoot\n"));
    }

    // Chiudi l'evento — non serve più
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

    // --- V8 LOG: banner + scan CSM ---
    L_STR("=== IDNEfirepro32g v0.5.5 LOG ===\n");
    L_STR("[D0] rebarInit entry\n");
    L_STR("[D0] imageHandle="); L_HEX((UINT64)(UINTN)imageHandle, 16); L_NL();
    L_STR("[D0] systemTable="); L_HEX((UINT64)(UINTN)systemTable, 16); L_NL();
    L_STR("[D0] SystemTable->FirmwareRevision=");
    L_HEX(systemTable->FirmwareRevision, 8); L_NL();
    L_STR("[D0] SystemTable->NumberOfTableEntries=");
    L_DEC(systemTable->NumberOfTableEntries); L_NL();
    // Scan CSM protocols to check whether CSM is really active
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

    // 1. DSDT PATCH: try immediately, on failure register ReadyToBoot callback
    IoWrite8(POST_PORT, POST_DSDT_PATCH);
    L_STR("[D1] DSDT patch attempt\n");
    if (PatchDsdtMalh(systemTable)) {
        L_STR("[D1] DSDT patched immediately (MALH=1)\n");
        DEBUG((DEBUG_INFO, "ReBarDXE: DSDT patchato subito [POST D1]\n"));
    } else {
        L_STR("[D1] DSDT not yet available, ReadyToBoot callback registered\n");
        DEBUG((DEBUG_INFO, "ReBarDXE: DSDT non ancora disponibile, registro ReadyToBoot callback\n"));
        status = gBS->CreateEventEx(
            EVT_NOTIFY_SIGNAL,
            TPL_CALLBACK,
            OnReadyToBoot,
            (VOID*)systemTable,
            &gEfiEventReadyToBootGuid,
            &gReadyToBootEvent);
        if (EFI_ERROR(status)) {
            DEBUG((DEBUG_ERROR, "ReBarDXE: ERRORE creazione ReadyToBoot event: %r\n", status));
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
                // La programmazione definitiva avviene a ExitBootServices (force=TRUE)
                if (ProgramCpuMmioWindow(mmioRbIo, FALSE)) {
                    L_STR("[D2] ProgramCpuMmioWindow DXE: OK\n");
                    DEBUG((DEBUG_INFO, "ReBarDXE: CPU MMIO 64-bit window OK\n"));
                } else {
                    L_STR("[D2] ProgramCpuMmioWindow DXE: FAIL\n");
                    DEBUG((DEBUG_ERROR, "ReBarDXE: ERRORE programmazione CPU MMIO window\n"));
                }
                // Readback D18F1 88/8C per vedere se la write prende
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

                // Salva rbIo per ExitBootServices (HT Gate + bridge quirk)
                gSavedRbIo = mmioRbIo;

                // HT Gate e bridge quirk spostati a ExitBootServices:
                // are not needed during BIOS and can cause allocation
                // >4GB of the normal GPUs (black screen with aggressive patches)
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
            "ReBarDXE: %d device nascosti via Link Disable [POST D5]\n",
            (UINT32)gHiddenDeviceCount));
    }

    // 4. Registra callback ExitBootServices (riabilita link PCIe)
    status = gBS->CreateEvent(
        EVT_SIGNAL_EXIT_BOOT_SERVICES,
        TPL_CALLBACK,
        OnExitBootServices,
        NULL,
        &gExitBootServicesEvent);

    if (EFI_ERROR(status)) {
        DEBUG((DEBUG_ERROR,
            "ReBarDXE: ERRORE creazione evento ExitBootServices: %r\n", status));
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

    // 6. Hook PreprocessController: register notification for when
    //    il protocollo ResourceAllocation diventa available.
    //    If the module loads before PciBus, the protocol does not exist
    //    still — la callback lo intercettera' al momento giusto.
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
            DEBUG((DEBUG_ERROR, "ReBarDXE: ERRORE RegisterProtocolNotify: %r\n", status));
        } else {
            DEBUG((DEBUG_INFO, "ReBarDXE: Attendo protocollo ResourceAllocation...\n"));
            // Prova immediately nel caso il protocollo esista gia'
            OnResAllocProtocolNotify(gResAllocNotifyEvent, NULL);
        }
    }

    IoWrite8(POST_PORT, POST_HOOK_REGISTERED);
    L_STR("[D6] rebarInit complete, awaiting ExitBootServices\n");
    // Flush precoce: safety net se ExitBS flush fails
    // The second flush at OnExitBootServices overwrites with full data
    LogFlushToNvram();
    DEBUG((DEBUG_INFO, "ReBarDXE: ===== INIT COMPLETO ===== [POST D6]\n"));
    return EFI_SUCCESS;
}

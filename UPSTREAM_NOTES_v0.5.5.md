# IDNEfirepro32g v0.5.5 — Upstream Notes

## Bug 2 Regression Report on Gigabyte GA-990FX-Gaming Rev 1.1

This document is intended as a feedback to the
[Banjo5k/990fxOrchestrator-personal](https://github.com/Banjo5k/990fxOrchestrator-personal)
fork, specifically PR #1 ("deep-dive-bug-scan") which introduced 6 bug
fixes in v9.6 of the upstream module.

We forked the original `990fxOrchestrator` upstream as `IDNEfirepro32g`
to add hardware-specific support for AMD FirePro S9170 32 GB GDDR5 on
Gigabyte GA-990FX-Gaming Rev 1.1. 
When we noticed the Banjo5k fork, we tested all 6 fixes against our codebase and hardware.

Cause we are working on that project right now, if you want to work on a updated code choose and edit this fork version for now.

---

## Acknowledgments

Thanks to **@Banjo5k** for the thorough deep-dive bug scan and the
detailed PR #1. The 6 issues identified are all real bugs in the
upstream codebase. Five of the six fixes apply cleanly to our fork
and we have integrated them. The sixth (BUG 2 — PCI offset overflow
in `ProgramCpuMmioWindow`) caused a hard regression on our Gigabyte
hardware, and we are documenting that here for the benefit of other
users on similar boards.

---

## Test Methodology

* Build with all 6 Banjo5k fixes applied → released as
  `IDNEfirepro32g v0.5.2 → v0.5.3` (16 GB BAR diagnostic build) →
  injected into `idne04.F2` via MMTool 4.50.0.23 → flashed via CH341A
  with full chip backup.
* Verified BIOS POST progression, USB input responsiveness, BIOS
  setup navigability, and Linux amdgpu probe.
* When regression observed, recovered via CH341A re-flash of
  pre-test backup, then re-ran the test with the suspect fix
  individually reverted (`v0.5.4`) to confirm the regression source.

---

## Test Results Summary

| Bug # | Severity | Description                                            | Status on Gigabyte 990FX |
|-------|----------|--------------------------------------------------------|--------------------------|
| 1     | HIGH     | `gBS->Stall()` called in ExitBootServices callback     | ✓ Applied — works        |
| 2     | MEDIUM   | PCI offset overflow in `ProgramCpuMmioWindow`          | ✗ **REGRESSION**         |
| 3     | LOW      | `pciFindExtCapEcam()` infinite loop on corrupt list    | ✓ Applied — works        |
| 4     | LOW      | Uninitialized buffers in `pciRebar*` helpers           | ✓ Applied — works        |
| 5     | LOW      | `reBarSetupDevice()` discarded `HandleProtocol` ret    | ✓ Applied — works        |
| 6     | TRIVIAL  | Hardcoded `.0` in Intel ReBAR log strings              | ✓ Applied — works        |

**5 of 6 fixes work cleanly. BUG 2 fix breaks BIOS Aptio IV on this board.**

---

## BUG 2 — Detailed Regression Report

### Symptom (idne053 build, BUG 2 fix applied)

After flashing the version containing the BUG 2 fix:

1. BIOS POST takes ~15 seconds to reach the Gigabyte logo.
2. BIOS UI eventually displays but is **completely unresponsive** —
   keyboard and mouse input are dead (USB controller appears not
   responding).
3. Attempting to load default settings (F9 or via menu) hangs the
   system at **POST code AB** (Aptio IV "Setup Idle / Verify Password"
   checkpoint) with input frozen.
4. POST sequence reaches checkpoint 92 (PCI bus enumeration done)
   and A2 (SATA detect done), but stalls indefinitely at AB.
5. Power-cycle does not recover; only CH341A re-flash to a known-good
   BIOS image restores normal operation.

### Symptom Disappears Once BUG 2 Fix is Reverted (idne054 build)

Building the same source tree with **only the BUG 2 fix reverted**
(all other 5 fixes still applied) restored normal BIOS behavior:
* POST timing back to ~3 s for logo.
* USB keyboard and mouse fully responsive.
* BIOS setup fully navigable.
* All other functionality preserved (PreScan, Link Disable, BAR
  programming, NB MMIO Window 1, etc.).

This empirical pair (idne053 broken, idne054 working, single
variable changed) confirms the BUG 2 fix as the root cause of the
regression on our hardware.

### Technical Analysis

#### Original (pre-fix) behavior

```c
// In ProgramCpuMmioWindow():
//   cpuNbAddr = EFI_PCI_ADDRESS(0, 0x18, 1, 0)  // = D18F1, raw value 0x180100
//   MMIO_BASE_HIGH_1 = 0x188

pciWriteDword(rbIo, cpuNbAddr + MMIO_BASE_HIGH_1, &baseHigh);

// 0x180100 + 0x188 = 0x180288
// EDK2 EFI_PCI_ADDRESS layout:
//   bits  0-7  = register
//   bits  8-15 = function
//   bits 16-23 = device
//   bits 24-31 = bus
//   bits 32+   = extended register (for 4 KB cfg space)
//
// Decoding 0x180288:
//   bus = 0
//   dev = 0x18
//   func = 2     <-- overflowed from offset 0x100 of the desired reg 0x188
//   reg = 0x88   <-- low byte of original 0x188
//
// Result: write addressed to D18F2 reg 0x88 (DRAM Controller area)
//         instead of intended D18F1 reg 0x188 (NB MMIO Window 1 base high).
```

This is the bug Banjo5k correctly identified.

**On AMD FX (Family 15h) Northbridge**, however, **D18F2 reg 0x88** is
a benign DRAM PHY-related register that:
* Defaults to zero on POR.
* Is not consulted by Aptio IV during POST in any way that we have
  been able to detect.

So the buggy write — writing 0 (because `baseHigh = 0` on our system,
where `mmioBase < 256 GB`) — is harmless: it writes 0 over a register
that already contained 0 and that nothing depends on.

#### "Fixed" behavior (using `pciAddrOffset()`)

```c
pciWriteDword(rbIo, pciAddrOffset(cpuNbAddr, MMIO_BASE_HIGH_1), &baseHigh);

// pciAddrOffset() correctly reconstructs the 64-bit EFI_PCI_ADDRESS
// with the extended-register bits (offset >> 8) routed to bits 32+
// of the address, leaving:
//   bus = 0, dev = 0x18, func = 1, reg = 0x188
//
// Result: write correctly addressed to D18F1 reg 0x188 (the actual
//         NB MMIO Window 1 base high register).
```

This is the fix Banjo5k correctly proposed.

**However**, on Gigabyte GA-990FX-Gaming Rev 1.1, **D18F1 reg 0x188 is
not necessarily zero at the moment the DXE driver runs**. The Aptio IV
BIOS appears to pre-program this register during early init — most
likely as part of its own (incomplete) attempt at MMIO routing setup.
Our DXE module then writes 0 over it (because we compute
`baseHigh = (mmioBase >> 40) & 0xFF`, which is zero for any address
below 256 GB).

This zeroing of a register the BIOS later relies on is the regression
trigger. The downstream effects appear to include:
* Disrupted NB MMIO Window 1 routing in some non-obvious mode.
* USB controller state not being properly accessible by the BIOS
  setup utility (consistent with input freeze at AB).
* Possibly other side effects we have not characterized.

#### Why the regression was not seen in pure-upstream builds

We suspect this regression is specific to the combination of:
* Gigabyte AGESA-based Aptio IV (the OEM-specific integration may be
  what pre-programs `D18F1 reg 0x188` differently from a clean ASRock
  or similar build).
* AMD FX Family 15h (Bulldozer/Piledriver) where the Address Map
  function 1 register layout is fully populated.

Other AM3+ boards, particularly other vendors or the original
RebarUEFI test platforms, may not exhibit this behavior.

### Resolution in Our Fork

We have reverted the BUG 2 fix in `IDNEfirepro32g v0.5.4` and kept it
reverted in `v0.5.5`. The 7 affected call sites (4 writes, 3 reads on
`cpuNbAddr + offset`) use plain pointer arithmetic on the
`EFI_PCI_ADDRESS` value, accepting the latent overflow into the
function field as a workaround that does not disrupt our specific
BIOS.

This is documented in the source as `BUG 2 FIX REVERTED` with
in-code comments.

### Recommendation for Upstream

The BUG 2 fix is correct in principle and probably appropriate for
many target platforms. However, its impact depends on what the host
BIOS does with the affected NB registers prior to DXE driver
execution. We suggest one of:

1. **Make the fix conditional** on a vendor / BIOS detection step,
   defaulting to "off" (i.e., keep direct addition) for Gigabyte
   AGESA-based boards. Could check vendor string in SMBIOS or the
   PCI vendor of the LPC bridge.

2. **Only update the HIGH registers when their value actually needs
   to be non-zero** (i.e., when `mmioBase` or `mmioLimit` exceed
   256 GB). For sub-256-GB MMIO windows, leave `*_HIGH_1` registers
   untouched, which avoids the overwrite regardless of whether the
   write addressing is "correct" or "buggy".

3. **At minimum, document the platform-specific risk** so that
   downstream forks targeting Gigabyte 990FX hardware can opt out.

We have implemented option (2)-style restraint in our fork by simply
not applying the fix.

---

## Diagnostic Markers

For other testers on Gigabyte hardware who want to reproduce, the
NVRAM log (`ReBarBootLog` UEFI variable) in our fork carries enough
diagnostic markers to confirm the configuration:

```
[DE] ResizeAmdGpuBars v0.5.5 enter (16GB clamp + BUG2 reverted)
[DE] REBAR_CTRL (post)=0x00000e20  (or 0x00000f20 if 32 GB)
[DR] bridge readback PB=... PL=... PBu=... PLu=...
[DR] bridge effective base=0x... limit=0x...
[DR] extension OK (limit >= 0x28FFFFFFFF, +1GB above BAR2)
[DT] W9170 found @bus:dev.func -> preventive hide (any-slot v0.5.1+)
[DF] ResizeAmdGpuBars OK v0.5.5 -- BAR0 16GB @ 0x...
```

If `[DE]` reports v0.5.5 entry but the BIOS hangs at AB nonetheless,
something else in our chain is implicated and we would appreciate
hearing about it.

---

## Files Distributed

This commit / archive contains:
* `IDNEfirepro32g_v0.5.5.c` — full source, English file header
* `IDNEfirepro32g_v0.5.5.efi` — compiled DXE driver (39 424 bytes)
* `IDNEfirepro32g_v0.5.5.ffs` — FFS-wrapped, ready for MMTool
  Replace into a Gigabyte GA-990FX-Gaming F2 BIOS image
  (FILE_GUID `adf0508f-a992-4a0f-8b54-0291517c21aa`)
* `IDNEfirepro32gDxe.inf` — EDK2 module definition
* `verify_rom.py` — post-injection sanity verifier (6 checks)
* This `UPSTREAM_NOTES_v0.5.5.md`

### Artifact Hashes (MD5)

| File                           | Bytes | MD5                                |
|--------------------------------|------:|------------------------------------|
| `IDNEfirepro32g_v0.5.5.efi`    | 39424 | `f6ac865227e53f5f2110d657c52f1260` |
| `IDNEfirepro32g_v0.5.5.ffs`    | 39486 | `c271aef5f49074c2eec8d92aded85bf0` |
| `IDNEfirepro32g_v0.5.5.c`      | ~135k | `90b57b01b10f17d2ceafd3441f3e1330` |

---

## Contact

Issues / questions / further test results welcome. The IDNEfirepro32g
fork lives at our internal repository (to be made public alongside
this report).

— 990fxorchestrator maintainers, 2026-05-02

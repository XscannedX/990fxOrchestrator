# Architecture

The module is a single EDK2 DXE driver compiled into one translation unit
(`990fxOrchestrator.c`). It runs entirely during firmware boot and does not install any
persistent runtime service beyond an NVRAM log flush.

The work is split into five sequential stages, with a final ExitBootServices
callback that performs the actual hardware reprogramming. Everything before
ExitBootServices is either discovery or preparation; actual BAR writes and
bridge-window rewrites happen at the last moment, so that the BIOS resource
allocator does its job first on devices the module has not hidden, and the
module's own writes are not subsequently overwritten.

```
┌──────────────────────────────────────────────────────────────────────┐
│  rebarInit (driver entry)                                            │
│                                                                      │
│  stage 1  DSDT patch                   ─── ACPI, in-memory           │
│  stage 2  CPU NB MMIO window           ─── D18F1 Window 1 regs       │
│  stage 3  Pre-scan + Link Disable      ─── hide >4GB BARs from BIOS  │
│  stage 4  PreprocessController hook    ─── backup quirk path         │
│                                                                      │
│  register  ExitBootServices callback                                 │
└──────────────────────────────────────────────────────────────────────┘
                                 │
                                 ▼
┌──────────────────────────────────────────────────────────────────────┐
│  OnExitBootServices                                                  │
│                                                                      │
│  a. Force NB MMIO window again (BIOS may have overwritten)           │
│  b. Open SR5690 HT gate (setpci-equivalent via register write)       │
│  c. Re-enable PCIe links on all hidden devices                       │
│  d. Apply990FxBridgeQuirkAll   ─── pref64 bit on AMD bridges         │
│  e. ResizeIntelGpuBars         ─── Arc A770: ReBAR + placement       │
│  f. ResizeAmdGpuBars           ─── W9170:    ReBAR + placement       │
│  g. Flush log to NVRAM                                               │
└──────────────────────────────────────────────────────────────────────┘
```

## Stage 1 — DSDT patch

The AMI DSDT shipped with the 990FXG firmware declares its MMIO64 window
descriptor behind an ACPI cap named `MALH`. The stock value is `Zero`,
effectively telling the OS *"no 64-bit MMIO window is available"*. Flipping
it to `One` exposes the descriptor.

The patch is done **in-memory** at boot: the driver walks the ACPI table
set obtained from `gEfiAcpi20TableGuid` / `gEfiAcpi10TableGuid`, locates the
DSDT, finds the `MALH` name object, and rewrites the `Zero` opcode byte to
`One`. No re-checksumming of the static image is needed because this is the
loaded copy the OS will consume.

## Stage 2 — CPU Northbridge MMIO window

AMD FX Family 15h processors route MMIO ranges to the root complex via
Northbridge function 1 device 18h (`D18F1`), specifically through MMIO Base
/ Limit register pairs known as *Windows*. Window 1 is reprogrammed to
cover a 256 GB range starting at `0xA0000000`, routed to the root bus, with
prefetchable = 1 and 64-bit = 1.

This is what actually creates the MMIO64 address space the later stages
allocate into. Without this step the root bus has no decode capability
above 4 GB and every BAR written above it would simply not decode.

## Stage 3 — Pre-scan and PCIe Link Disable

The BIOS resource allocator on this board silently fails on any device
whose full BAR would not fit below 4 GB. Rather than argue with it, the
module simply hides those devices before the allocator runs.

For each device on the bus:

1. Read VID:DID + class code.
2. Identify whether the device has a BAR larger than 4 GB at its native
   size. On the target hardware this is done by VID:DID match rather than
   by probing, to avoid the BAR-sizing protocol itself misbehaving.
3. If hidden, find the upstream bridge and set the *Link Disable* bit in
   its PCIe capability (Link Control register, bit 4).

With Link Disable asserted, the downstream endpoint does not respond to
Configuration Requests — from the BIOS's point of view the slot is empty.
The BIOS allocator completes normally without attempting to size the
hidden BARs.

The pre-scan records the hidden-device list in a module-local array for
stage 4 to restore.

## Stage 4 — PreprocessController hook (backup path)

Some devices may only appear on the bus after the pre-scan — hotplug is
theoretically possible on some bridges, and certain downstream switches
delay their secondary bus enumeration. `PreprocessController` is an AMI
PciHostBridgeResourceAllocation phase that the BIOS calls before it sizes
each device. The module installs a pre-hook that, for devices whose
VID:DID matches the "needs hiding" list but which weren't caught by the
pre-scan, applies the same Link Disable trick on the fly.

In practice on the validated hardware this hook fires rarely — the
pre-scan catches everything — but it exists as a safety net.

Additionally, this stage applies the **990FX bridge quirk**: on AMD SR5690
root ports (DID `0x5a16` / `0x5a1c`) the prefetchable base/limit bit 0
("supports 64-bit decode") is set, without modifying the upper32 halves
(which the BIOS and the ResizeIntelGpuBars stage program correctly).

## ExitBootServices — the actual work

The driver registers an `EFI_EVENT_NOTIFY` on ExitBootServices and performs
all destructive writes there. The ordering matters:

**(a) Force NB MMIO window again.** The BIOS sometimes overwrites the NB
window registers late in POST. Force them once more, the ExitBootServices
callback is after all BIOS SMM handlers.

**(b) HT gate.** `setpci -s 00:00.0 94.L=00000003` documents this: on
SR5690 register 0x94, bits 0–1 together are "enable forwarding of 64-bit
MMIO through the HyperTransport link". The BIOS leaves this at `0` because
it has no reason to open the gate.

**(c) Re-enable PCIe links** on previously hidden devices by clearing
their upstream bridge's Link Disable bit. Wait for link training
completion (polled on the Link Status register).

**(d) `Apply990FxBridgeQuirkAll`** (already described above) is re-run
here as a defensive pass in case stage 4's PreprocessController path
missed something.

**(e) `ResizeIntelGpuBars`** — the Arc A770 path:

1. Scan every root port on bus 0 across all functions (dev 0–31 ×
   func 0–7). Each matching bridge is walked down into its
   `[secondary_bus, subordinate_bus]` range.
2. For each endpoint matching VID `0x8086` with class `0x03` (display
   controller), find the PCIe Resizable BAR extended capability at cap
   list offset 0x15.
3. Program `REBAR_CTRL` to select the largest advertised size. For Arc
   A770 this is 16 GB on BAR2.
4. Rewrite BAR2 to the fixed target address `0x1800000000`.
5. Walk the full bridge chain from the root port down to the endpoint,
   setting on each intermediate bridge: prefetchable base / limit /
   upper32 to a 64-bit window that contains BAR2, and Command.{IO,MEM,BM}.

**(f) `ResizeAmdGpuBars`** — the W9170 path:

Identical structure to (e) but matching VID:DID `1002:67A0` (Hawaii XT
GL). Resizes BAR0 to 16 GB at `0x1C00000000` and BAR2 to 8 MB at
`0x2000000000`. The bridge chain rewrite covers the union of both.

**(g) Flush log to NVRAM.** The module builds a trace string during
stages 1–4 and ExitBootServices (`[DA] … [DF]` prefixed lines) and
commits it to the `ReBarBootLog` NVRAM variable at the end. This gives a
forensic record for every boot, readable from the OS after reboot.

## Design principles worth stating explicitly

- **No cursor, no auto-bump.** Every BAR target address is a compile-time
  constant. There is no "next free address" pointer that migrates placement
  across boots. Determinism is the point.
- **No kernel workaround dependencies.** The module is correct alone.
  `pci=realloc`, `pci=nocrs`, `pci=nommconf` are not required on Linux.
- **Bus-agnostic discovery.** Every scan visits dev 0–31 × func 0–7 with
  proper multi-function detection (header bit 0x80). Hardcoding a bus
  address has bitten this project multiple times (see CHANGELOG.md).
- **Fail closed, log everything.** If any stage cannot complete, the
  module logs the reason and exits the stage. It does not try to "fix up"
  with heuristics.

# Static BIOS patches

This directory contains the **static hex patches** applied to the Gigabyte
GA-990FX-Gaming BIOS image, complementary to the runtime DXE module in
[`../990fxOrchestrator/`](../990fxOrchestrator/).

The module does *runtime* reprogramming (DSDT in-memory, CPU NB MMIO
window, PCIe Link Disable, BAR sizing); these patches do *compile-time*
reprogramming of AMI's own DXE modules (PciRootBridge, PciBus,
AmiBoardInfo, AmdAgesaDxeDriver) to stop them fighting the runtime work.

## Files

| File              | Purpose                                           |
|-------------------|---------------------------------------------------|
| `990fx.patches`   | UEFIPatch pattern file — split into Block 1 (patches currently applied in FTP39.F2, binary-verified) and Block 2 (patches tested historically but not present in FTP39, kept commented out for reference) |

## How this file is organised

`990fx.patches` is organised by **binary ground truth**, not by intent.

- **Block 1 — applied.** Each entry was verified against the
  uncompressed PE32 body of the target FFS in the FTP39.F2 image by
  counting occurrences of both the ORIGINAL and REPLACEMENT byte
  sequences. An entry is listed in Block 1 only if original=0 and
  replacement≥1 — i.e. the final byte state is *provably* the patched
  one. 14 patches across four FFS modules (PciRootBridge, PciBus,
  AmdAgesaDxeDriver, AmiBoardInfo).
- **Block 2 — tested.** Hex patches from earlier iterations of the
  work. Real GUIDs and real byte patterns used during testing. On
  FTP39 none of these match: the regions they targeted have been
  rewritten by the Block-1 patches (pattern drift), or the approach
  was abandoned in favour of a different one. Commented out so
  UEFIPatch will not attempt to apply them.

This split is deliberate. An "offensive vs defensive" taxonomy describes
intent; the Block 1 / Block 2 split describes what is *actually present*
in the validated image. For a porter deriving patches against a new
BIOS revision, the latter is the useful invariant.

## AGESA patches — highest-risk subset

Block 1.3 contains three patches to `AmdAgesaDxeDriver`
(GUID `6950AFFF-6EE3-11DD-AD8B-0800200C9A66`). They modify the module
that initializes memory training and chipset glue. Misapplication — a
hex pattern matching at an unintended site on a neighbouring BIOS
revision — can brick a cold boot, and the failure mode is silent (no
POST, no debug LED).

Before flashing an AGESA-patched image on any BIOS version other than
the one this file was derived against:

1. Run `UEFIPatch.exe <image> 990fx.patches` in diagnostic mode (no
   write) and confirm every Block-1.3 pattern matches **exactly once**.
2. Zero matches on any AGESA pattern means the pattern has drifted —
   re-derive before flashing, do not guess.
3. Multiple matches means the pattern is no longer unique — the patch
   would hit unintended code. Re-derive with more context bytes.

AGESA Patch 14 has an intentionally short pattern (2 bytes). Even in
the validated FTP39 image its replacement happens to match twice — one
is the patch, the other an unrelated alignment NOP. Apply only against
the AGESA FFS, and only after confirming the original matches exactly
once.

## Applying

```
UEFIPatch.exe <bios_image.bin> 990fx.patches
```

Every Block-1 pattern should match exactly once. If a pattern does not
match:

- The BIOS version is not one this file was derived against. Check the
  target FFS bytes with UEFITool and compare against the patterns in
  `990fx.patches`.
- Someone else has already patched this image. Re-derive from a stock
  vendor image.

## Critical: do not flash UEFIPatch output directly

**Do not flash `UEFIPatch`'s output image to your BIOS chip** on AMI
Aptio IV motherboards. UEFIPatch can corrupt pad files around any module
it rewrites, and the corruption is not always visible in UEFITool.

Instead:

1. Use UEFIPatch to *verify* patterns and produce a **research** image.
2. Use **MMTool 4.50.0.23** (not newer, not older) to replace the
   `990fxOrchestrator.ffs` module inside the **original** image.
3. Re-run UEFIPatch on the MMTool output to apply the hex patches.

See [`../docs/FLASHING.md`](../docs/FLASHING.md) for the full, step-by-step
safe workflow — the one that has been validated on this board across
multiple versions without brick events.

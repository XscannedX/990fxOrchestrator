# Pre-built AmiBoardInfo module (contains the modified DSDT)

This directory ships the **complete modified PE32 body of `AmiBoardInfo`**
extracted from the validated `ftp39.F2` image. Drop it into your BIOS
with UEFITool's **Replace body** and you get, in one step:

- The 64-bit `QWordMemory` descriptors under `\_SB.PCI0` (expanded M2MX
  window to 252 GB and the "woken up" 16 EB descriptor).
- The `MALH` gate declaration the runtime driver expects to patch.
- All the other static AmiBoardInfo patches from
  [`../bios-patches/990fx.patches`](../bios-patches/990fx.patches)
  block 2.6/2.7 already applied.

No iASL, no UEFIPatch, no manual DSDT editing. Three clicks in
UEFITool.

## File

| File                            | Size     | SHA-256 (first 16 hex) |
|---------------------------------|----------|------------------------|
| `AmiBoardInfo_ftp39.pe32.bin`   | 27 008 B | (compute locally)      |

**What it is:** the raw PE32 image body of the `AmiBoardInfo` DXE module
as extracted from `ftp39.F2`. On AMI Aptio IV this module embeds the
DSDT as a data blob inside its own PE32 binary at offset `0xA30` — which
is why swapping the whole PE32 body is the simplest way to ship the
modified DSDT.

**What it is not:** a raw `.aml` DSDT file. If you want to work at the
ACPI-table granularity (e.g. disassemble with `iasl -d`, edit the ASL,
recompile), open the shipped `.pe32.bin` file and look at offset `0xA30`
for 22 661 bytes — that is the DSDT table, ACPI revision 2, OEMID
`ALASKA`, OEM Table ID `A M I`.

## Where to insert it

### Path in the BIOS tree

```
Flash image
└── BIOS region
    └── Main firmware volume (GUID 8C8CE578-8A3D-4F1C-9935-896185C32DD3)
        └── AmiBoardInfo     ← DXE driver, position 12 on ftp39.F2
            └── Compressed section
                └── PE32 image section  ← replace THIS body
```

### Exact FFS identity

| Property              | Value                                    |
|-----------------------|------------------------------------------|
| **File GUID**         | `9F3A0016-AE55-4288-829D-D22FD344C347`   |
| **File name (UI)**    | `AmiBoardInfo`                           |
| **File type**         | DXE driver                               |
| **Parent FV GUID**    | `8C8CE578-8A3D-4F1C-9935-896185C32DD3` (Main DXE volume) |
| **Position on ftp39** | Index 12 (between `PciRootBridge` at 11 and `EBC` at 13) |

## UEFITool step-by-step (the workflow most people use)

Tested on **UEFITool NE A73** and on **UEFITool 0.28.0**. Either works.
NE A73 is preferred because it parses the embedded ACPI table and gives
slightly nicer validation on the replace step.

### 1. Open your BIOS image

`File → Open image file…` → pick your `.F2` or `original.bin` dump.

The tree expands to the AMI Aptio IV firmware volumes. Leave the flash
descriptor and ME region collapsed; all the action is in the BIOS
region's main DXE volume.

### 2. Find the AmiBoardInfo module

Either:

- `File → Search` (`Ctrl+F`) → tab **"GUID"** → paste
  `9F3A0016-AE55-4288-829D-D22FD344C347` → Enter.
- Or expand the tree manually: `BIOS region → 8C8CE578-… (DXE volume)
  → AmiBoardInfo`.

Click on `AmiBoardInfo`. The right-hand pane shows its properties:
type = *DXE driver*, size roughly 36 KB (varies per BIOS rev).

### 3. Drill down to the PE32 body

Expand:

```
AmiBoardInfo
└── Compressed section (Tiano)
    └── PE32 image section        ← select this node
```

Select the `PE32 image section` node (not `Compressed section`, not
`AmiBoardInfo`).

### 4. Replace body

Right-click the PE32 node → **Replace body…** → browse to
`AmiBoardInfo_ftp39.pe32.bin` → Open.

UEFITool shows a confirmation with the old size vs new size. Sizes
should be close (±a few hundred bytes). Accept.

### 5. Save

`File → Save image file as…` → write out a new `.F2` file. **Do not
overwrite** your original — pick a new name like
`FTP39_with_pe32.F2`.

### 6. Verify (optional but recommended)

Reopen the saved file in UEFITool and repeat step 2. The
`AmiBoardInfo` module should now show size = 27 008 B (± UEFITool
recomputes compression and volume padding so minor drift is normal).

Search by text for the string `ALASKA` → exactly one hit, inside
`AmiBoardInfo` at the DSDT OEMID position. That hit's surrounding
bytes should match the "QWordMemory #1 max = 256 GB" pattern:

```
FF FF FF FF 3F 00 00 00
```

If UEFITool finds that byte sequence inside `AmiBoardInfo`, the
modified DSDT landed correctly.

## When this file is not the right choice

Ship-your-own-DSDT workflows where this pre-built body is the *wrong*
file to use:

- **Different BIOS base (not FTP39).** AmiBoardInfo contains
  board-specific data beyond the DSDT — SIO resource tables, hardware
  monitor maps, USB mux routing. Swapping this file into (e.g.) FTP26
  would also overwrite those, with unpredictable results. Only use
  this file if your starting image is `ftp39.F2` or an image
  byte-identical to FTP39's pre-patch AmiBoardInfo.
- **Different motherboard revision or different vendor.** Obvious —
  other boards have their own AmiBoardInfo, this one is 990FX-Gaming
  Rev 1.0 / Rev 1.1 BIOS only.
- **You need to modify the DSDT further** (your own `_CRS`, additional
  QWordMemory descriptors, a different MALH gate, etc.). In that case:
  use UEFIExtract to get the DSDT out (at offset `0xA30` of this file,
  or out of your own AmiBoardInfo), disassemble with `iasl -d`, edit
  the ASL, recompile, and inject your new `.aml` back as a DSDT-body
  replace in UEFITool NE.

For those cases the pre-built PE32 body here is the *starting point*,
not the *finished product*.

## What if a later BIOS revision ships a different AmiBoardInfo layout

If Gigabyte publishes a BIOS revision after the one this project was
validated against and the DSDT layout inside AmiBoardInfo shifts (new
offset, renamed objects, different descriptor order), then this
pre-built file is no longer the right drop-in. You will need to:

1. Extract the **stock** AmiBoardInfo from the new vendor BIOS.
2. Re-derive your DSDT modifications against the new DSDT bytes.
3. Rebuild a fresh `AmiBoardInfo_<ftpXX>.pe32.bin` from that work.

The runtime driver (`990fxOrchestrator.c` → `PatchDsdtMalh`) searches
for the byte pattern `08 4D 41 4C 48 00` (NameOp + "MALH" + ZeroOp)
and self-adjusts to where it finds it — so the MALH runtime patch
does not require recompilation on BIOS revisions that only move
offsets. The static QWordMemory descriptors (the 252 GB one, the
16 EB one) do need to be re-applied by hand.

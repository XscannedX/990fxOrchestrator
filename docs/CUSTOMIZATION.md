# Customizing the module for your own hardware

> **Everything in this document is untested.** The module has been
> verified on exactly one hardware configuration (Gigabyte GA-990FX-Gaming
> Rev 1.0 + Rev 1.1 BIOS + FX-8350 + the specific 4-GPU stack listed in
> the main README). The steps below are the author's best reconstruction
> of what a port to a different board / different GPU / different chipset
> would require, but no such port has been done, so the instructions are
> a map, not a guarantee. Treat every port as a research project with a
> real risk of bricking the target board. Keep your CH341A ready.
> (see [`RECOVERY.md`](RECOVERY.md)) from the first flash attempt.

This guide walks through adapting `990fxOrchestrator` to a GPU or a board
that isn't the exact validated stack. It assumes you already understand
[`ARCHITECTURE.md`](ARCHITECTURE.md) and [`MMIO64_LAYOUT.md`](MMIO64_LAYOUT.md).

There are two levels of change, in increasing difficulty:

1. **Swap in a different GPU of the same vendor.** Easy — one literal to
   change, one constant if the BAR size differs.
2. **Add a new GPU family or change board.** Harder — discovery logic and
   placement constants both need review.

Start with level 1. If your situation requires level 2, read all of it.

---

## Step 0 — collect your GPU's PCI identity

Before touching the source, you need four numbers per card you want the
module to manage.

**On Linux (pre-flash, with the card visible, even at 256 MB BAR):**

```bash
lspci -nn -vv -s <BDF>
```

where `<BDF>` is the bus:device.function of your card (e.g. `25:00.0`).

Example output for the Arc A770:

```
25:00.0 VGA compatible controller [0300]: Intel Corporation
        DG2 [Arc A770] [8086:56a0] (rev 08)
        ...
        Region 0: Memory at 80000000 (64-bit, non-prefetchable) [size=16M]
        Region 2: Memory at a0000000 (64-bit, prefetchable) [size=256M]
        Capabilities: [420 v1] Physical Resizable BAR
                BAR 2: current size: 256MB, supported: 256MB 512MB 1GB 2GB 4GB 8GB 16GB
```

From this you extract:

| Item                          | Example       | Where to get it                        |
|-------------------------------|---------------|----------------------------------------|
| Vendor ID                     | `0x8086`      | `[8086:56a0]`, first half              |
| Device ID                     | `0x56A0`      | `[8086:56a0]`, second half             |
| Which BAR holds VRAM          | `2`           | The large prefetchable 64-bit region   |
| Max ReBAR size supported      | `16GB`        | `Capabilities: Physical Resizable BAR` |
| Class code                    | `0x03`        | `[0300]`, high byte → "display ctlr"   |

Do this for every GPU you want the module to manage.

**If your card has no `Physical Resizable BAR` capability**, this module
cannot resize its BAR — there is nothing to resize, the BAR is fixed in
silicon (sometimes). The module can still *place* the fixed-size BAR at a deterministic
address above 4 GB if placement is what you need (useful for 16 GB Teslas
that ship with a 16 GB BAR hardwired), but you skip the `REBAR_CTRL` write.

---

## Step 1 — swap in a different GPU of the same vendor

This is the scenario: you have a different AMD card (say, a MI25 or a
Radeon Pro W6800) and you want this module to size and place it.

### 1a. Change the VID:DID match literal

Open `990fxOrchestrator/990fxOrchestrator.c`, find the `ResizeAmdGpuBars` function (search for
`ResizeAmdGpuBars`). The discovery loop contains:

```c
if ((vd & 0xFFFF) != 0x1002) continue;      // VID: AMD
if ((UINT16)(vd >> 16) != 0x67A0) continue; // DID: Hawaii XT GL (W9170)
```

Replace `0x67A0` with your card's DID. If your card is not AMD,
replace `0x1002` with your vendor ID too (but then see Step 2: you
probably also need a new discovery function rather than editing this
one, since the BAR layout assumption downstream is AMD-specific).

The equivalent block for the Arc lives in `ResizeIntelGpuBars`:

```c
if ((dVidDid & 0xFFFF) != 0x8086) continue;  // VID: Intel
...
if (cls != 0x03) continue;                   // Class: display controller
```

That routine accepts any Intel display-class device, so swapping to a
Battlemage or newer Arc requires *no* source change for discovery — just
confirm your card actually advertises a Resizable BAR cap.

### 1b. Confirm or change the target BAR index

Near the top of `990fxOrchestrator.c` the placement constants are grouped:

```c
#define ARC_BAR2_TARGET          0x1800000000ULL   // Arc A770 BAR2 (LMEM)
#define W9170_BAR0_TARGET        0x1C00000000ULL   // W9170 BAR0
#define W9170_BAR2_TARGET        0x2000000000ULL   // W9170 BAR2 (doorbell)
```

And inside each resize routine, a filter selects which BAR to touch:

```c
// In ResizeIntelGpuBars
if (barIndex != 2) continue;   // Arc VRAM lives on BAR2

// In ResizeAmdGpuBars (pseudocoded)
// The code writes BAR0 and BAR2 explicitly by offset.
```

**If your card's VRAM is on a different BAR index**, change the filter
and/or the offset math to match. The BAR index is the one you identified
in Step 0.

### 1c. Confirm the BAR size

The resize code reads `REBAR_CAP` and picks the maximum advertised size.
If your card maxes out at a size different from 16 GB, the placement
will still work, but the address you picked needs to be aligned to that
size and the bridge window needs to be big enough.

For example, a card that caps at 8 GB could sit at `0x1C00000000` (still
16 GB-aligned, just uses half the slot) — but then the bridge window
can shrink to 8 GB too.

### 1d. Rebuild and verify

```
cd edk2
build_990fxo.bat
```

Confirm the new VID:DID appears as an ASCII literal in the built `.efi`:

```powershell
$b = [IO.File]::ReadAllBytes("Build\...\990fxOrchestrator.efi")
[Text.Encoding]::ASCII.GetString($b) | Select-String "YOUR:DID"
```

Then inject via MMTool, flash, boot, and read the NVRAM log (see
[`DEBUG_CODES.md`](DEBUG_CODES.md)).

---

## Step 2 — add a new GPU family or change board

This is the scenario: you have a card from a vendor the module doesn't
know about, or your board has different bridges, or you want to manage
a fifth GPU.

### 2a. Decide the placement address

Look at `lspci -vvv` on a boot with the card forcibly allocated at its
native BAR size (you may need `pci=realloc` temporarily just to see what
the OS would do). Identify:

- What BARs the BIOS already placed, and where.
- What gaps exist in the existing MMIO64 map.
- Whether your card's BAR is 16 GB-aligned requirement (larger-aligned
  if bigger).

Pick a free, aligned slot. For example, with the current map, slot
`0x2100000000 – 0x24FFFFFFFF` (16 GB) is free. Add a constant:

```c
#define MYCARD_BAR2_TARGET     0x2100000000ULL
```

### 2b. Add a new Resize function

Copy `ResizeIntelGpuBars` as a template (it is the cleanest of the two
existing routines). Rename it, change:

- The VID match literal.
- The class or DID filter (whichever is appropriate).
- The target BAR index filter.
- The target address literal.

Call it from `OnExitBootServices` in the same sequence as the existing
resize routines.

### 2c. Extend the Northbridge window if needed

The compile-time `NB_WINDOW_BASE` / `NB_WINDOW_LIMIT` define the maximum
MMIO64 address the module can route. On the validated stack this is
256 GB, which is generous. If you are adding so many BARs that 256 GB
is tight, the Northbridge's MMIO Base/Limit registers have more headroom
— consult the AMD Family 15h BKDG.

### 2d. Board port — changing the Northbridge

If you are not on an SR5690-class Northbridge, the `ProgramCpuMmioWindow`
routine needs wholesale replacement. Every chipset has different MMIO
routing registers. Examples of what needs to change:

- **Intel LGA 2011 / X79 / X99:** MMIOH registers in the IIO, PCI config
  space device `0:5:0` register `0x40`-ish range.
- **AMD AM4 / TRX40:** MMIO routing is largely handled by PSP / AGESA,
  less likely to need this module at all.
- **Other AMD Family 15h boards (890FX, 970):** often similar to SR5690
  but with different HT gate register. Test.

In every case, the sequence is: find the register(s) that declare
"these address ranges decode as MMIO and are routable to the PCIe root
complex", widen them to include your new range, write them at
ExitBootServices.

### 2e. Board port — changing the DSDT patch

The `MALH` name is specific to AMI Aptio IV DSDTs from Gigabyte's 990FXG
line. On another board the equivalent gate has a different name.

1. Dump your DSDT: `acpidump -n DSDT -o dsdt.dat` then
   `iasl -d dsdt.dat` to get `dsdt.dsl`.
2. Search for ACPI conditional expressions around the MMIO64 window
   resource descriptor. Look for `Name(...,Zero)` that is referenced by
   an `If(...)` guarding a `QWordMemory(...)` in a `_CRS` method.
3. Either patch that name at boot the same way `MALH` is patched, or if
   there isn't one, inject a new `_CRS` override with the 64-bit window
   you want the OS to see.

This is the hardest part of a board port. Expect a week of work if
you've never done ACPI reverse engineering before.

---

## Placement pitfalls

These were learned the hard way in this project. Read before picking a
new address.

### The "BAR1 + BAR3" trap

Many enterprise cards have:

- A large BAR1 (16 GB on Tesla P100) at address `X`.
- A tiny BAR3 (32 MB on Tesla P100) at address `X + 16 GB`, aligned to
  its own size.

The tiny BAR3 consumes a full 16 GB slot because BIOS alignment rules
put it at the next 16 GB boundary, and no other 16 GB BAR can share
that slot without overlapping. If you see a 32 MB region sitting at
`0xC_00000000` in `lspci`, it does **not** mean `0xC_00000000 + 32 MB
.. 0x10_00000000` is free. It means `0xC_00000000 .. 0x10_00000000`
is claimed.

### The "kernel decodes differently than I wrote" trap

If the bridge window's prefetchable base is not aligned to the window
size, or the window is larger than what the bridge can actually decode
(AMD 990FX bridges have a 36-bit upper limit on some revisions), Linux
will silently decode a different address. `lspci -vvv` will show the
bridge window at e.g. `0x1440000000` when you asked for `0x1400000000`,
and downstream devices will land somewhere you didn't expect.

Always cross-check `/proc/iomem` post-boot against what your module
wrote. If they disagree, the kernel is telling you the bridge cannot
actually decode what you asked for.

### Multi-function bridges

Repeatedly bitten in this codebase: *every* PCI scan of bus 0 must
iterate `func = 0..7` with standard multi-function detection. On this
board alone, three critical bridges live at non-zero functions
(`00:15.3`, various GPP ports). See the same note in
[`HARDWARE_NOTES.md`](HARDWARE_NOTES.md).

---

## A minimal worked example

Say you have a **Radeon RX 7900 XTX** (VID `1002`, DID `744C`) you want
to put in the slot currently occupied by the W9170. The card supports
ReBAR up to 16 GB. Its VRAM is on BAR0.

Edits in `990fxOrchestrator.c`:

```diff
- if ((UINT16)(vd >> 16) != 0x67A0) continue;  // Hawaii XT GL (W9170)
+ if ((UINT16)(vd >> 16) != 0x744C) continue;  // Navi 31 (RX 7900 XTX)
```

Update the log banner strings from `W9170` to `RX 7900 XTX` (cosmetic).

Update the constants if you want clarity (cosmetic; addresses can stay):

```diff
-#define W9170_BAR0_TARGET     0x1C00000000ULL
+#define AMDGPU_BAR0_TARGET    0x1C00000000ULL
```

Rebuild, inject, flash. The module should discover the card, resize it,
and place it at `0x1C00000000` exactly as it did with the W9170.

If instead you keep the W9170 *and* add the RX 7900 XTX as a fifth GPU,
pick a new slot (`0x2100000000` as suggested above), clone
`ResizeAmdGpuBars` into `ResizeNavi31Bars`, and add the call.

---

## Getting help

If your port doesn't work, before filing an issue, gather:

- `lspci -vvv` from a stock boot (no module) and from a module-enabled
  boot.
- `/proc/iomem` post-boot.
- The `ReBarBootLog` NVRAM variable dump (see
  [`DEBUG_CODES.md`](DEBUG_CODES.md)).
- Your modified `990fxOrchestrator.c` (the diff against main is enough).
- Your board model, BIOS version, and the `lspci -tv` tree.

With those, someone can usually tell within one reading whether the
module saw the card, what it tried to write, and what the bridge
chain did with those writes.

# Flashing the module into the BIOS

This is destructive. Read [`RECOVERY.md`](RECOVERY.md) first. Do not
flash without owning a CH341A programmer and a SOIC8 clip.

## Tools you need

| Tool                  | Version tested | Purpose                          |
|-----------------------|----------------|----------------------------------|
| **MMTool**            | 4.50.0.23      | Replace the FFS inside the image |
| UEFITool              | new engine     | Inspection only, don't use its replace-with-pad |
| Efiflash.efi          | Gigabyte-bundled | Flash the image from DOS/UEFI shell |
| `uuidgen` or any GUID tool | —         | If you want a fresh `FILE_GUID`  |

**Do not use UEFIPatch output for flashing.** UEFIPatch produces an image
whose pad files have been reshuffled; flashing that directly has been
observed to corrupt adjacent modules on AMI Aptio IV. Use MMTool
**4.50.0.23 exactly** (the 5.x line has its own quirks on older firmwares).

## Workflow

```
┌──────────────────┐     ┌────────────┐     ┌────────────┐
│ Build the .ffs   │ ──► │ Open the   │ ──► │ Replace by │
│ (edk2 build)     │     │ BIOS image │     │ GUID       │
└──────────────────┘     │ in MMTool  │     └─────┬──────┘
                         └────────────┘           │
                                                  ▼
                                          ┌───────────────┐
                                          │ Save new      │
                                          │ image         │
                                          └──────┬────────┘
                                                 │
                                                 ▼
                                         ┌────────────────┐
                                         │ Flash with     │
                                         │ Efiflash from  │
                                         │ DOS boot USB   │
                                         └────────────────┘
```

## Step-by-step

### 1. Produce the `.ffs`

```powershell
cd edk2
.\build_990fxo.bat
.\gen_ffs_990fxo.bat
```

Output:
`edk2\Build\ReBarUEFI\RELEASE_VS2022(your version)\X64\yourfolder\990fxOrchestrator\990fxOrchestrator\OUTPUT\990fxOrchestrator.ffs`

Verify a size in the ballpark of 38 KB. The binary markers should
include `v9.5 LOG` — see [`DEBUG_CODES.md`](DEBUG_CODES.md) for the
verification command.

### 2. Make a reference copy of your current BIOS

Before touching anything, **read the current BIOS off the chip** with
your CH341A:

1. Power the machine off, unplug it.
2. Clip the SOIC8 to the BIOS chip (it's usually labeled Winbond
   `25Qxx` or MX25Lxx, a little rectangle near the battery).
3. Use a CH341A reader (e.g. the asprogrammer utility, or `flashrom -p
   ch341a_spi`) to save the chip content as `original.bin`.
4. Store that file somewhere that is not on the target machine.

Any flashing utility that writes the chip can also brick it mid-write.
The off-target copy is your safety net.

### 3. Open your source BIOS in MMTool

Your source can be either:

- The official Gigabyte `.F2` file from the manufacturer's website,
  loaded directly.
- Your `original.bin` dump from step 2, which is 100% what's on the chip
  right now.

Use the latter if you're not sure the public BIOS matches exactly what
you've got.

In MMTool: **File → Load Image** → select the `.F2` or `.bin`.

### 4. Find the slot to replace

Two choices:

**(a) Replace the existing 990fxOrchestrator module** if one is
already present. Search in MMTool for GUID
`adf0508f-a992-4a0f-8b54-0291517c21aa`. If you find it, select it and
continue to step 5.

**(b) Replace a small unused DXE module** if no prior ReBar is in the
image. Pick something the board does not need — a legacy option ROM for
hardware you don't have works. Note the GUID of the slot you're
replacing. **If you choose this path, change `FILE_GUID` in
`990fxOrchestrator/990fxOrchestrator.inf` to match the slot's GUID, rebuild, regenerate
`.ffs`, and come back.**

#### 4.1. Critical: position in the DXE volume

**On this board the module must sit immediately above `PciBus` in the
main DXE firmware volume.** Dispatch order in EDK2 follows FFS order
within a volume (modulo DEPEX constraints); placing the module *after*
PciBus means PciBus has already run its allocation pass by the time the
module gets a chance to pre-scan and Link-Disable oversized devices, so
the whole mechanism collapses.

The **validated position on `ftp39.F2`** is:

```
Index   Module name
------  --------------------------------
  8     ReFlash
  9     990fxOrchestrator      ← THE MODULE
 10     PciBus                 ← immediately below
 11     PciRootBridge
 12     AmiBoardInfo           ← contains the DSDT (see ../dsdt/)
```

**Verification in MMTool:** after replacing the slot, scroll through
the DXE volume (main FV after `AmdAgesaDxeDriver`) and confirm
`990fxOrchestrator` appears directly **above** `PciBus`.

If you used option **(a)** (replace a prior ReBarDxe/990fxOrchestrator
already in the image) the existing slot is at the right position —
MMTool does not re-sort. Done.

If you used option **(b)** (replace an unused module) the new slot
inherits the position of whatever you displaced, **which is probably
wrong**. Two options to fix:

1. **Pick a different donor slot** that sits between the AGESA driver
   and PciBus. In `ftp39.F2` the `9BD5C81D-096C-4625-A08B-405F78FE0CFC`
   slot at index 1 (adjacent to AGESA) is a common candidate. List
   candidate slots with:

   ```
   UEFIExtract.exe <your_bios.bin>
   # then inspect <your_bios>.dump/…/3 8C8CE578-8A3D-4F1C-9935-896185C32DD3/
   # folders named "N <GUID-or-name>" are listed in FFS order.
   ```

   Any slot that exists *above* the one labelled `PciBus` is a valid
   donor as long as the original module is unused on your config.

2. **Rebuild the FV from scratch** with the module inserted at the
   desired position. This is a more invasive workflow (requires
   `GenFv` or `FMMT`, and tight attention to padding) and is not
   needed for the common case. Most users get good mileage out of
   option 1.

As a sanity-check, search for the string `990fxOrchestrator v9.5 LOG`
inside your saved `.F2` file: it should appear **before** the string
`PciBus` by file offset. If the order is inverted, the module will
load but its pre-scan will run *after* PciBus has already assigned the
BARs — visible in the NVRAM log as all-zero bridge chain reads and no
BAR resize.

### 5. Replace

In MMTool, with the target slot selected:

**Replace → Choose File** → select your `990fxOrchestrator.ffs`.

MMTool will ask for confirmation. Accept. The image is now modified
in memory.

**File → Save As** → write out a new `.F2` file, e.g.
`FTP39_v95.F2`. Do **not** overwrite your original.

### 6. Flash

Prepare a FAT32 bootable USB stick with DOS (the Gigabyte BIOS update
utility runs on a DOS boot). Copy onto it:

- `Efiflash.efi` (from the Gigabyte package or a prior working update).
- Your new `.F2` image (`FTP39_v95.F2`).

Boot the machine from the USB. At the DOS prompt:

```
Efiflash FTP39_v95.F2
```

Answer "yes" to the confirmation. The utility writes the SPI flash and
reboots.

### 7. First boot after flash

The first POST with a freshly flashed BIOS can take 10–30 seconds
longer than normal — the BIOS is re-initializing NVRAM. Do not power
off.

If the board does not POST at all (no video, no POST codes, no beeps),
**reach for your CH341A**. Restore `original.bin`, reset CMOS for
safety, boot, confirm the machine is back to the pre-flash state, then
diagnose what went wrong before attempting another flash.

### 8. Verify the module is active

After the first successful boot into the OS:

```bash
# Linux
sudo cat /sys/firmware/efi/efivars/ReBarBootLog-b00710c0-a992-4a0f-8b54-0291517c21aa \
  | strings | head -20
```

You should see `=== 990fxOrchestrator v9.5 LOG ===`. If you do, the
module ran. Then:

```bash
lspci -vvv
```

Check the target GPU's BARs are at the configured addresses (see
[`MMIO64_LAYOUT.md`](MMIO64_LAYOUT.md)).

## Common failure modes

| Symptom                                     | Likely cause                                 | Recovery                                 |
|---------------------------------------------|----------------------------------------------|------------------------------------------|
| No POST, no video, no fans spin-down        | Image corrupted mid-flash, or replaced a critical module | CH341A restore to `original.bin`         |
| POST reaches 0xD1 on LCD and hangs          | DSDT pattern didn't match (BIOS update changed layout) | Rebuild the module with updated pattern |
| OS boots, `lspci` shows 256 MB BAR on GPU   | Module ran but the resize skipped your card  | Check NVRAM log, verify VID:DID in code  |
| OS hangs on amdgpu/nvidia module load       | BAR placed where it conflicts with another BAR | Re-check [`MMIO64_LAYOUT.md`](MMIO64_LAYOUT.md) collisions |
| `ReBarBootLog` variable doesn't exist       | Module not loaded (wrong GUID / not injected) | Re-open image in MMTool, confirm the slot |

## Safety rules

1. **Never flash without a verified `original.bin`** in your hand.
2. **Never flash over Ethernet or from a virtual machine.** Run
   everything locally on the target.
3. **Stable power.** A UPS or at least "don't flash during a
   thunderstorm" is adequate for a desktop board. A brown-out
   mid-write produces a partial image which may or may not boot.
4. **One variable at a time.** If you're adding new hex patches *and*
   replacing a module *and* changing the DSDT, do each in a separate
   flash cycle with boot verification between. Diagnosing a combined
   failure is brutal.

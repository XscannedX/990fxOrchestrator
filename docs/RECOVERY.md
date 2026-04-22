# Brick recovery with CH341A + SOIC8 clip
(if you dont want the read this there are a lot of videos and materials already posted online).

If a flash goes wrong, this is how you recover.

## Tools required

| Item                   | Notes                                              |
|------------------------|----------------------------------------------------|
| CH341A programmer      | The black-and-red USB stick, ~€5                   |
| SOIC8 test clip        | Pomona 5250 or clone, ~€8–15                       |
| 3.3 V SPI flash chip pinout knowledge | See below                           |
| Known-good BIOS dump (`original.bin` or a vendor `.F2`) | Stored off-target |
| Host PC running Linux or Windows | Any recent OS works                      |
| `flashrom` (Linux) or AsProgrammer / NeoProgrammer (Windows)                | 
  
Total hardware cost: **~€15**. If you are flashing BIOS modifications
without owning this, stop reading this repo and go acquire the hardware
first.

## Identify the flash chip

On the Gigabyte GA-990FX-Gaming Rev 1.1 the BIOS flash is a standard
8-pin SOIC package, usually a **Winbond W25Q64** or compatible (8 MB =
64 Mbit). It is located near the CMOS battery, close to the southbridge.

The chip is labeled on top:

```
┌──────────────┐
│ W25Q64FVSSIG │   ← model
│ 1847 G       │   ← date code + lot
└──────────────┘
```

Pin 1 is marked by a small dot or notch in a corner. All pins in order:

```
         ┌─────┐
    /CS ─┤1   8├─ VCC (3.3 V)
    DO  ─┤2   7├─ /HOLD
   /WP  ─┤3   6├─ CLK
    GND ─┤4   5├─ DI
         └─────┘
```

## Preparing to clip

1. **Power off the machine and unplug the PSU from the wall.** Not just
   the soft power button — the PSU must be fully disconnected.
2. **Remove the CMOS battery** to fully de-power any standby rails.
3. Confirm there is no 3.3 V on VCC of the chip with a multimeter before
   clipping. The CH341A will supply 3.3 V itself.

## Clipping

The SOIC8 clip has 8 tiny spring-loaded contacts. Align:

- Clip pin 1 (red wire by convention, or marked on the clip) to chip
  pin 1 (the dot).
- All eight contacts must touch their matching chip leads. Wiggle the
  clip gently until you feel it seat.

A bad clip is the #1 cause of "won't read" errors — **if the read
fails, it is almost always the clip**.

## Reading first, writing second

**Always read the chip before writing.** Even if you think you know
what's on it, confirm:

```bash
# Linux, flashrom:
sudo flashrom -p ch341a_spi -r current.bin

# Or on Windows, AsProgrammer:
#   1. Detect → finds the chip model
#   2. Read → save as current.bin
```

Check the read:

- File size is exactly 8388608 bytes (for 64 Mbit chip).
- Reading twice produces identical files (`sha256sum` both).
- `UEFITool` can open `current.bin` and shows a recognizable BIOS
  structure.

If any of those fail, re-seat the clip and read again. Do not write
until reads are clean.

## Writing recovery image

With a good read confirmed and a known-good image in hand:

```bash
# Linux, flashrom:
sudo flashrom -p ch341a_spi -w original.bin -V

# -V gives verbose output, so you see "erase OK", "write OK", "verify OK"
# in sequence. All three must say OK.
```

```
# Windows, AsProgrammer:
#   1. Open → select original.bin
#   2. Unprotect (if the chip has status register protection)
#   3. Erase
#   4. Write
#   5. Verify
```

**The verify step is not optional.** A successful write that fails
verify is a brick — the chip took the writes but the content is not
what you sent. Re-seat the clip and write again.

Write time for an 8 MB chip over CH341A: about **3–5 minutes**. Don't
unplug during the write.

## Post-recovery

1. **Unclip** the SOIC8.
2. **Reinstall the CMOS battery.**
3. **Plug the PSU back in.** Before pressing power, hold the clear-CMOS
   jumper for 10 seconds or short the clear-CMOS pads (check the
   manual). This ensures NVRAM is clean — a flashed BIOS with stale
   NVRAM state can appear bricked when it isn't.
4. **Press power.** The board should POST normally.
5. Go into BIOS setup, **Load Optimized Defaults**, save, reboot.

If the board still doesn't POST after a confirmed-verified flash,
the flash chip itself may be damaged (rare but possible with repeated
aggressive clipping). At that point the recovery is either chip desolder
+ reball or replacement motherboard.

## Protecting yourself from future bricks

- **Keep `original.bin` somewhere you can always reach it.** Multiple
  copies: USB stick, cloud, phone. "On the target machine's hard drive"
  doesn't count — you may not be able to boot it.
- **Label your recovery images.** Include the board model, BIOS
  version, and date in the filename. `gigabyte-ga990fx-gaming-rev11_FTP39_v95_20260421.bin`
  beats `bios.bin`.
- **Test your CH341A workflow before you need it.** Clip, read, verify
  the read matches a known image. Do this once, without pressure,
  before flashing anything experimental. When the board is bricked is
  not the time to discover your clip contacts are bent.
- **One change per flash.** If the recovery image is yesterday's known
  good, getting back to "yesterday" is unambiguous. If it's four
  changes old, you lose a day re-doing all of them.

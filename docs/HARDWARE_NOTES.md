# Hardware notes

Lessons learned from nine revisions of this module, one per non-obvious
platform behaviour. Treat this as a list of traps, not a manual.

## SB900 root port is at function 3

On the Gigabyte GA-990FX-Gaming Rev 1.0, the SB900 PCIe root port that
drives the chipset-side riser lives at bus-device-function **`00:15.3`**.
Not `.0`. Not `.1`. Function **3**.

Every PCI bus scan in this module explicitly iterates `func = 0..7` with
the standard multi-function detection (header type byte, bit 0x80 on
function 0). The same bug has been fixed three times in this codebase
(v7, v9.4, v9.5) each time a different discovery loop forgot to scan
beyond function 0. If you add a new discovery loop, it must also scan
all eight functions or it will silently miss the chipset-side riser.

## SR5690 HT gate

The Northbridge → PCIe root-complex bridge will not forward 64-bit MMIO
transactions until the two low bits of register `0x94` on device `00:00.0`
are set:

```
setpci -s 00:00.0 94.L=00000003
```

The module does this at ExitBootServices automatically. Doing it from
Linux after boot has no effect — the BIOS resource allocator has already
failed by then.

## BIOS menu options — summary

See [`BIOS_SETUP.md`](BIOS_SETUP.md) for the full treatment. The
short version:

- **Do not toggle CSM** in the menu. On this board it is effectively
  cosmetic — the static BIOS patches plus this module handle
  allocation regardless of its state. Leave at default.
- **Do not enable HPCM** (High Performance PCI Config Mode, or the
  local equivalent). It causes instability with the module active.
- **Do not use `pci=realloc`, `pci=nocrs`, or `pci=nommconf`** on the
  kernel command line. The module makes them unnecessary and, worse,
  `pci=realloc` can silently re-place BARs at different addresses
  than the ones the module carefully chose.

The entire point of the module is that *you should not have to
configure anything* beyond loading Optimized Defaults after the flash.

## AMI DSDT `MALH` cap

The AMI DSDT in Gigabyte's 990FXG BIOS line declares a `MALH` named
object, initialized to `Zero`. It acts as a gate: when `Zero`, the
MMIO64 window descriptor that immediately follows is suppressed. When
`One`, the descriptor is exposed to the OS.

Stock BIOS keeps `MALH = Zero`. The module patches it in-memory to `One`
at boot. Without this, Linux allocates BARs successfully above 4 GB but
marks the window as unreachable, and devices fail to bind.

If a future BIOS update shifts the `MALH` offset or renames the object,
the pattern search in `rebarInit` needs to be updated — it looks for
the specific byte sequence of an ACPI Name declaration with the
four-character name `MALH`.

## Tesla P100 — tape on PCIe pins 3, 4, 5

The NVIDIA Tesla P100 is a server GPU. On a consumer / prosumer
motherboard it will **not initialize** out of the box, even if the
BAR problem is solved. The card reads certain PCIe edge-finger pins
during power-up to detect "am I in a server chassis with the server
power sideband?" — and a motherboard that cannot assert those signals
makes the card refuse to come up.

The community fix is mechanical and permanent: cover pins **3, 4, and
5** on the PCIe edge connector with a strip of Kapton tape (polyimide,
heat-resistant) before inserting the card. Those pins are grouped
together, so a single strip ~4 mm wide does all three at once.

Numbering convention: pin 1 is at the end closer to the bracket; the
three pins are on the **top side** (component side) of the board. A
reference card image and the exact pin locations are available in
community threads — search "Tesla P100 tape mod PCIe pins 3 4 5".

Without the tape:
- The P100 will not POST, or
- It POSTs but remains at a very low power state with no driver bind, or
- The driver binds but the card is unstable under any real load.

With the tape: the card behaves normally and allocates its native
16 GB BAR as the module expects.

This has nothing to do with the ReBAR work — a stock, unmodified
BIOS cannot use the P100 at all without it. Mention this up-front
because anyone reproducing this build from a used-P100 purchase off
eBay needs to know.

**Cooling is also not optional.** The P100 is passively cooled by
design (server chassis airflow). In a standard ATX case you need a
70–80 mm blower duct taped or 3D-printed onto the rear of the card,
or a suitable aftermarket fan shroud. Without forced air, the P100
thermal-throttles within 30 seconds under load.

## Hawaii (W9170) specifics

The AMD FirePro W9170 is a Hawaii XT GL (VID:DID `1002:67A0`). Two
things matter:

**Hawaii does not support PCIe function-level reset.** If the GPU hangs
(e.g. a ring timeout under heavy workload), only a physical reboot
recovers it. Do not rely on `echo 1 > /sys/.../reset` — it will return
success and change nothing.

**Memory clock stability.** On this card, DPM level 2 (`mclk = 1500 MHz`)
corrupts inference output with subtle bit flips. DPM level 1
(`mclk = 1250 MHz`) is stable. Lock it before running compute:

```bash
echo 1 > /sys/bus/pci/devices/0000:14:00.0/pp_dpm_mclk
```

**SRBM power state.** The Hawaii SRBM (System Register Block Manager)
defaults to `auto` runtime power management. Some workloads trigger a
race where the SRBM suspends mid-transaction. Force `on`:

```bash
echo on > /sys/bus/pci/devices/0000:14:00.0/power/control
```

Both are OS-side knobs — the module does not touch them.

## Arc A770 on a PCIe 1.0 x1 riser

The riser used to relocate the Arc A770 trains at **PCIe 1.0 x1
(2.5 GT/s, ~250 MB/s)**. This is fine for a display adapter or for a
single-GPU LLM workload that fits entirely in VRAM, but it is a severe
bottleneck in any multi-GPU configuration where activations cross the
bus. The observed benchmarks on Qwen3.5-27B:

| Configuration     | pp512  | tg128  | Notes                             |
|-------------------|--------|--------|-----------------------------------|
| Arc alone         | 74.5   | 4.0    | link OK                           |
| Arc + W9170       | 60.6   | 5.2    | link x1 dominates                 |
| 2×Tesla alone     | 149.7  | 10.4   | baseline                          |
| 2×Tesla + Arc     | 104.2  | 5.4    | **Arc makes the Teslas slower**   |

The Arc is only useful alone in this topology. This is a physical-link
limitation, not a module behaviour — documented here so others don't
waste time thinking the module is at fault.

## MMTool vs UEFITool vs UEFIPatch

Only MMTool 4.50.0.23 is trusted to replace a module in the BIOS image
on this board. See [`FLASHING.md`](FLASHING.md) for the full explanation
of why UEFIPatch and UEFITool's replace-with-pad-file can corrupt the
image.

## Recovery hardware

A **CH341A programmer + SOIC8 clip** is mandatory. The flash chip on
this board is socketed-but-awkward; the clip is the fast path. Total
cost ~€15. See [`RECOVERY.md`](RECOVERY.md).

## Kernel command line

Production cmdline on the validated stack (Ubuntu 24.04, kernel 6.17):

```
ro quiet splash nowatchdog mitigations=off pcie_aspm=off pcie_port_pm=off
radeon.cik_support=0 amdgpu.cik_support=1
```

Notably **absent**: `pci=realloc`, `pci=nocrs`, `pci=nommconf`. The
module's job is to make those unnecessary. If you find yourself needing
them, something on your platform is still wrong and the module is not
fully doing its job — file an issue with your boot log.

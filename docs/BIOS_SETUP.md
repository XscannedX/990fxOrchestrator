# BIOS setup — what to touch and what to leave alone

This is the **validated configuration** on the target hardware. It is
also the *minimum-change* configuration: the module plus the static hex
patches on the BIOS image are designed to handle every gate the firmware
puts up, without asking the user to toggle anything in BIOS setup.

The short version: **after flashing, load Optimized Defaults and boot.**
Do not toggle menu options that look related to BAR / 64-bit / CSM /
PCI reallocation unless this document says to. Several of them are at
best cosmetic and at worst actively destructive.

## Target board revision note

The validated machine is physically a **Gigabyte GA-990FX-Gaming
Rev 1.0** board running the **Rev 1.1 BIOS** (the 1.1 image is used as
the base for all modifications — the underlying silicon is common to
both revisions, and the 1.1 firmware is the cleaner patch base).

If you are on Rev 1.0 with Rev 1.0 stock BIOS, upgrade to the official
Rev 1.1 BIOS first, verify the machine POSTs normally, *then* apply
the modifications described in `FLASHING.md`.

## After first flash

1. Enter BIOS setup (DEL during POST).
2. **Load Optimized Defaults** .
3. **Save & Exit** .
4. Boot to OS.

That's the whole setup. The module does everything else.

## The options you should not touch

### CSM (Compatibility Support Module)

**Leave at default (on). Do not toggle.**

On this board the CSM menu option is **effectively cosmetic**. The
combination of:

- The static hex patches applied to PciBus, PciRootBridge, AmiBoardInfo
  and AGESA (see `bios-patches/990fx.patches`), and
- The `990fxOrchestrator` DXE module itself

handles resource allocation correctly **regardless of the CSM menu
state**. The patches make the "CSM on" path behave like the "CSM off"
path for the specific failure modes that matter (oversized 64-bit BARs,
DSDT MMIO64 gate, bridge pref64 windows).

Toggling CSM off in the menu looks related and feels like it should
help. In practice on this board it changes nothing for resource
allocation and occasionally breaks unrelated things (legacy option ROM
behaviour for add-in cards). If your setup worked with CSM on, leave
it on. If it worked with CSM off, leave it off.
Im my setup i left it on, but from linux is said clearly that is deactivated. so my primary advice is to not touch it. 

### HPCM (Host PCI Cycle Mode, or similar-named option)

**Do not enable.**

There is a menu option (the exact name varies across BIOS revisions,
sometimes "HPCM", sometimes "High Performance PCI Config Mode", and
sometimes buried under a PCI sub-menu) that advertises itself as
improving PCI performance. On this board, enabling it **causes POST
errors or instability** on boots where the module is active.

The safe setting is the default (disabled / automatic — whatever the
stock value is). Do not change it.

### Above 4G Decoding (not present, even at code level in my MB)

This menu option may or may not be present depending on how AMIBCP was
used on the image. It is **not required** — the module provides
equivalent functionality independently. Whatever value it has, leave
it.

If the option is *not* present, that is expected and fine; the module
does not depend on the menu toggle.

### IOMMU / AMD-Vi

Default. This has nothing to do with the module. If you run VMs with
PCI passthrough, enable it for that reason, not for this module.
But i completely disable the iommu, so if you want to enable it, consider that you have to work on that. 

### Secure Boot

Off (default for this board BIOS). Not directly related to module
function, but flashing a modified BIOS with Secure Boot active adds
certificate management complexity that is not addressed by this
project.

### Legacy USB Support

Default. Unrelated.

## OS-level: kernel command line

The validated `linux` cmdline is deliberately boring:

```
ro quiet splash nowatchdog mitigations=off pcie_aspm=off pcie_port_pm=off
radeon.cik_support=0 amdgpu.cik_support=1
```

Specifically **absent** from the cmdline, and the reasons:

### `pci=realloc` — do not use

The kernel's `pci=realloc` option asks Linux to re-attempt BAR
allocation if the BIOS-provided layout is incomplete. It works, kind
of, on an unmodified BIOS. With this module active it has two bad
effects:

1. **It can re-do what the module already did correctly**, sometimes
   landing BARs at *different* addresses than the ones this module
   carefully chose to avoid collisions. The result is silent
   misplacement — `lspci` will show BARs, they'll be above 4 GB, but
   they won't be at `0x1800000000` / `0x1C00000000` / etc. as the
   module intended.
2. **It can fail on the Tesla P100 BAR3 slots** which the BIOS
   allocates correctly and the module does not touch. A realloc pass
   sometimes thinks those 16 GB-aligned slots are "wasted" and tries
   to pack things differently, corrupting the working allocation.

The module's entire *raison d'être* is to make `pci=realloc`
unnecessary. If you need it, something is wrong — the module did not
run, or the static BIOS patches were not applied, or your hardware
differs from the validated stack in a way this project has not
accounted for. File an issue with your NVRAM log.

### `pci=nocrs` — do not use

Same logic. This option tells the kernel to ignore the BIOS-provided
PCI resource map entirely and reassign from scratch. With the module
active, the BIOS map is *already* correct (the module corrected it).
`pci=nocrs` throws away good information.

### `pci=nommconf` — do not use

This disables MMCONFIG (extended PCI config space via memory-mapped
access). The module writes the Resizable BAR capability through
MMCONFIG. Disabling it means the module's writes succeed during boot
but the OS cannot read the capability back to confirm. Visible
misbehaviour follows.

### `amdgpu.dpm=0` / `amdgpu.runpm=0` — do not use

Specific to the W9170: disabling DPM or runtime PM turns off the
memory clock management. The card works, but at reduced power
efficiency and with a known regression in the UVD block (video
decode). There is no benefit to turning these off on Hawaii.

### What you *can* usefully add

- `nowatchdog` — stops the kernel watchdog thread, marginally reduces
  jitter.
- `mitigations=off` — disables CPU-vulnerability mitigations. On an
  AMD FX from 2012 the mitigations cost more than they protect you
  from on a single-user compute box. Leave on if the machine is
  multi-user or network-facing.
- `pcie_aspm=off` — disables Active State Power Management on the
  PCIe link. Needed because some older GPUs (W9170 included) have
  flaky ASPM behaviour; turning it off is a stability win.
- `pcie_port_pm=off` — as above for the root ports.
- `radeon.cik_support=0 amdgpu.cik_support=1` — ensures the W9170 is
  bound by `amdgpu` rather than the legacy `radeon` driver. Required
  for Vulkan.

## Summary: setup decisions at a glance

| Question                                   | Answer              |
|--------------------------------------------|---------------------|
| Should I toggle CSM off?                   | No, leave default   |
| Should I enable HPCM?                      | No, leave disabled  |
| Should I enable "Above 4G" if I see it?    | Optional, doesn't matter |
| Should I enable IOMMU?                     | Only for VMs        |
| Should I add `pci=realloc` to cmdline?     | **No**              |
| Should I add `pci=nocrs`?                  | **No**              |
| Should I add `pci=nommconf`?               | **No**              |
| Does the module replace all of the above?  | Yes, that's the whole point |

If the module is working correctly, you should be able to run the
validated kernel cmdline listed above (boring, unmodified) and have
all four GPUs with full 16 GB BARs visible via `lspci`.

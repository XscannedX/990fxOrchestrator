# Acknowledgments

This project stands on a lot of other people's work. In rough order of
how essential each was.

## The starting point

**[xCuri0](https://github.com/xCuri0) — [ReBarUEFI](https://github.com/xCuri0/ReBarUEFI)**

MIT-licensed. The original DXE driver approach that proved a runtime
module could do resource re-placement at boot time on unsupported
firmware. Without this project I would never have looked at the problem
as "write a driver" rather than "give up and buy new hardware". The FFS
`FILE_GUID` in this module is the one xCuri0 used, deliberately kept as
a tribute. See [`README.md`](README.md#dedication--why-this-exists).

## Development tools

| Tool                | License       | Used for                                     |
|---------------------|---------------|----------------------------------------------|
| **EDK2 / TianoCore**| BSD-2-Clause  | UEFI build system, base libraries, protocols |
| **Ghidra** (NSA)    | Apache 2.0    | Reverse engineering AMI PciBus, PciRootBridge, AGESA, AmiBoardInfo |
| **UEFITool**        | BSD-2-Clause  | BIOS image exploration, module identification |
| **UEFIPatch**       | BSD-2-Clause  | Static hex patch application (research only — not used for final flashing) |
| **MMTool 4.50.0.23**| Proprietary (AMI) | The only trusted tool for replacing FFS modules inside AMI Aptio IV images without corrupting pad files |
| **AMIBCP 4.53 / 5.02** | Proprietary (AMI) | BIOS menu option visibility editing |
| **iASL** (Intel)    | Intel OSS     | DSDT decompile / compile for reverse engineering the `MALH` patch |
| **modGRUBShell** / `setup_var` | Various OSS | UEFI NVRAM variable manipulation from a pre-OS shell |
| **Visual Studio 2022 / MSVC** | Proprietary (Microsoft) | C compiler + linker, EDK2 VS2022 toolchain |

## Hardware programming / recovery

| Tool                | License       | Used for                                   |
|---------------------|---------------|--------------------------------------------|
| **flashrom**        | GPL-2.0       | SPI flash read / write / verify on Linux   |
| **AsProgrammer**    | Freeware      | Same, alternative Windows path             |
| **NeoProgrammer**   | Freeware      | Same, alternative Windows path             |
| **CH341A** firmware | Unlicensed / clone vendor | USB-SPI programmer hardware    |

The CH341A + SOIC8 clip combination is documented extensively by the
hobbyist electronics community — countless forum threads, YouTube
walkthroughs, and flashrom contributors made that recovery path
trivial to learn.

## Documentation and specifications

| Source                                      | Role                              |
|---------------------------------------------|-----------------------------------|
| **AMD Family 15h BKDG** (BIOS and Kernel Developer's Guide) | Northbridge D18F1 Window registers, HT config register 0x94, MMIO routing |
| **ACPI Specification** (UEFI Forum)         | DSDT structure, Name object opcodes, resource descriptors |
| **PCI Express Base Specification** (PCI-SIG)| Resizable BAR capability definition, bridge prefetchable window semantics, Link Disable |
| **UEFI Specification** (UEFI Forum)         | DXE driver model, event groups, runtime service patterns |
| **Intel 64 and IA-32 Architectures SDM**    | POST port conventions, PIT programming (for the diagnostic beep melody branch) |
| **Hawaii register reference** (AMD, public) | W9170 BAR layout, REBAR_CAP format |

## Community

**The [WinRAID forum](https://winraid.level1techs.com/)** — the single
most valuable resource for this project. Specific threads that solved
specific problems:

- Pad File corruption behaviour of UEFIPatch vs MMTool, and why
  MMTool 4.50.0.23 specifically is the one to use.
- AMI Aptio IV module GUID conventions across chipset vendor packages.
- Reverse engineering techniques for AMIBCP option visibility.
- The general social contract that "yes, you can modify your own BIOS,
  and here is the decade of accumulated know-how".

Most of what anyone contributes back to a project like this is a thin
new layer of specifics on top of the forum's collective wisdom.

**The Tesla P100 hobbyist community** — for the pin-3/4/5 tape mod
discovery, the cooling solutions, and the generally thankless work of
keeping 2016-era server GPUs usable in 2026 home setups.

**llama.cpp and related LLM inference projects** — for giving this
project a reason to exist beyond "because the card should work". The
validated runtime workload that exercises all four GPUs at once is a
stock `llama.cpp` Vulkan build.

## People

- **xCuri0** — personally, via the upstream repo. Already credited
  above but worth repeating.
- Contributors to the open-source tools listed in this file, past and
  future.
- Everyone on WinRAID who answered a question, posted a dump, or left
  a detailed forum reply that's still on the internet years later.

If you contributed something this project depended on and you're not
listed here, that's my oversight, not intent. File an issue or a PR
and I'll add you.

---

## Software not used, and why worth noting

For completeness, a few tools in this space that are *not* in the
workflow:

- **UEFITool's "Replace with pad"** — corrupts adjacent modules on
  AMI Aptio IV. Use MMTool instead.
- **UEFIPatch for flashing** — same; it's a useful research tool for
  seeing where hex patches land, but its output images should not go
  onto the chip.
- **Gigabyte's Windows-based `@BIOS` utility** — has been observed to
  refuse modified images; DOS-based Efiflash is the reliable path.

## License notes

This project is MIT-licensed. The software and documentation listed
above are licensed under their own terms; this list is an
acknowledgment, not a redistribution. Where a tool is proprietary
(MMTool, AMIBCP, Visual Studio), only the project's *output* is
distributed here, and users need to obtain the tools themselves from
their respective vendors.

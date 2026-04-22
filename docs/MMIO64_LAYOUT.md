# MMIO64 layout

The module places every managed BAR at a fixed, compile-time address above
4 GB. The full map used on the validated hardware is:

```
 64-bit physical address                              size    owner   note
──────────────────────────────────────────────────────────────────────────
 0x0_00000000 – 0x0_9FFFFFFF                          ~2.5 GB BIOS     below-4G RAM + MMIO32
 0x0_A0000000 – 0x0_FFFFFFFF                          ~1.5 GB BIOS     PCI MMIO32 hole
                                                                      (Arc BAR0, legacy ranges, APIC)
 0x1_00000000 – 0x7_FFFFFFFF                           28 GB  RAM      system memory
 0x8_00000000 – 0xB_FFFFFFFF                           16 GB  module   Tesla P100 #1 BAR1 (CPU-mapped VRAM)
 0xC_00000000 – 0xC_01FFFFFF                           32 MB  BIOS     Tesla P100 #1 BAR3 (doorbell/ctrl)
 0x10_00000000 – 0x13_FFFFFFFF                         16 GB  module   Tesla P100 #2 BAR1
 0x14_00000000 – 0x14_01FFFFFF                         32 MB  BIOS     Tesla P100 #2 BAR3
 0x18_00000000 – 0x1B_FFFFFFFF                         16 GB  module   Arc A770 BAR2 (GDDR6 LMEM)
 0x1C_00000000 – 0x1F_FFFFFFFF                         16 GB  module   W9170 BAR0 (HBM/GDDR aperture)
 0x20_00000000 – 0x20_007FFFFF                          8 MB  module   W9170 BAR2 (doorbell/ctrl)
 0x20_00800000 – 0x3F_FFFFFFFF                         ~126 GB free     reserved / unused
──────────────────────────────────────────────────────────────────────────
```

Expressed in `990fxOrchestrator.c` as compile-time constants:

```c
#define NB_WINDOW_BASE           0xA0000000ULL        // CPU NB D18F1 Window 1 start
#define NB_WINDOW_LIMIT          0x3FFFFFFFFFULL      // → 256 GB MMIO64 routable

#define ARC_BAR2_TARGET          0x1800000000ULL      // Arc A770 BAR2  16 GB
#define W9170_BAR0_TARGET        0x1C00000000ULL      // FirePro W9170 BAR0 16 GB
#define W9170_BAR2_TARGET        0x2000000000ULL      // FirePro W9170 BAR2  8 MB
#define W9170_BRIDGE_BASE        0x1C00000000ULL      // bridge window covering W9170 BARs
#define W9170_BRIDGE_LIMIT       0x2000FFFFFFULL
```

## Why these addresses

**Start at `0x8_00000000` (32 GB), not at 4 GB.** Everything between
`0x1_00000000` and `0x7_FFFFFFFF` is system RAM on a machine with more than
4 GB installed. Placing a BAR there is an instant hang.

**Leave BAR3 of each Tesla P100 where the BIOS put it** (`0xC_00000000` and
`0x14_00000000`, 32 MB each). Those BARs are tiny and the BIOS places them
at a fixed offset inside an otherwise-unused 16 GB slot, immediately after
BAR1. Moving them would just create a hole we can't reuse anyway. The
module does not touch them.

**Do not place the Arc A770 at `0xC_00000000`.** This was bug "BUG-A" in
version v9.2: the 16 GB slot at `0xC` looks free *on paper* but it's
reserved for P100 #1's BAR3 block. Collisions there manifest as post-OS
amdgpu init failures on adjacent cards because the bridge window overlaps.

**Do not place the W9170 at `0x14_00000000`.** Symmetric to above — bug
"BUG-B" in v9.2, same 16 GB collision with P100 #2's BAR3.

**The Arc A770 target is BAR2, not BAR0.** On the A770, BAR0 is a 16 MB
register aperture (MMIO control space). The framebuffer / LMEM is BAR2.
Targeting BAR0 resizes the wrong thing and leaves the GPU with a BAR too
small to be useful. This was bug "BUG-C" in v9.2.

## Bridge windows

Every PCIe bridge whose subordinate range contains a resized endpoint gets
a prefetchable window programmed to **contain the endpoint BAR exactly**:

| Bridge             | Hosts              | Prefetchable window         |
|--------------------|--------------------|-----------------------------|
| 00:04.0 (SR5690)   | FirePro W9170      | 0x1C00000000 – 0x2000FFFFFF |
| 00:15.3 (SB900)    | Arc A770 via riser | 0x1800000000 – 0x1BFFFFFFFF |
| 23:00.0 (Intel UP) | Arc A770 (switch)  | 0x1800000000 – 0x1BFFFFFFFF |
| 24:01.0 (Intel DN) | Arc A770 (switch)  | 0x1800000000 – 0x1BFFFFFFFF |

The Tesla P100 bridges are handled by the BIOS directly — the BIOS places
those BARs correctly without help, because the 16 GB BAR1 is already
declared in the Tesla's ROM as a separate request the BIOS can satisfy in
the lower MMIO64 region it already uses for memory.

On every target bridge, the four registers written are:

```
+0x24    Prefetchable Memory Base     (lower 32 bits, bit0 = 1 → 64-bit decode)
+0x26    Prefetchable Memory Limit    (lower 32 bits, bit0 = 1)
+0x28    Prefetchable Memory Base Upper32
+0x2C    Prefetchable Memory Limit Upper32
```

The `0x0001` low-bit marker on base and limit is the PCI-SIG convention
for "this bridge's prefetchable window uses a 64-bit decoder". Without it
most OSes (Linux included) refuse to assign 64-bit BARs through the bridge
even if the upper32 registers are populated correctly.

## Porting checklist

If adapting to different hardware, the constants to change are localized:

1. **`NB_WINDOW_BASE` / `NB_WINDOW_LIMIT`** — start and size of the MMIO64
   routable window. Needs to be supported by your Northbridge / PCH /
   IIO.
2. **`ARC_BAR2_TARGET`, `W9170_BAR0_TARGET`, `W9170_BAR2_TARGET`,
   `W9170_BRIDGE_BASE`, `W9170_BRIDGE_LIMIT`** — per-card target
   addresses. Must:
   - Lie within the NB window.
   - Be aligned to the BAR size (`0x1800000000` is 16 GB aligned).
   - Not collide with BAR1/BAR3 of devices you do *not* resize (check
     `lspci -vvv` pre-flash and pick addresses that sit in the gaps).
3. **Discovery VID:DID literals** (`0x1002 / 0x67A0`, `0x8086 / cls 0x03`)
   — add or swap for your target cards.

No other changes to `990fxOrchestrator.c` are required for a pure placement port.

#ifndef PCI_REGS_H
#define PCI_REGS_H

// Basic PCI Config Space sizes
#define PCI_CFG_SPACE_SIZE      256
#define PCI_CFG_SPACE_EXP_SIZE  4096

// Extended Capability Macros
#define PCI_EXT_CAP_ID(header)      ((header) & 0x0000ffff)
#define PCI_EXT_CAP_VER(header)     (((header) >> 16) & 0xf)
#define PCI_EXT_CAP_NEXT(header)    (((header) >> 20) & 0xffc)

// Extended Capability ID for Resizable BAR
#define PCI_EXT_CAP_ID_REBAR    0x0015

// Resizable BAR Capability Registers (Offsets from Cap Base)
#define PCI_REBAR_CAP           4       /* capability register */
#define PCI_REBAR_CAP_SIZES     0x00FFFFF0  /* supported BAR sizes */

#define PCI_REBAR_CTRL          8       /* control register */
#define PCI_REBAR_CTRL_BAR_IDX  0x00000007  /* BAR index */
#define PCI_REBAR_CTRL_NBAR_MASK 0x000000E0  /* # of resizable BARs */
#define PCI_REBAR_CTRL_NBAR_SHIFT 5     /* shift for # of BARs */
#define PCI_REBAR_CTRL_BAR_SIZE 0x00001F00  /* BAR size */
#define PCI_REBAR_CTRL_BAR_SHIFT 8      /* shift for BAR size */

#endif // PCI_REGS_H

# Debug codes

The module emits three kinds of diagnostics during boot:

1. **POST port 0x80** byte codes, visible on any motherboard debug LCD or
   via a POST code card. Useful when the machine fails to boot and NVRAM
   is not reachable.
2. **NVRAM log tags** in the `ReBarBootLog` UEFI variable, with two-
   letter stage prefixes `[DA] … [DF]`. Written automatically at every
   boot — see "Automatic per-boot NVRAM log dump" below.
3. **PC speaker beep melody** played just before handoff to the OS.
   Audible confirmation that the module reached `POST_FULL_COMPLETE`
   (`0xFC`) — useful when no POST card or monitor is attached.

## POST port 0x80 map

```
#define POST_PORT               0x80
```

| Code | Constant                | Stage                                         |
|------|-------------------------|-----------------------------------------------|
| 0xD0 | `POST_MODULE_LOADED`    | `rebarInit` entered                           |
| 0xD1 | `POST_DSDT_PATCH`       | DSDT MALH patch in progress                   |
| 0xD2 | `POST_NB_MMIO_WINDOW`   | CPU NB D18F1 Window 1 programmed              |
| 0xD3 | `POST_PRESCAN_START`    | Pre-scan of PCI bus starting                  |
| 0xD5 | `POST_LINK_DISABLE_DONE`| All oversized devices hidden via Link Disable |
| 0xD6 | `POST_HOOK_REGISTERED`  | PreprocessController backup hook armed        |
| 0xD8 | `POST_EXITBS_ENTER`     | ExitBootServices callback fired               |
| 0xD9 | `POST_EXITBS_NB_WINDOW` | NB window forced again at ExitBS              |
| 0xDA | `POST_EXITBS_HT_GATE`   | SR5690 HT gate opened                         |
| 0xDB | `POST_EXITBS_LINK_EN`   | PCIe links re-enabled on hidden devices       |
| 0xDC | `POST_EXITBS_BAR_PROG`  | BAR placement pass complete                   |
| 0xDD | `POST_EXITBS_DONE`      | Handoff to OS complete                        |
| 0xDE | `POST_EXITBS_AMD_REBAR` | `ResizeAmdGpuBars` entered                    |
| 0xDF | `POST_AMD_REBAR_DONE`   | `ResizeAmdGpuBars` returned                   |
| 0xEE | `POST_ERROR_GENERIC`    | Generic error inside the module               |
| 0xEF | `POST_ERROR_LINK_TRAIN` | Link training timeout after re-enable         |
| 0xFC | `POST_FULL_COMPLETE`    | All stages OK, module fully active            |

**Reading the sequence.** A healthy boot produces, in order:

```
0xD0 → 0xD1 → 0xD2 → 0xD3 → 0xD5 → 0xD6
→ (BIOS POST continues, module is idle)
→ 0xD8 → 0xD9 → 0xDA → 0xDB → 0xDC → 0xDE → 0xDF → 0xDD → 0xFC
```

If the machine hangs at `0xD1`, the DSDT layout has shifted (BIOS update on
the vendor side?) and the `MALH` offset needs to be re-discovered.

If the machine hangs at `0xD2`, the NB MMIO window write is being rejected
by hardware — usually a sign that HT gate is still closed or the board
isn't actually an SR5690.

If the machine reaches `0xD5` but never gets to `0xD8`, the BIOS never
called ExitBootServices — either the BIOS is looping in POST (hidden device
surprise) or the kernel is stuck earlier (CSM still on, no OS loader found).

## Automatic per-boot NVRAM log dump

**Every boot, the module writes a complete trace of its decisions to a
UEFI NVRAM variable.** There is no switch to enable this — it happens
unconditionally, as long as the module runs to ExitBootServices. This is
by far the most useful diagnostic when something goes wrong, because:

- It is committed to persistent storage, so it survives a reboot, a
  kernel panic, or even a subsequent BIOS re-flash.
- It is readable offline from any OS that can see UEFI variables.
- It contains the actual values read and written (register addresses,
  BAR sizes, bridge programming) — not just "I ran this function".

### How it works internally

The module maintains a 16 KB RAM buffer. During every stage, `L_STR()`
helpers append trace lines with their `[Dx]` tag (see table below). At
the very end of `OnExitBootServices`, a single `gRT->SetVariable()` call
commits the whole buffer.

- **Buffer size:** 16 KB. If the log overflows, later lines are dropped,
  but the earliest-and-most-useful stages are preserved.
- **Flushed when:** last thing inside the ExitBootServices callback,
  before the return. If the machine hangs before ExitBS, nothing is
  written — use the POST port 0x80 codes instead.
- **Re-flashed every boot:** the variable is overwritten, not appended.
  Only the most recent boot is persisted.

### Where the log lives

| OS        | Path                                                                              |
|-----------|-----------------------------------------------------------------------------------|
| Linux     | `/sys/firmware/efi/efivars/ReBarBootLog-b00710c0-a992-4a0f-8b54-0291517c21aa`     |
| Windows   | Any UEFI-variable reader — GUID `b00710c0-a992-4a0f-8b54-0291517c21aa`, name `ReBarBootLog` |
| UEFI shell | `dmpstore -guid b00710c0-a992-4a0f-8b54-0291517c21aa ReBarBootLog`              |

The file on Linux has a 4-byte EFI attribute prefix before the payload —
piping through `strings` (as shown below) is the simplest way to read
the human-readable content.

### Saving the log for a bug report

```bash
# Copy the raw variable somewhere writable (efivars is read-only for
# non-root by default and normal copy preserves permissions badly):
sudo dd \
  if=/sys/firmware/efi/efivars/ReBarBootLog-b00710c0-a992-4a0f-8b54-0291517c21aa \
  of=rebar_boot_log_$(date +%Y%m%d_%H%M%S).bin

# Human-readable version:
strings rebar_boot_log_*.bin > rebar_boot_log_readable.txt
```

Attach the `.bin` (raw, preserves all bytes) *and* the `.txt` (for humans
to scan quickly) to any issue you file.

## NVRAM log tags

The `L_STR()` helper appends a line to an in-memory buffer during boot,
and `FlushLogToNVRAM()` commits the buffer to the `ReBarBootLog` UEFI
variable on ExitBootServices completion. Every line begins with a
two-letter stage tag:

| Tag    | Stage                                                           |
|--------|-----------------------------------------------------------------|
| `[DA]` | NB MMIO window + HT gate (SR5690 0x94)                          |
| `[DB]` | Bridge quirks (`Apply990FxBridgeQuirkAll`, hidden device wake)  |
| `[DC]` | Intel GPU ReBAR pass (`ResizeIntelGpuBars`)                     |
| `[DD]` | ExitBootServices sequencing (final "log flush" marker)          |
| `[DE]` | AMD GPU ReBAR pass (`ResizeAmdGpuBars`)                         |
| `[DF]` | AMD GPU ReBAR success summary line                              |

### Reading the NVRAM log on Linux

```bash
sudo cat \
  /sys/firmware/efi/efivars/ReBarBootLog-b00710c0-a992-4a0f-8b54-0291517c21aa \
  | strings
```

### Reading on Windows

Use `mountvol` to mount the EFI system partition, then any tool that can
read UEFI variables (e.g. `RW — Read & Write Utility`), looking for GUID
`b00710c0-a992-4a0f-8b54-0291517c21aa` and name `ReBarBootLog`.

## What a healthy v9.5 log looks like

```
=== 990fxOrchestrator v9.5 LOG ===
[DA] SR5690 0x94 BEFORE: 00000000
[DA] SR5690 0x94 AFTER:  00000003
[DB] Apply990FxBridgeQuirkAll
[DB] Quirk bridge 00:04.0 DID=5a16 pref64=OK
[DB] Quirk bridge 00:0b.0 DID=5a16 pref64=OK
[DB] v9 00:15.3 43a3 detected — window delegata a ResizeAmdGpuBars
[DB] Quirk bridge 00:15.3 DID=43a3 pref64=OK
[DB] ReEnableAllHiddenDevices
[DC] ResizeIntelGpuBars v9.5 enter
[DC] ResizeIntelGpuBars OK v9.5 — Arc BAR2 16GB @ 0x0000001800000000 …
[DC] ResizeIntelGpuBars exit, nextPref=… nextMmio=…
[DE] ResizeAmdGpuBars v9.4 enter
[DE] W9170 @ 14:00.0 sotto bridge 00:04.0 (bridgeVidDid=0x5a181002)
[DE] W9170 ReBAR @ 0x0200
[DE] REBAR_CAP=0x0007F000
[DE] max size idx=0E
[DE] REBAR_CTRL (pre)=0x00000820
[DE] W9170 BAR0 target=0x0000001C00000000
[DE] REBAR_CTRL (post)=0x00000E20
[DE] W9170 BAR0 low=0x0000000C …
[DE] W9170 BAR2 low=0x00000004 …
[DF] ResizeAmdGpuBars OK v9.4 — BAR0 16GB @ 0x0000001C00000000 …
[DD] ExitBS sequence complete, flushing log to NVRAM
```

The two common failure modes look like:

- `[DE] W9170 (1002:67A0) NOT FOUND su alcun root port — skip` → the card
  is either absent, behind a bridge that failed link training, or
  physically in a slot the discovery loop doesn't cover (should not
  happen in v9.4+, which is bus-agnostic).
- `[DC] GPU Intel (8086 cls 03) NOT FOUND su alcun root port/func` →
  same class of problem for the Arc A770.

(Note: some log lines are still in Italian in the published source. They
are being translated incrementally. The tag letters and semantics are the
stable interface and will not change.)

## PC speaker beep melody

Just before the module returns from `OnExitBootServices` and raises POST
`0xFC` (full complete), it plays a short melody on the PC speaker. This
is a **deliberate audible "everything is OK" signal** — useful when:

- You boot headless and want to know the module ran without walking to
  the machine with a screen.
- Your debug POST card is not installed.
- You want a clear audible *difference* between "module ran, handed off
  to OS" vs "BIOS POSTed but the module never fired" (in which case the
  only sound is the usual single BIOS POST beep, if any).

### The pattern

The pattern is **Morse `V`** played backwards — "two short, two long,
two short" — at 880 Hz on the PC speaker:

| Beat | Length | Approx. duration | Meaning   |
|------|--------|------------------|-----------|
| 1    | short  | 120 ms           | `·`       |
| 2    | short  | 120 ms           | `·`       |
|      | pause  | 180 ms           |           |
| 3    | long   | 400 ms           | `—`       |
| 4    | long   | 400 ms           | `—`       |
|      | pause  | 180 ms           |           |
| 5    | short  | 120 ms           | `·`       |
| 6    | short  | 120 ms           | `·`       |

Total time: ~1.6 seconds. It is recognisable and short enough not to
become annoying on every reboot.

### Hardware used

- **PIT 8253/8254 channel 2** — square wave generator wired to the PC
  speaker on every PC-compatible since the original IBM PC.
  - Port `0x43`: Mode/Command register.
  - Port `0x42`: Channel 2 data (divisor for 1.193182 MHz).
- **System Control port `0x61`** — bit 0 gates timer 2, bit 1 routes
  timer 2's output to the speaker. The module saves and restores the
  other bits (NMI, parity error, IO channel check) so it does not
  corrupt unrelated system state.

### Silencing the melody

If you do not want the beeps — e.g. a server in a rack where extra
sounds are confusing — edit `990fxOrchestrator.c` and flip the compile-time gate:

```c
#define ENABLE_BEEP_MELODY 0
```

at the top of the file, then rebuild. The melody compiles out entirely
(`V9PlayFcMelody()` expands to `do { } while (0)`) — no PIT or port
`0x61` writes, no speaker state changes.

### What silence means

- **No beep at all** after POST → either:
  - The module was not loaded (check FFS insertion, POST card, NVRAM
    log absent?), or
  - The module crashed before reaching ExitBootServices (NVRAM log will
    show the last stage that ran), or
  - `ENABLE_BEEP_MELODY` was set to `0` at build time.
- **Truncated or garbled beep** → the module started the melody but
  something else (SMI storm? BIOS re-entering POST?) interrupted it.
  The NVRAM log will still be flushed *after* the melody, so it will
  tell you whether the module completed cleanly.

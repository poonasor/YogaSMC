# ThinkCentre / IdeaCentre support (this fork)

This fork adds fan reading and control for **Lenovo ThinkCentre/IdeaCentre
desktops** whose fan is driven by a Nuvoton **NCT6683D/NCT6686D/NCT6687D**
Super-I/O EC (verified on a ThinkCentre **M710q Tiny**, i5-7500T, macOS 13).

Upstream YogaSMC only supports ThinkPad/IdeaPad laptops, which expose a
`PNP0C09` ACPI EC the driver attaches to. ThinkCentre desktops have no VPC
ACPI interface; the fan is controlled by the Nuvoton EC. The new
`ThinkCentre` service attaches to the ACPI EC device (`EC`/`EC0`/`H_EC`/
`PNP0C09`/`ACID0001` — on the M710q the EC device is named `EC` with HID
`ACID0001`) and drives the chip through its LPC I/O ports directly.

## What you get

| Feature | Interface |
|---|---|
| Fan RPM | SMC keys `FNum`, `F0Ac` (fpe2), `F0Mn/F0Mx/F0Sf`, `F0ID` |
| CPU/DIMM temperatures | SMC keys `TCXC` (PECI/PCH), `TM0p…` (DIMM), `TG0P…` (generic) |
| Fan control (duty 0–255) | App slider, `ioreg` property `FanPWM`, user-client EC write 0x2E |
| Think-style levels 0–7 / Auto | `FanSpeed` property, user-client EC write 0x2F (compat with ThinkFanHelper-style clients) |
| Auto mode restore | `FanAuto` property, EC write 0x2F = 0x84 |

Fan control uses the EC configuration handshake from the Linux `nct6683`
driver: write `FAN_CFG_REQ` (0x80) to `0xA01`, wait ~1.5 ms, write the target
duty to `0xA28+n`, then write `FAN_CFG_DONE` (0x40).

Note that `0xA01`/`0xA28` are **EC register addresses** (bank `0x0A`, index
`0x01`/`0x28`) reached through the indexed window at `ecBase+4/5/6`, not LPC
I/O ports. Their resemblance to the `0x0A00-0x0A2F` range the `SIO1` ACPI
device reserves on the M710q is a coincidence.

## Status

Verified working on a ThinkCentre M710q Tiny (i5-7500T, macOS 13.7.8,
OpenCore, SIP enabled):

```
ThinkCentre  registered, matched, active
  ChipName = "Nuvoton NCT6683D"   FanCount = 1   PwmAvailable = Yes
  Key Submitted = 10              HWM base 0xa20 at SIO 0x2e

FNum  1        F0Ac  1061 rpm      F0ID  "CPU Fan"
TG0P  52.00 C  TG1P  25.00 C
```

## Installation (OpenCore)

0. **Fetch the build dependencies first.** `MacKernelSDK`, `Lilu.kext` and
   `VirtualSMC.kext` live at the repo root but are gitignored, so a fresh
   clone does not have them and `build_kext.sh` will fail on the missing
   include paths. Get them with:

   ```sh
   git clone --depth=1 https://github.com/acidanthera/MacKernelSDK
   # DEBUG builds of both, from the latest releases:
   #   https://github.com/acidanthera/Lilu/releases       -> Lilu.kext
   #   https://github.com/acidanthera/VirtualSMC/releases -> VirtualSMC.kext
   # unzip and place Lilu.kext and VirtualSMC.kext at the repo root
   ```

   Only their `Contents/Resources` headers are used, so the DEBUG variants of
   the versions you run are the right ones (currently Lilu 1.7.1,
   VirtualSMC 1.3.7).

1. Build: `./build_kext.sh` (kext) and `./build_app.sh` (menu bar app).
   Both scripts work with Command Line Tools only — Xcode is not required.
   With full Xcode you can instead build the `YogaSMC` scheme as usual, using
   the same dependencies from step 0.

   `YogaSMCPane` is the exception: compiling its `Base.lproj/YogaSMCPane.xib`
   needs `ibtool`, which requires full Xcode, and the pane has no
   `ThinkCentre` case — `YogaSMCPane.swift` would show it as "Unsupported"
   with both feature tabs removed. Every ThinkCentre control lives in the
   menu bar app instead.
2. Copy `build/YogaSMC.kext` to `EFI/OC/Kexts/` and add it to
   `Kernel → Add` in `config.plist` (any position after `VirtualSMC.kext`).
3. **Disable `SMCSuperIO.kext`** — it talks to the same chip and registers
   the same fan keys.
4. Copy `build/YogaSMCNC.app` to `/Applications` and reboot.

## Verification after reboot

- `kmutil showloaded | grep -i yoga` — the kext must be loaded.
- `ioreg -r -c ThinkCentre -w0` — the service with `ChipName`, `FanCount`,
  `FanDuty` (refreshes every 2 s) and `Key Submitted`.
- `clang -O2 -framework IOKit -o build/smckeys tools/smckeys.c && ./build/smckeys`
  — fan/temperature SMC keys, decoded by the type each key reports.
- Menu bar app: 0–100 % slider and Auto button; RPM readout while the menu
  is open.
- Debug builds (`-DDEBUG` in `build_kext.sh`) log chip detection and probe
  progress to the kernel log. Getting at that log is genuinely awkward: the
  driver uses `IOLog` (prefix `YSMC`, **not** `YogaSMC`), which lands in the
  kernel ring buffer readable with `sudo dmesg`, not in `log show`. The
  default 128 KB buffer can be flushed within a minute or two of login by
  unrelated spam, and `msgbuf=<bytes>` did not raise it on macOS 13.7.8.
  The OC debug boot log (`opencore-*.txt` on the EFI partition, requires a
  DEBUG OpenCore.efi) only proves injection — IOKit matching happens later.

  Because of that, a failed probe leaves breadcrumbs on the ACPI EC nub,
  which outlives the driver and is readable at any time:

  ```sh
  ioreg -r -n EC -l -w0 | grep YSMC-TC
  #   YSMC-TC-Probe   = "entered" | "detectChip-failed" | "matched"
  #   YSMC-TC-SioId2E / YSMC-TC-SioId4E   device id seen at each SIO port
  #   YSMC-TC-ecBase  HWM base once detection succeeds
  ```

  `ioreg -rc ThinkCentre -w0` additionally publishes `PwmIndex`, `TachIndex`
  and `TempSensorCount` so a wrong channel pick is visible without a log.

## Emulated EC interface (for user-client clients)

| EC offset | Read | Write |
|---|---|---|
| `0x2E` | current duty | target duty 0–255 (manual) |
| `0x2F` | level/auto status | 0–7 level, `0x47` full, `0x8x` auto |
| `0x31` | fan select (0) | no-op (single fan) |
| `0x84` (+`0x85`) | fan RPM lo/hi | — |

## Notes and limitations

- Auto-mode restore issues the REQ/DONE handshake without a duty write;
  whether the EC fully resumes its own curve can vary with EC firmware
  (customer id is logged at probe). A power cycle always restores factory
  behavior.
- Register semantics and the REQ/DONE protocol are derived from the Linux
  `nct6683` driver (GPL-2.0-or-later, Copyright (C) 2013 Guenter Roeck) and
  the SMCSuperIO sensor implementation in acidanthera/VirtualSMC.
- The YogaSMC preference pane does not show ThinkCentre-specific controls.

## Lessons learned porting the build to Command Line Tools

The kext currently loads on the M710q; getting there surfaced several
requirements that Xcode normally hides, kept here for future reference:

1. **`kmod_info` glue.** Xcode generates the exported `kmod_info`
   structure from the `MODULE_NAME`/`MODULE_START` build settings via
   linker-generated kext objects. CLT builds must define it in code — see
   the `MANUAL_KEXT_GLUE` block at the end of `YogaSMC/YogaSMC.cpp`.
   OpenCore's injector refuses a kext without it ("Invalid Parameter").
2. **All sources must be linked.** OpenCore's prelinked linker patches
   kernel C++ vtables by walking every `superClass` symbol it finds —
   including *undefined* ones. One missing object file (`YogaWMI.cpp` in
   our case) leaves the class' metaclass vtable undefined and aborts the
   whole injection with "Vtable patching failed".
3. **Signing is not required** for OpenCore injection; ad-hoc signatures
   are stripped during relinking. Sign the bundle only to match Xcode
   output.
4. **Attach via the ACPI EC nub, not `IOPCIClassMatch`.** On the M710q a
   personality mirroring SMCSuperIO's (`IOPCIDevice` + `IOPCIClassMatch` +
   custom match category + `IOResourceMatch=ACPI`) never instantiated,
   while an `IOACPIPlatformDevice` personality matching the `EC` device
   attaches reliably (the same pattern the WMI personalities use).


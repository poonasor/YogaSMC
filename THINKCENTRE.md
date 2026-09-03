# ThinkCentre / IdeaCentre support (this fork)

This fork adds fan reading and control for **Lenovo ThinkCentre/IdeaCentre
desktops** whose fan is driven by a Nuvoton **NCT6683D/NCT6686D/NCT6687D**
Super-I/O EC (verified on a ThinkCentre **M710q Tiny**, i5-7500T, macOS 13).

Upstream YogaSMC only supports ThinkPad/IdeaPad laptops, which expose a
`PNP0C09` ACPI EC the driver attaches to. ThinkCentre desktops have neither
that EC device nor a `VPC` ACPI interface — the fan is controlled by the
Nuvoton EC behind the LPC bridge. The new `ThinkCentre` service class
attaches to the LPC `IOPCIDevice` instead and drives the chip directly.

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

## Installation (OpenCore)

1. Build: `./build_kext.sh` (kext) and `./build_app.sh` (menu bar app).
   Both scripts work with Command Line Tools only — Xcode is not required.
   For Xcode builds, clone `MacKernelSDK` and place `Lilu.kext` +
   `VirtualSMC.kext` (DEBUG builds) at the repo root, then build the
   `YogaSMC` scheme as usual.
2. Copy `build/YogaSMC.kext` to `EFI/OC/Kexts/`.
3. Add it to `Kernel → Add` in `config.plist` (any position after
   `VirtualSMC.kext`).
4. **Disable `SMCSuperIO.kext`** — it talks to the same chip and registers
   the same SMC keys.
5. Copy `build/YogaSMCNC.app` to `/Applications` and reboot.

## Verification after reboot

- `ioreg -r -c ThinkCentre -w0` shows the service with `ChipName`,
  `FanCount`, `FanDuty` (refreshes every 2 s) and `Key Submitted`.
- `tools/smckeys` prints fan/temperature SMC keys.
- Menu bar app: 0–100 % slider and Auto button; RPM readout while the menu
  is open.

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

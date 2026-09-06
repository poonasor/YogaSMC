//
//  CentreSMCPane.swift
//  YogaSMCPane
//
//  ThinkCentre / IdeaCentre (Nuvoton NCT6683D) tab.
//
//  The pane has no user client, so every value here is read back from the
//  IORegistry properties ThinkCentre.cpp publishes. Fan control stays in the
//  menu bar app: CentreFanHelper reaches the EC emulation through the user
//  client and can show a real tachometer reading, which the registry does not
//  carry. What the pane owns is identification, the driver's own view of the
//  fan, and the two defaults YogaSMCNC consults when it launches.
//

import AppKit
import Foundation

/// A driver-published integer, or an explicit "Unknown" when the key is absent.
/// Absent is never rendered as 0 — on this driver a missing key usually means the
/// running kext predates the property, which is worth telling apart from a real 0.
private func centreNumber(_ props: NSDictionary, _ key: String) -> String {
    guard let value = (props[key] as? NSNumber)?.intValue else { return "Unknown" }
    return String(value)
}

extension YogaSMCPane {
    @IBAction func vCentreDisableFanSet(_ sender: NSButton) {
        defaults.setValue(sender.state == .on, forKey: "DisableFan")
        _ = scriptHelper(reloadAS, "Reload YogaSMCNC")
    }

    @IBAction func vCentreSaveFanLevelSet(_ sender: NSButton) {
        defaults.setValue(sender.state == .on, forKey: "SaveFanLevel")
        _ = scriptHelper(reloadAS, "Reload YogaSMCNC")
    }

    func updateCentre(_ props: NSDictionary) {
        updateCentreFan(props)
        updateCentreHardware(props)

        vCentreDisableFan.state = defaults.bool(forKey: "DisableFan") ? .on : .off
        vCentreSaveFanLevel.state = defaults.bool(forKey: "SaveFanLevel") ? .on : .off

        // The General tab shows "EC Capability" for every class. On ThinkCentre the
        // driver never calls validateEC() and never touches an ACPI EC — it drives
        // the Super-I/O directly — so the "RW" it publishes is a fixed claim.
        vECRead.toolTip = "ThinkCentre reaches the Nuvoton EC through Super-I/O port access. "
            + "This value is a constant in the driver, not the result of a probe."
    }

    private func updateCentreFan(_ props: NSDictionary) {
        // FanManualMode is only published once something has driven the fan. Absent
        // therefore means the EC is still running its own curve, which is automatic.
        let manual = (props["FanManualMode"] as? Bool) ?? false
        vCentreMode.stringValue = manual ? "Manual" : "Automatic"
        vCentreMode.toolTip = manual
            ? "The driver is holding a commanded duty cycle."
            : "The EC is running its own fan curve."

        if let duty = (props["FanDuty"] as? NSNumber)?.intValue, (0...255).contains(duty) {
            // Truncating, to match CentreFanHelper's percentage exactly - the two are
            // shown side by side and must not disagree by a point.
            vCentreDuty.stringValue = String(format: "%d %% (%d/255)", duty * 100 / 255, duty)
            vCentreDuty.toolTip = manual
                ? "In manual mode this is the last duty the driver was told to set, not a measurement."
                : "Sampled by the driver's poller."
        } else {
            vCentreDuty.stringValue = "Unknown"
            vCentreDuty.toolTip = nil
        }

        // Absent and false mean different things: no key at all points at an older
        // kext, false at a board whose EC exposes no PWM output.
        switch props["PwmAvailable"] as? Bool {
        case .some(true):
            if let index = (props["PwmIndex"] as? NSNumber)?.intValue {
                vCentrePwm.stringValue = "Channel \(index)"
            } else {
                vCentrePwm.stringValue = "Available"
            }
        case .some(false):
            vCentrePwm.stringValue = "Unavailable"
        case .none:
            vCentrePwm.stringValue = "Unknown"
        }

        // TachIndex is tachIndex[0], which keeps its initialiser when setupFans()
        // found nothing — start() survives that case, so guard on the fan count
        // rather than printing a channel number no fan is wired to.
        if let fans = (props["FanCount"] as? NSNumber)?.intValue, fans == 0 {
            vCentreTach.stringValue = "None enabled"
        } else if let tach = (props["TachIndex"] as? NSNumber)?.intValue {
            vCentreTach.stringValue = "Channel \(tach)"
        } else {
            vCentreTach.stringValue = "Unknown"
        }
    }

    private func updateCentreHardware(_ props: NSDictionary) {
        vCentreChip.stringValue = props["ChipName"] as? String ?? "Unknown"

        // Left on the ACPI EC nub by probe(), not on the driver itself.
        if let base = getParentNumber("YSMC-TC-ecBase", service) {
            vCentreEcBase.stringValue = String(format: "0x%04x", base)
        } else {
            vCentreEcBase.stringValue = "Unknown"
        }

        vCentreFanCount.stringValue = centreNumber(props, "FanCount")
        vCentreSensorCount.stringValue = centreNumber(props, "TempSensorCount")
        vCentreKeyCount.stringValue = centreNumber(props, "Key Submitted")
    }
}

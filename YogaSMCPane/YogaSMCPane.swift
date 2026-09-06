//
//  YogaSMCPane.swift
//  YogaSMCPane
//
//  Created by Zhen on 9/17/20.
//  Copyright © 2020 Zhen. All rights reserved.
//

import AppKit
import Foundation
import IOKit
import PreferencePanes
import os.log

let DYTCCommand = ["L", "M", "H"]
let thinkLEDCommand = [0, 0x80, 0xA0, 0xC0]
let thinkBatteryName = ["BAT_ANY", "BAT_PRIMARY", "BAT_SECONDARY"]

class YogaSMCPane: NSPreferencePane {
    let service = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("YogaVPC"))
    let defaults = UserDefaults(suiteName: "org.zhen.YogaSMC")!

    var thinkBatteryNumber = -1

    @IBOutlet weak var vVersion: NSTextField!
    @IBOutlet weak var vClass: NSTextField!
    @IBOutlet weak var vECRead: NSTextField!
    @IBOutlet weak var vHideMenubarIcon: NSButton!
    @IBAction func toggleMenubarIcon(_ sender: NSButton) {
        if vHideMenubarIcon.state == .on {
            defaults.setValue(true, forKey: "HideIcon")
            vMenubarIcon.isEnabled = false
        } else {
            defaults.setValue(false, forKey: "HideIcon")
            vMenubarIcon.isEnabled = true
        }
        _ = scriptHelper(reloadAS, "Reload YogaSMCNC")
    }

    @IBOutlet weak var vMenubarIcon: NSTextField!
    @IBAction func setMenubarIcon(_ sender: NSTextField) {
        if vMenubarIcon.stringValue == "" {
            guard defaults.value(forKey: "Title") != nil else { return }
            defaults.removeObject(forKey: "Title")
        } else {
            if defaults.string(forKey: "Title") == vMenubarIcon.stringValue { return }
            defaults.setValue(vMenubarIcon.stringValue, forKey: "Title")
        }
        _ = scriptHelper(reloadAS, "Reload YogaSMCNC")
    }

    @IBOutlet weak var vHideCapsLock: NSButton!
    @IBAction func toggleHideCapsLock(_ sender: NSButton) {
        defaults.setValue(vHideCapsLock.state == .on, forKey: "HideCapsLock")
        _ = scriptHelper(reloadAS, "Reload YogaSMCNC")
    }

    @IBAction func vClearEvents(_ sender: NSButton) {
        _ = scriptHelper(stopAS, "Stop YogaSMCNC")
        defaults.removeObject(forKey: "Events")
        _ = scriptHelper(startAS, "Start YogaSMCNC")
    }

    // Idea
    @IBOutlet weak var vFnKeyRadio: NSButton!
    @IBOutlet weak var vFxKeyRadio: NSButton!

    @IBOutlet weak var vBatteryID: NSTextField!
    @IBOutlet weak var vBatteryTemperature: NSTextField!
    @IBOutlet weak var vCycleCount: NSTextField!
    @IBOutlet weak var vMfgDate: NSTextField!

    @IBOutlet weak var vAlwaysOnUSBMode: NSButton!
    @IBOutlet weak var vConservationMode: NSButton!
    @IBOutlet weak var vRapidChargeMode: NSButton!

    @IBOutlet weak var vCamera: NSTextField!
    @IBOutlet weak var vBluetooth: NSTextField!
    @IBOutlet weak var vWireless: NSTextField!
    @IBOutlet weak var vWWAN: NSTextField!
    @IBOutlet weak var vGraphics: NSTextField!

    // Think

    @IBOutlet weak var vChargeThresholdStart: NSTextField!
    @IBOutlet weak var vChargeThresholdStop: NSTextField!
    @IBOutlet weak var vPrimaryChargeThresholdStart: NSTextField!
    @IBOutlet weak var vPrimaryChargeThresholdStop: NSTextField!
    @IBOutlet weak var vSecondaryChargeThresholdStart: NSTextField!
    @IBOutlet weak var vSecondaryChargeThresholdStop: NSTextField!

    @IBOutlet weak var vPowerLEDSlider: NSSlider!
    @IBOutlet weak var vStandbyLEDSlider: NSSlider!
    @IBOutlet weak var vThinkDotSlider: NSSliderCell!
    @IBOutlet weak var vCustomLEDSlider: NSSlider!
    @IBOutlet weak var vCustomLEDList: NSPopUpButton!

    @IBOutlet weak var vSecondFan: NSButton!
    @IBOutlet weak var vFanStop: NSButton!
    @IBOutlet weak var vDisableFan: NSButton!
    @IBOutlet weak var vSaveFanLevel: NSButton!

    @IBOutlet weak var vMuteLEDFixup: NSButton!

    // Centre

    @IBOutlet weak var vCentreMode: NSTextField!
    @IBOutlet weak var vCentreDuty: NSTextField!
    @IBOutlet weak var vCentrePwm: NSTextField!
    @IBOutlet weak var vCentreTach: NSTextField!

    @IBOutlet weak var vCentreChip: NSTextField!
    @IBOutlet weak var vCentreEcBase: NSTextField!
    @IBOutlet weak var vCentreFanCount: NSTextField!
    @IBOutlet weak var vCentreSensorCount: NSTextField!
    @IBOutlet weak var vCentreKeyCount: NSTextField!

    @IBOutlet weak var vCentreDisableFan: NSButton!
    @IBOutlet weak var vCentreSaveFanLevel: NSButton!

    // Main

    @IBOutlet weak var mainTabView: NSTabView!
    // Deliberately strong: NSTabView holds the only other reference to a tab item,
    // so removeTabViewItem() deallocates it and a weak outlet would be nil on the
    // next willSelect().
    @IBOutlet var ideaViewItem: NSTabViewItem!
    @IBOutlet var thinkViewItem: NSTabViewItem!
    @IBOutlet var centreViewItem: NSTabViewItem!

    @IBOutlet weak var vDYTCRevision: NSTextField!
    @IBOutlet weak var vDYTCFuncMode: NSTextField!
    @IBOutlet weak var DYTCSlider: NSSlider!
    @IBOutlet weak var DYTCPSCCheck: NSButton!

    @IBOutlet weak var backlightSlider: NSSlider!
    @IBOutlet weak var vBackLightTimeout: NSTextField!

    @IBOutlet weak var autoSleepCheck: NSButton!
    @IBOutlet weak var yogaModeCheck: NSButton!
    @IBOutlet weak var indicatorCheck: NSButton!
    @IBOutlet weak var muteCheck: NSButton!
    @IBOutlet weak var micMuteCheck: NSButton!

    @IBOutlet weak var vClamshellMode: NSButton!

    /// Removing a tab item deallocates it, so a second willSelect() would otherwise
    /// remove an item that is no longer there. AppKit tolerates that, but only
    /// after the outlet has been dereferenced.
    func hideTab(_ item: NSTabViewItem?) {
        guard let item = item,
              mainTabView.indexOfTabViewItem(item) != NSNotFound else { return }
        mainTabView.removeTabViewItem(item)
    }

    override func mainViewDidLoad() {
        super.mainViewDidLoad()
        if #available(macOS 10.12, *) {
            os_log(#function, type: .info)
        }
        // The Centre checkboxes mirror defaults the menu bar app reads, so seed them
        // once at load: in a DEBUG build the tab stays visible on Idea/Think machines,
        // where updateCentre() never runs and they would otherwise show a stale off.
        vCentreDisableFan.state = defaults.bool(forKey: "DisableFan") ? .on : .off
        vCentreSaveFanLevel.state = defaults.bool(forKey: "SaveFanLevel") ? .on : .off
    }

    override func willSelect() {
        guard service != 0, sendBoolean("Update", true, service) else { return }

        guard let props = getProperties(service) else {
            if #available(macOS 10.12, *) {
                os_log("Unable to acquire driver properties!", type: .fault)
            }
            return
        }

        if let val = props["VersionInfo"] as? NSString {
            vVersion.stringValue = val as String
        } else {
            if #available(macOS 10.12, *) {
                os_log("Unable to identify driver version!", type: .fault)
            }
            return
        }

        if let val = props["EC Capability"] as? NSString {
            vECRead.stringValue = val as String
        } else {
            if #available(macOS 10.12, *) {
                os_log("Unable to identify EC capability!", type: .fault)
            }
            return
        }

        updateMain(props)

        switch props["IOClass"] as? NSString {
        case "IdeaVPC":
            vClass.stringValue = "Idea"
            updateIdea(props)
            #if !DEBUG
            hideTab(thinkViewItem)
            hideTab(centreViewItem)
            #endif
        case "ThinkVPC":
            vClass.stringValue = "Think"
            updateThink(props)
            #if !DEBUG
            hideTab(ideaViewItem)
            hideTab(centreViewItem)
            #endif
        case "ThinkCentre":
            vClass.stringValue = "Centre"
            updateCentre(props)
            // A desktop shares none of the laptop controls, and leaving the Think
            // tab up would duplicate the fan checkboxes onto the same defaults.
            hideTab(ideaViewItem)
            hideTab(thinkViewItem)
        case "YogaHIDD":
            vClass.stringValue = "HIDD"
            hideTab(ideaViewItem)
            hideTab(thinkViewItem)
            hideTab(centreViewItem)
        default:
            vClass.stringValue = "Unsupported"
            hideTab(ideaViewItem)
            hideTab(thinkViewItem)
            hideTab(centreViewItem)
        }
    }
}

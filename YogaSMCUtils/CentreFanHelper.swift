//
//  CentreFanHelper.swift
//  YogaSMCNC
//
//  Fan control UI for ThinkCentre/IdeaCentre desktops (NCT6683D eSIO).
//  Talks to the ThinkCentre kext service through the shared user client:
//    EC 0x2E: read current duty / write target duty (0-255)
//    EC 0x2F: write 0x84 to return to automatic control
//    EC 0x84: read 2 bytes fan RPM
//
//  Created for the ThinkCentre (M710q) YogaSMC fork.
//

import AppKit
import Foundation
import os.log

class CentreFanHelper {
    let menu: NSMenu
    let connect: io_connect_t
    let defaults = UserDefaults(suiteName: "org.zhen.YogaSMC")!
    var enable = false
    var manual = false

    let fanReading = NSTextField(frame: NSRect(x: 12, y: 32, width: 150, height: 30))
    let dutyReading = NSTextField(frame: NSRect(x: 150, y: 32, width: 90, height: 30))
    let slider = NSSlider(frame: NSRect(x: 15, y: 5, width: 200, height: 30))
    let autoMode = NSButton(frame: NSRect(x: 220, y: 8, width: 50, height: 24))

    var savedDuty: UInt8 = 0

    var fanDutyReg: UInt64 = 0x2E
    var fanLevelReg: UInt64 = 0x2F
    var fanRpmReg: UInt64 = 0x84

    public init(_ menu: NSMenu, _ connect: io_connect_t) {
        self.menu = menu
        self.connect = connect
        enable = menu.items[2].title.hasSuffix("ThinkCentre")
        fanReading.stringValue = "---- rpm"

        slider.maxValue = 100
        slider.minValue = 0
        slider.target = self
        slider.action = #selector(sliderChanged)
        slider.isContinuous = true

        autoMode.title = NSLocalizedString("Auto", comment: "")
        autoMode.bezelStyle = .texturedRounded
        autoMode.setButtonType(.onOff)
        autoMode.target = self
        autoMode.action = #selector(autoChanged)
        autoMode.state = .on

        fanReading.isEditable = false
        fanReading.isSelectable = false
        fanReading.isBezeled = false
        fanReading.drawsBackground = false
        fanReading.font = menu.font

        dutyReading.isEditable = false
        dutyReading.isSelectable = false
        dutyReading.isBezeled = false
        dutyReading.drawsBackground = false
        dutyReading.font = menu.font
        dutyReading.stringValue = "Auto"

        let view = NSView(frame: NSRect(x: 0, y: 0, width: 290, height: slider.frame.height + 40))
        view.addSubview(fanReading)
        view.addSubview(dutyReading)
        view.addSubview(slider)
        view.addSubview(autoMode)

        let item = NSMenuItem()
        item.view = view
        menu.insertItem(item, at: 4)
    }

    @objc func autoChanged(_ sender: NSButton) {
        setAuto()
    }

    @objc func sliderChanged(_ sender: NSSlider) {
        autoMode.state = .off
        savedDuty = UInt8(sender.intValue * 255 / 100)
        setDuty(savedDuty)
    }

    func setDuty(_ duty: UInt8 = 0) {
        var value = duty
        if duty != 0 {
            savedDuty = duty
        } else {
            value = savedDuty
        }

        guard enable else { return }
        var input = [value]
        if kIOReturnSuccess != IOConnectCallMethod(connect, UInt32(kYSMCUCWriteEC),
                                                   &fanDutyReg, 1, &input, 1, nil, nil, nil, nil) {
            if #available(macOS 10.12, *) {
                os_log("Write Fan Duty failed!", type: .fault)
            }
            showOSD("WriteFanFail")
            enable = false
        } else {
            manual = true
            defaults.set(Int(value), forKey: "CentreFanDuty")
        }
    }

    func setAuto() {
        guard enable else { return }
        var input: [UInt8] = [0x84]
        if kIOReturnSuccess != IOConnectCallMethod(connect, UInt32(kYSMCUCWriteEC),
                                                   &fanLevelReg, 1, &input, 1, nil, nil, nil, nil) {
            if #available(macOS 10.12, *) {
                os_log("Return to Auto failed!", type: .fault)
            }
            showOSD("WriteFanFail")
        } else {
            manual = false
            defaults.removeObject(forKey: "CentreFanDuty")
        }
    }

    @objc func update(_ updateState: Bool = false) {
        guard enable, connect != 0 else { return }

        var output: [UInt8] = [0, 0]
        var outputSize = 2
        guard kIOReturnSuccess == IOConnectCallMethod(connect, UInt32(kYSMCUCReadEC),
                                                      &fanRpmReg, 1, nil, 0, nil, nil, &output, &outputSize),
              outputSize == 2 else {
            if #available(macOS 10.12, *) {
                os_log("Failed to read RPM", type: .error)
            }
            enable = false
            return
        }

        fanReading.stringValue = String(format: "%d rpm", Int32(output[0]) | Int32(output[1]) << 8)

        outputSize = 1
        guard kIOReturnSuccess == IOConnectCallMethod(connect, UInt32(kYSMCUCReadEC),
                                                      &fanDutyReg, 1, nil, 0, nil, nil, &output, &outputSize),
              outputSize == 1 else { return }

        let duty = output[0]

        if manual {
            dutyReading.stringValue = String(format: "%d %%", Int32(duty) * 100 / 255)
        } else {
            dutyReading.stringValue = String(format: "Auto %d %%", Int32(duty) * 100 / 255)
        }

        guard updateState else {
            if !manual {
                slider.intValue = Int32(duty) * 100 / 255
            }
            return
        }

        // initial state from the kext
        var level: [UInt8] = [0]
        outputSize = 1
        if kIOReturnSuccess == IOConnectCallMethod(connect, UInt32(kYSMCUCReadEC),
                                                   &fanLevelReg, 1, nil, 0, nil, nil, &level, &outputSize),
           outputSize == 1 {
            manual = (level[0] & 0x80) == 0
            autoMode.state = manual ? .off : .on
        }

        if manual {
            slider.intValue = Int32(duty) * 100 / 255
            savedDuty = duty
        } else if let duty = defaults.object(forKey: "CentreFanDuty") as? Int {
            savedDuty = UInt8(duty)
        }
    }
}

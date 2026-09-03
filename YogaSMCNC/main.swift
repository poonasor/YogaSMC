//
//  main.swift
//  YogaSMCNC
//
//  Standalone entry point so the app builds without Xcode storyboards.
//  NSApplication.shared instantiates VolumeObserver via NSPrincipalClass.
//

import AppKit

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.run()

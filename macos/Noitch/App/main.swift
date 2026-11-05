import AppKit

// No storyboard, no SwiftUI App lifecycle: the process starts straight into the run loop.
let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.run()

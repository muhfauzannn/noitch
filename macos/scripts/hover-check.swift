// Drives a running Noitch with the real cursor and checks what its window does. Unit tests cannot
// see these bugs: they live in how AppKit, Core Animation and the window server interact.
// Run through macos/scripts/measure.sh --hover. No clicks. The cursor is restored at the end.
// Posting mouse events may need Accessibility permission for the terminal.
import CoreGraphics
import Foundation

let maxCpuMsPerTransition = 10.0

let pid = Int32(CommandLine.arguments[1])!
let probePath = CommandLine.arguments[2]

func windows() -> [[String: Any]] {
    CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as? [[String: Any]] ?? []
}

func frame(of window: [String: Any]) -> CGRect {
    CGRect(dictionaryRepresentation: window["kCGWindowBounds"] as! CFDictionary)!
}

func islandWindow() -> [String: Any]? {
    windows().first { ($0["kCGWindowOwnerPID"] as? Int32) == pid }
}

func islandWidth() -> CGFloat {
    islandWindow().map { frame(of: $0).width } ?? 0
}

func moveCursor(to point: CGPoint) {
    CGWarpMouseCursorPosition(point)
    CGEvent(mouseEventSource: nil, mouseType: .mouseMoved, mouseCursorPosition: point, mouseButton: .left)?
        .post(tap: .cghidEventTap)
}

func wait(_ seconds: Double) {
    Thread.sleep(forTimeInterval: seconds)
}

func cpuNanoseconds() -> Double {
    let process = Process()
    process.executableURL = URL(fileURLWithPath: probePath)
    process.arguments = [String(pid)]
    let output = Pipe()
    process.standardOutput = output
    try! process.run()
    process.waitUntilExit()
    let text = String(decoding: output.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
    return Double(text.split(separator: " ")[1])!
}

let originalCursor = CGEvent(source: nil)!.location
let display = CGDisplayBounds(CGMainDisplayID())
let notch = CGPoint(x: display.midX, y: 8)
let away = CGPoint(x: display.midX, y: 420)

moveCursor(to: away)
wait(0.5)
let collapsedWidth = islandWidth()
guard collapsedWidth > 0 else {
    print("FAIL  island window not found")
    moveCursor(to: originalCursor)
    exit(1)
}

// A window drawn above the island at the notch takes its mouse events and invalidates this test.
if let island = islandWindow(), let islandLayer = island["kCGWindowLayer"] as? Int {
    for window in windows() where (window["kCGWindowOwnerPID"] as? Int32) != pid {
        if let layer = window["kCGWindowLayer"] as? Int, layer > islandLayer, frame(of: window).contains(notch) {
            print("note  \(window["kCGWindowOwnerName"] ?? "another app") is above the island; quit it for a valid result")
        }
    }
}

var failures = 0

func expect(_ description: String, expanded: Bool) {
    let width = islandWidth()
    let passed = (width > collapsedWidth) == expanded
    print("\(passed ? "ok  " : "FAIL")  \(description) (window width \(Int(width)))")
    if !passed { failures += 1 }
}

moveCursor(to: notch)
wait(0.05)
moveCursor(to: away)
wait(0.25)
expect("a brief hover does not expand", expanded: false)

moveCursor(to: notch)
wait(0.2)
expect("expands after a sustained hover", expanded: true)
wait(0.9)
expect("stays expanded while the pointer remains over the notch", expanded: true)
moveCursor(to: away)
wait(0.15)
expect("stays large while the collapse animation runs", expanded: true)
wait(0.8)
expect("shrinks once the collapse has finished", expanded: false)

moveCursor(to: notch)
wait(0.2)
moveCursor(to: away)
wait(0.15)
expect("interrupted expansion stays large while it collapses", expanded: true)
wait(0.9)
expect("then shrinks", expanded: false)

moveCursor(to: notch)
wait(0.2)
moveCursor(to: away)
wait(0.12)
moveCursor(to: notch)
wait(0.8)
expect("returning to the notch during collapse does not reopen it", expanded: false)
moveCursor(to: away)
wait(0.2)
moveCursor(to: notch)
wait(0.2)
expect("a fresh hover after leaving reopens it", expanded: true)
moveCursor(to: away)
wait(1)
expect("and collapses on the final exit", expanded: false)

let roundTrips = 10
let before = cpuNanoseconds()
for _ in 0..<roundTrips {
    moveCursor(to: notch)
    wait(0.7)
    moveCursor(to: away)
    wait(0.8)
}
let perTransition = (cpuNanoseconds() - before) / 1e6 / Double(roundTrips * 2)
let cpuPassed = perTransition <= maxCpuMsPerTransition
print("\(cpuPassed ? "ok  " : "FAIL")  CPU per transition \(String(format: "%.2f", perTransition)) ms (budget \(maxCpuMsPerTransition))")
if !cpuPassed { failures += 1 }

moveCursor(to: originalCursor)
exit(failures == 0 ? 0 : 1)

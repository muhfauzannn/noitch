// Candidate: NSPanel hosting SwiftUI. Same geometry and spring as appkit_ca.swift.
import AppKit
import SwiftUI

final class Panel: NSPanel {
    override var canBecomeKey: Bool { false }
    override var canBecomeMain: Bool { false }
}

@Observable final class Model { var expanded = false }

struct PillView: View {
    let model: Model
    var body: some View {
        VStack(spacing: 0) {
            UnevenRoundedRectangle(bottomLeadingRadius: model.expanded ? 24 : 12, bottomTrailingRadius: model.expanded ? 24 : 12, style: .continuous)
                .fill(.black)
                .frame(width: model.expanded ? 440 : 200, height: model.expanded ? 140 : 32)
            Spacer(minLength: 0)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

@MainActor final class Delegate: NSObject, NSApplicationDelegate {
    var panel: Panel!
    let model = Model()

    func applicationDidFinishLaunching(_ note: Notification) {
        let screen = NSScreen.main!
        let size = NSSize(width: 480, height: 160)
        let frame = NSRect(x: screen.frame.midX - size.width / 2, y: screen.frame.maxY - size.height, width: size.width, height: size.height)
        panel = Panel(contentRect: frame, styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: false)
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = false
        panel.level = .statusBar
        panel.collectionBehavior = [.canJoinAllSpaces, .stationary, .ignoresCycle, .fullScreenAuxiliary]
        panel.contentView = NSHostingView(rootView: PillView(model: model))
        panel.orderFrontRegardless()
        CATransaction.flush()
        DispatchQueue.main.async { reportReady() }
        if ProcessInfo.processInfo.environment["BENCH_MODE"] == "animate" {
            Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [self] _ in
                MainActor.assumeIsolated {
                    withAnimation(.spring(duration: 0.4, bounce: 0.2)) { model.expanded.toggle() }
                }
            }
        }
    }
}

func reportReady() {
    var tv = timeval()
    gettimeofday(&tv, nil)
    print("READY \(Double(tv.tv_sec) + Double(tv.tv_usec) / 1e6)")
    fflush(stdout)
}

let app = NSApplication.shared
app.setActivationPolicy(.accessory)
let delegate = MainActor.assumeIsolated { Delegate() }
MainActor.assumeIsolated { app.delegate = delegate }
app.run()

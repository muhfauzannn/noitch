// Candidate: appkit_ca.swift with SwiftUI linked. BENCH_MODE=lazy opens then closes one hosted window, as a settings window would.
import AppKit
import QuartzCore
import SwiftUI

final class Panel: NSPanel {
    override var canBecomeKey: Bool { false }
    override var canBecomeMain: Bool { false }
}

@MainActor final class Delegate: NSObject, NSApplicationDelegate {
    var panel: Panel!
    let pill = CALayer()
    var expanded = false
    var settings: NSWindow?

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
        let view = NSView(frame: NSRect(origin: .zero, size: size))
        view.wantsLayer = true
        panel.contentView = view
        pill.backgroundColor = NSColor.black.cgColor
        pill.anchorPoint = CGPoint(x: 0.5, y: 1)
        pill.position = CGPoint(x: size.width / 2, y: size.height)
        pill.bounds = CGRect(x: 0, y: 0, width: 200, height: 32)
        pill.cornerRadius = 12
        pill.cornerCurve = .continuous
        pill.maskedCorners = [.layerMinXMinYCorner, .layerMaxXMinYCorner]
        view.layer!.addSublayer(pill)
        panel.orderFrontRegardless()
        CATransaction.flush()
        DispatchQueue.main.async { reportReady() }
        if ProcessInfo.processInfo.environment["BENCH_MODE"] == "lazy" {
            DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [self] in
                let window = NSWindow(contentRect: NSRect(x: 200, y: 200, width: 420, height: 300), styleMask: [.titled, .closable], backing: .buffered, defer: false)
                window.isReleasedWhenClosed = false
                window.contentView = NSHostingView(rootView: Form { Toggle("Launch at login", isOn: .constant(true)); Text("Settings") }.padding())
                window.orderFrontRegardless()
                settings = window
                DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [self] in
                    settings?.close()
                    settings?.contentView = nil
                    settings = nil
                }
            }
        }
        if ProcessInfo.processInfo.environment["BENCH_MODE"] == "animate" {
            Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [self] _ in
                MainActor.assumeIsolated { toggle() }
            }
        }
    }

    func toggle() {
        expanded.toggle()
        let bounds = CGRect(x: 0, y: 0, width: expanded ? 440 : 200, height: expanded ? 140 : 32)
        let radius: CGFloat = expanded ? 24 : 12
        let from = pill.presentation() ?? pill
        for (key, old, new) in [("bounds", NSValue(rect: from.bounds), NSValue(rect: bounds)), ("cornerRadius", from.cornerRadius as NSNumber, radius as NSNumber)] as [(String, Any, Any)] {
            let spring = CASpringAnimation(perceptualDuration: 0.4, bounce: 0.2)
            spring.keyPath = key
            spring.duration = spring.settlingDuration
            spring.fromValue = old
            spring.toValue = new
            pill.add(spring, forKey: key)
        }
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        pill.bounds = bounds
        pill.cornerRadius = radius
        CATransaction.commit()
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

import CoreGraphics
import Foundation
import Testing
@testable import Noitch

struct IslandGeometryTests {
    private let notched = IslandGeometry(display: .init(
        frame: CGRect(x: 0, y: 0, width: 1512, height: 982),
        notchHeight: 38,
        notchWidth: 200,
        menuBarHeight: 38
    ))
    private let external = IslandGeometry(display: .init(
        frame: CGRect(x: 1512, y: 100, width: 2560, height: 1440),
        notchHeight: 0,
        notchWidth: 0,
        menuBarHeight: 25
    ))

    @Test func `custom size persists and out of range values are clamped`() throws {
        let name = "noitch-size-test-" + UUID().uuidString
        let defaults = try #require(UserDefaults(suiteName: name))
        defer { defaults.removePersistentDomain(forName: name) }
        var settings = AppSettings()
        settings.general.expandedWidth = 720
        settings.general.expandedHeight = 230
        settings.save(to: defaults)
        let loaded = AppSettings.load(from: defaults)
        #expect(loaded.general.expandedWidth == 720)
        #expect(loaded.general.expandedHeight == 230)
        defaults.set(9999.0, forKey: "expandedWidth")
        #expect(AppSettings.load(from: defaults).general.expandedWidth == 900)
    }

    @Test func `custom expanded size leaves the closed pill unchanged`() {
        var custom = notched
        custom.expandedSize = CGSize(width: 720, height: 230)
        #expect(custom.pillSize(for: .expanded) == CGSize(width: 720, height: 230))
        #expect(custom.pillSize(for: .collapsed) == notched.pillSize(for: .collapsed))
        #expect(custom.panelFrame(for: .expanded).width == 752)
    }

    @Test func `collapsed pill exactly covers the notch`() {
        #expect(notched.pillSize(for: .collapsed) == CGSize(width: 200, height: 38))
    }

    @Test func `a display without a notch gets a virtual one the height of the menu bar`() {
        #expect(external.pillSize(for: .collapsed) == CGSize(width: IslandGeometry.virtualNotchWidth, height: 25))
    }

    @Test func `the collapsed window fits the pill and flares, so it blocks no clicks around them`() {
        let pill = notched.pillSize(for: .collapsed)
        let frame = notched.panelFrame(for: .collapsed)
        #expect(frame.width == pill.width + IslandGeometry.flareRadius * 2)
        #expect(frame.height == pill.height + IslandGeometry.topEdgeHitMargin)
    }

    @Test func `the expanded window has room for the flares as well as overshoot`() {
        #expect(IslandGeometry.overshootMargin > IslandGeometry.flareRadius)
    }

    @Test func `the expanded window leaves room for spring overshoot`() {
        let frame = notched.panelFrame(for: .expanded)
        let pill = notched.pillSize(for: .expanded)
        #expect(frame.width == pill.width + IslandGeometry.overshootMargin * 2)
        #expect(frame.height == pill.height + IslandGeometry.overshootMargin + IslandGeometry.topEdgeHitMargin)
    }

    @Test func `opening is slower and no bouncier than closing`() {
        let open = IslandGeometry.spring(for: .expanded)
        let close = IslandGeometry.spring(for: .collapsed)
        #expect(open.response > close.response)
        #expect(open.bounce <= close.bounce)
    }

    @Test(arguments: [IslandState.collapsed, .expanded])
    func `the window is centered and its hit area reaches the display top edge`(state: IslandState) {
        for geometry in [notched, external] {
            let frame = geometry.panelFrame(for: state)
            #expect(frame.midX == geometry.display.frame.midX)
            #expect(frame.maxY == geometry.display.frame.maxY + IslandGeometry.topEdgeHitMargin)
        }
    }
}

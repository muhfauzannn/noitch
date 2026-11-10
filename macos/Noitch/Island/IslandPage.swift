import CoreGraphics

/// The pages the expanded island can show, in tab order.
enum IslandPage: Int, CaseIterable {
    case home
    case usage

    func neighbor(_ step: Int) -> IslandPage? { IslandPage(rawValue: rawValue + step) }
}

/// Something in the header that every page shares.
enum HeaderControl: Hashable {
    case tab(IslandPage)
    case settings
}

/// The shared header, from the pill's top-left corner with y growing downward: page tabs at the left
/// of the band beside the notch, settings at the right. Pages keep their own buttons clear of these.
/// The Windows app uses the same numbers.
struct HeaderLayout: Equatable {
    static let sidePadding: CGFloat = 22
    static let button: CGFloat = 26
    static let gap: CGFloat = 4

    let size: CGSize
    let headerHeight: CGFloat

    func frame(of control: HeaderControl) -> CGRect {
        let y = max((headerHeight - Self.button) / 2, 0)
        switch control {
        case .tab(let page):
            return CGRect(x: Self.sidePadding + CGFloat(page.rawValue) * (Self.button + Self.gap), y: y, width: Self.button, height: Self.button)
        case .settings:
            return CGRect(x: size.width - Self.sidePadding - Self.button, y: y, width: Self.button, height: Self.button)
        }
    }

    func control(at point: CGPoint) -> HeaderControl? {
        let controls = IslandPage.allCases.map(HeaderControl.tab) + [.settings]
        return controls.first { frame(of: $0).contains(point) }
    }
}

/// Turns a stream of horizontal scroll deltas into at most one page step per gesture, so momentum
/// can't skip a page. A pause longer than `gestureGap` starts a new gesture. Positive steps forward.
struct PageSwipe {
    static let threshold = 40.0
    static let gestureGap = 0.25

    private var accumulated = 0.0
    private var lastTime = -Double.infinity
    private var consumed = false

    mutating func add(_ delta: Double, at time: Double) -> Int {
        if time - lastTime > Self.gestureGap {
            accumulated = 0
            consumed = false
        }
        lastTime = time
        guard !consumed else { return 0 }
        accumulated += delta
        guard abs(accumulated) >= Self.threshold else { return 0 }
        consumed = true
        return accumulated > 0 ? 1 : -1
    }

    mutating func reset() {
        accumulated = 0
        consumed = false
    }
}

import AppKit

final class AppDelegate: NSObject, NSApplicationDelegate {
    private var island: IslandController?

    func applicationDidFinishLaunching(_ notification: Notification) {
        island = IslandController()
    }
}

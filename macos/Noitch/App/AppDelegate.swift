import AppKit

final class AppDelegate: NSObject, NSApplicationDelegate {
    private var island: IslandController?
    private var terminationSignal: DispatchSourceSignal?

    func applicationDidFinishLaunching(_ notification: Notification) {
        island = IslandController()
        // `pkill` is a documented way to quit, and a plain SIGTERM would skip the cleanup below.
        signal(SIGTERM, SIG_IGN)
        let source = DispatchSource.makeSignalSource(signal: SIGTERM, queue: .main)
        source.setEventHandler { NSApp.terminate(nil) }
        source.resume()
        terminationSignal = source
    }

    func applicationWillTerminate(_ notification: Notification) {
        island?.stop()
    }
}

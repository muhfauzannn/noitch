import AppKit

final class AppDelegate: NSObject, NSApplicationDelegate {
    private var island: IslandController?
    private var terminationSignal: DispatchSourceSignal?

    func applicationDidFinishLaunching(_ notification: Notification) {
        // As a unit test host the app only loads the tests. An island would start the now playing
        // helper, and a test run that ends with SIGKILL would leave that child behind.
        guard ProcessInfo.processInfo.environment["XCTestConfigurationFilePath"] == nil else { return }
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

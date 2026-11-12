import AppKit
import Carbon.HIToolbox
import os

/// A key and its modifiers, for the shortcut that pins the island open. Pure values; the names are
/// the ones macOS shows in menus.
struct Shortcut: Equatable {
    /// A virtual key code, as `NSEvent.keyCode` gives it.
    var keyCode: UInt16
    var command = false
    var option = false
    var control = false
    var shift = false

    /// ⌃⌥N.
    static let pinDefault = Shortcut(keyCode: UInt16(kVK_ANSI_N), option: true, control: true)

    /// A global shortcut needs ⌘, ⌥ or ⌃: with ⇧ alone it would steal typing.
    var isUsable: Bool { (command || option || control) && Self.keyNames[keyCode] != nil }

    init(keyCode: UInt16, command: Bool = false, option: Bool = false, control: Bool = false, shift: Bool = false) {
        self.keyCode = keyCode
        self.command = command
        self.option = option
        self.control = control
        self.shift = shift
    }

    init(event: NSEvent) {
        let flags = event.modifierFlags
        self.init(keyCode: event.keyCode, command: flags.contains(.command), option: flags.contains(.option),
                  control: flags.contains(.control), shift: flags.contains(.shift))
    }

    /// "⌃⌥N", modifiers in the order menus use.
    var display: String {
        (control ? "⌃" : "") + (option ? "⌥" : "") + (shift ? "⇧" : "") + (command ? "⌘" : "") + (Self.keyNames[keyCode] ?? "?")
    }

    /// Carbon's modifier mask, for `RegisterEventHotKey`.
    var carbonModifiers: UInt32 {
        (command ? UInt32(cmdKey) : 0) | (option ? UInt32(optionKey) : 0) | (control ? UInt32(controlKey) : 0) | (shift ? UInt32(shiftKey) : 0)
    }

    /// Stored as two numbers: the key, and the modifiers as bits (1 ⌘, 2 ⌥, 4 ⌃, 8 ⇧).
    var storedModifiers: Double {
        Double((command ? 1 : 0) | (option ? 2 : 0) | (control ? 4 : 0) | (shift ? 8 : 0))
    }

    init?(storedKey: Double, modifiers: Double) {
        guard storedKey >= 0, storedKey < 256, modifiers.isFinite else { return nil }
        let bits = Int(modifiers)
        self.init(keyCode: UInt16(storedKey), command: bits & 1 != 0, option: bits & 2 != 0, control: bits & 4 != 0, shift: bits & 8 != 0)
        guard isUsable else { return nil }
    }

    /// The keys a shortcut may use, by the names on an ANSI keyboard.
    static let keyNames: [UInt16: String] = {
        var names: [UInt16: String] = [:]
        let letters: [(Int, String)] = [
            (kVK_ANSI_A, "A"), (kVK_ANSI_B, "B"), (kVK_ANSI_C, "C"), (kVK_ANSI_D, "D"), (kVK_ANSI_E, "E"), (kVK_ANSI_F, "F"),
            (kVK_ANSI_G, "G"), (kVK_ANSI_H, "H"), (kVK_ANSI_I, "I"), (kVK_ANSI_J, "J"), (kVK_ANSI_K, "K"), (kVK_ANSI_L, "L"),
            (kVK_ANSI_M, "M"), (kVK_ANSI_N, "N"), (kVK_ANSI_O, "O"), (kVK_ANSI_P, "P"), (kVK_ANSI_Q, "Q"), (kVK_ANSI_R, "R"),
            (kVK_ANSI_S, "S"), (kVK_ANSI_T, "T"), (kVK_ANSI_U, "U"), (kVK_ANSI_V, "V"), (kVK_ANSI_W, "W"), (kVK_ANSI_X, "X"),
            (kVK_ANSI_Y, "Y"), (kVK_ANSI_Z, "Z"),
            (kVK_ANSI_0, "0"), (kVK_ANSI_1, "1"), (kVK_ANSI_2, "2"), (kVK_ANSI_3, "3"), (kVK_ANSI_4, "4"),
            (kVK_ANSI_5, "5"), (kVK_ANSI_6, "6"), (kVK_ANSI_7, "7"), (kVK_ANSI_8, "8"), (kVK_ANSI_9, "9"),
            (kVK_Space, "Space"), (kVK_Return, "↩"), (kVK_Tab, "⇥"), (kVK_Escape, "⎋"),
            (kVK_LeftArrow, "←"), (kVK_RightArrow, "→"), (kVK_UpArrow, "↑"), (kVK_DownArrow, "↓"),
            (kVK_ANSI_Minus, "-"), (kVK_ANSI_Equal, "="), (kVK_ANSI_LeftBracket, "["), (kVK_ANSI_RightBracket, "]"),
            (kVK_ANSI_Semicolon, ";"), (kVK_ANSI_Quote, "'"), (kVK_ANSI_Comma, ","), (kVK_ANSI_Period, "."),
            (kVK_ANSI_Slash, "/"), (kVK_ANSI_Backslash, "\\"), (kVK_ANSI_Grave, "`"),
            (kVK_F1, "F1"), (kVK_F2, "F2"), (kVK_F3, "F3"), (kVK_F4, "F4"), (kVK_F5, "F5"), (kVK_F6, "F6"),
            (kVK_F7, "F7"), (kVK_F8, "F8"), (kVK_F9, "F9"), (kVK_F10, "F10"), (kVK_F11, "F11"), (kVK_F12, "F12"),
        ]
        for (code, name) in letters { names[UInt16(code)] = name }
        return names
    }()
}

/// One system-wide shortcut, registered with Carbon's `RegisterEventHotKey`: public API that needs no
/// accessibility permission and delivers an event only when the keys are pressed, so nothing runs
/// while waiting.
final class HotKey {
    private static let logger = Logger(subsystem: "app.noitch.mac", category: "HotKey")
    /// The last shortcut set in settings was refused, so settings can say so.
    static var refused = false
    /// Called on the main thread when the shortcut is pressed.
    var onPress: (() -> Void)?

    private var reference: EventHotKeyRef?
    private var handler: EventHandlerRef?

    /// Replaces any earlier shortcut. Returns false when the system refused it, usually because
    /// another app holds the same keys.
    @discardableResult
    func register(_ shortcut: Shortcut?) -> Bool {
        unregister()
        guard let shortcut, shortcut.isUsable else { return true }
        if handler == nil {
            var type = EventTypeSpec(eventClass: OSType(kEventClassKeyboard), eventKind: UInt32(kEventHotKeyPressed))
            let context = Unmanaged.passUnretained(self).toOpaque()
            InstallEventHandler(GetApplicationEventTarget(), { _, _, context in
                guard let context else { return noErr }
                let hotKey = Unmanaged<HotKey>.fromOpaque(context).takeUnretainedValue()
                MainActor.assumeIsolated { hotKey.onPress?() }
                return noErr
            }, 1, &type, context, &handler)
        }
        let id = EventHotKeyID(signature: OSType(0x4E_6F_69_74), id: 1)  // "Noit"
        let status = RegisterEventHotKey(UInt32(shortcut.keyCode), shortcut.carbonModifiers, id, GetApplicationEventTarget(), 0, &reference)
        if status != noErr {
            Self.logger.debug("Shortcut refused: \(status)")
            reference = nil
            return false
        }
        return true
    }

    func unregister() {
        if let reference { UnregisterEventHotKey(reference) }
        reference = nil
    }
}

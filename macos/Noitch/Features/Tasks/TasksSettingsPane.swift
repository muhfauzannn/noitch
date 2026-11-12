import AppKit

/// The tasks page's page in the settings window: the Moodle account, and how the island keeps up.
enum TasksSettingsPane {
    static let pane = SettingsPane(title: "Tasks", glyph: .icon(.tasks), color: .systemTeal) { form in
        AccountRows(form: form).build()
        form.section("Checking", footer: "Moodle can't tell Noitch about new work, so Noitch asks the site. Each check is one small request.") {
            form.choice("Check for new tasks", caption: "Also when the Mac wakes and when the island opens.", \.tasks.checkMinutes,
                        options: TasksSettings.checkChoices.map { ($0 == 0 ? "Only when I open the island" : $0 == 60 ? "Every hour" : "Every 30 minutes", $0) },
                        enabledBy: \.pages.showTasks)
            form.toggle("Alert on the closed island", caption: "A line under the notch for a few seconds, then a dot until you've seen the page.",
                        \.tasks.alerts, enabledBy: \.pages.showTasks)
            form.choice("Remind before due", \.tasks.reminders, options: ReminderChoice.allCases.map { ($0.title, $0) }, enabledBy: \.pages.showTasks)
            form.choice("Show", \.tasks.scope, options: TaskScope.allCases.map { ($0.title, $0) }, enabledBy: \.pages.showTasks)
        }
    }
}

/// The account part of the pane: who is signed in, or the fields to sign in with. Kept alive by the
/// form through its buttons' handlers, so it lives exactly as long as the page.
private final class AccountRows {
    private weak var form: SettingsForm?
    private let sitePicker = NSPopUpButton(frame: .zero, pullsDown: false)
    private let site = AccountRows.field("")
    private let username = AccountRows.field("")
    private let password = NSSecureTextField()
    private let key = AccountRows.field("")
    private let signIn = AccountRows.button("Sign in")
    private let useKey = AccountRows.button("Use key")
    private let signOut = AccountRows.button("Sign out")
    private var accountCaption: NSTextField?
    private var status: NSTextField?
    private var signedInViews: [NSView] = []
    private var signedOutViews: [NSView] = []

    init(form: SettingsForm) {
        self.form = form
        AccountRows.style(password)
        signIn.keyEquivalent = "\r"
        sitePicker.controlSize = .small
        sitePicker.font = .systemFont(ofSize: NSFont.smallSystemFontSize)
        sitePicker.addItems(withTitles: TasksSettings.knownSites.map(\.0) + ["Other…"])
        site.placeholderString = "moodle.example.ac.id"
    }

    func build() {
        guard let form else { return }
        signedInViews = form.section("Account", footer: "Signing out deletes the key from your Keychain. To revoke it on the site too, reset it under Preferences › Security keys.") {
            accountCaption = form.row("Signed in", caption: " ", control: signOut)
        }
        signedOutViews = form.section("Account", footer: "Your password is sent once to the site to get a key, and isn't stored. The key is kept in your Keychain.") {
            form.row("Site", control: sitePicker)
            form.row("Address", caption: "Only for Other: any Moodle site over https.", control: site)
            form.row("Username", control: username)
            form.row("Password", control: password)
            status = form.row("", caption: " ", control: signIn)
        }
        signedOutViews += form.section("Or use a security key", footer: "From the site's Preferences › Security keys: the key for the Moodle mobile web service.") {
            form.row("Key", control: key)
            form.row("", control: useKey)
        }
        form.bind(sitePicker) { [self] _ in siteChosen() }
        form.bind(signIn) { [self] _ in startSignIn() }
        form.bind(useKey) { [self] _ in startUseKey() }
        form.bind(signOut) { [self] _ in doSignOut() }
        report(nil)
        refresh()
    }

    /// A known site fills the address and locks it; "Other…" opens it for typing.
    private func siteChosen() {
        let index = sitePicker.indexOfSelectedItem
        let known = TasksSettings.knownSites.indices.contains(index)
        site.isEnabled = !known
        if known {
            site.stringValue = TasksSettings.knownSites[index].1
        } else {
            if TasksSettings.knownSites.contains(where: { $0.1 == site.stringValue }) { site.stringValue = "" }
            site.window?.makeFirstResponder(site)
        }
        report(nil)
    }

    private func refresh() {
        guard let settings = form?.settings.tasks else { return }
        site.stringValue = settings.site
        let known = TasksSettings.knownSites.firstIndex { $0.1 == settings.site }
        sitePicker.selectItem(at: known ?? TasksSettings.knownSites.count)
        site.isEnabled = known == nil
        accountCaption?.stringValue = settings.accountName + " · " + (URL(string: settings.site)?.host ?? settings.site)
        signedInViews.forEach { $0.isHidden = !settings.isSignedIn }
        signedOutViews.forEach { $0.isHidden = settings.isSignedIn }
    }

    private func report(_ text: String?) {
        status?.stringValue = text ?? ""
        status?.isHidden = text == nil
    }

    private func targetSite() -> String? {
        guard let site = TasksSettings.normalizedSite(site.stringValue) else {
            report("The site needs to be an https address, such as scele.cs.ui.ac.id.")
            return nil
        }
        return site
    }

    private func startSignIn() {
        guard let target = targetSite() else { return }
        let name = username.stringValue.trimmingCharacters(in: .whitespaces)
        let secret = password.stringValue
        guard !name.isEmpty, !secret.isEmpty else {
            report("Enter your username and password.")
            return
        }
        // The password lives only in this request; the field forgets it now.
        password.stringValue = ""
        setBusy("Signing in…")
        Task { [self] in finish(await MoodleSource.signIn(site: target, username: name, password: secret), site: target) }
    }

    private func startUseKey() {
        guard let target = targetSite() else { return }
        let token = key.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !token.isEmpty else {
            report("Paste the key first.")
            return
        }
        setBusy("Checking the key…")
        Task { [self] in finish(await MoodleSource.useKey(site: target, token: token), site: target) }
    }

    private func setBusy(_ text: String) {
        signIn.isEnabled = false
        useKey.isEnabled = false
        report(text)
    }

    private func finish(_ result: Result<(token: String, info: MoodleFormat.SiteInfo), MoodleProblem>, site: String) {
        signIn.isEnabled = true
        useKey.isEnabled = true
        switch result {
        case let .success((token, info)):
            guard info.functions.isEmpty || info.functions.contains("core_calendar_get_action_events_by_timesort") else {
                report("This site doesn't let this account read its task list.")
                return
            }
            guard TaskToken.save(token, site: site) else {
                report("Couldn't keep the key in your Keychain.")
                return
            }
            key.stringValue = ""
            report(nil)
            form?.update {
                $0.tasks.site = site
                $0.tasks.accountName = info.userName.isEmpty ? "Signed in" : info.userName
            }
            refresh()
        case let .failure(problem):
            report(Self.message(for: problem))
        }
    }

    private func doSignOut() {
        guard let form else { return }
        TaskToken.delete(site: form.settings.tasks.site)
        form.update { $0.tasks.accountName = "" }
        refresh()
    }

    private static func message(for problem: MoodleProblem) -> String {
        switch problem {
        case .wrongLogin: "Wrong username or password."
        case let .noToken(reason): "The site didn't issue a key (\(reason)). Try a security key instead."
        case .signedOut: "The site refused this key."
        case .maintenance: "The site is under maintenance. Try again later."
        case .unavailable: "Couldn't reach the site."
        }
    }

    private static func field(_ text: String) -> NSTextField {
        let field = NSTextField(string: text)
        style(field)
        return field
    }

    private static func style(_ field: NSTextField) {
        field.controlSize = .small
        field.font = .systemFont(ofSize: NSFont.smallSystemFontSize)
        field.widthAnchor.constraint(equalToConstant: 220).isActive = true
    }

    private static func button(_ title: String) -> NSButton {
        let button = NSButton(title: title, target: nil, action: nil)
        button.controlSize = .small
        button.bezelStyle = .push
        return button
    }
}

import Foundation
import Security
import os

/// Talks to a Moodle site's web service, the documented API its mobile app uses. Approved by the
/// owner on 2026-11-12, with the limits in docs/architecture.md (Tasks):
///
/// - The password is sent once, to `login/token.php`, and never kept. The key it returns is kept in
///   the Keychain, read for each request and dropped after it.
/// - The key travels in the request body, never in a URL, and is never logged.
/// - Requests run off the main thread in a task; nothing here repeats on its own.
nonisolated enum MoodleSource {
    private static let logger = Logger(subsystem: "app.noitch.mac", category: "Tasks")
    private static let service = "moodle_mobile_app"

    /// Signs in and checks the key, returning it and who it belongs to.
    @concurrent
    static func signIn(site: String, username: String, password: String) async -> Result<(token: String, info: MoodleFormat.SiteInfo), MoodleProblem> {
        let answer = await post(site: site, path: "login/token.php", fields: [
            ("username", username), ("password", password), ("service", service),
        ])
        switch answer.flatMap(MoodleFormat.token) {
        case let .failure(problem): return .failure(problem)
        case let .success(token): return await useKey(site: site, token: token)
        }
    }

    /// Checks a key pasted from the site's Security keys page.
    @concurrent
    static func useKey(site: String, token: String) async -> Result<(token: String, info: MoodleFormat.SiteInfo), MoodleProblem> {
        let answer = await call(site: site, token: token, function: "core_webservice_get_site_info", arguments: [])
        return answer.flatMap(MoodleFormat.siteInfo).map { (token, $0) }
    }

    /// What is still to do, from a week ago on: the list the Dashboard's Timeline shows.
    @concurrent
    static func tasks(site: String, now: Double) async -> Result<[MoodleTask], MoodleProblem> {
        guard let token = TaskToken.read(site: site) else { return .failure(.signedOut) }
        let answer = await call(site: site, token: token, function: "core_calendar_get_action_events_by_timesort", arguments: [
            ("timesortfrom", String(Int(now - TaskRule.overdueKept))),
            ("limitnum", "50"),
            ("limittononsuspendedevents", "1"),
        ])
        return answer.flatMap(MoodleFormat.tasks)
    }

    private static func call(site: String, token: String, function: String, arguments: [(String, String)]) async -> Result<Data, MoodleProblem> {
        await post(site: site, path: "webservice/rest/server.php", fields: [
            ("wstoken", token), ("wsfunction", function), ("moodlewsrestformat", "json"),
        ] + arguments)
    }

    private static func post(site: String, path: String, fields: [(String, String)]) async -> Result<Data, MoodleProblem> {
        guard let url = URL(string: "\(site)/\(path)"), url.scheme == "https" else { return .failure(.unavailable) }
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 15
        configuration.httpAdditionalHeaders = ["User-Agent": "Noitch (https://github.com/muhfauzannn/noitch)"]
        let session = URLSession(configuration: configuration)
        defer { session.finishTasksAndInvalidate() }
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.setValue("application/x-www-form-urlencoded", forHTTPHeaderField: "Content-Type")
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        request.httpBody = Data(fields.map { "\(encode($0))=\(encode($1))" }.joined(separator: "&").utf8)
        do {
            let (data, response) = try await session.data(for: request)
            let status = (response as? HTTPURLResponse)?.statusCode ?? 0
            guard status == 200 else {
                logger.debug("Moodle answered \(status)")
                return .failure(status == 503 ? .maintenance : .unavailable)
            }
            return .success(data)
        } catch {
            logger.debug("Moodle request failed: \(error.localizedDescription)")
            return .failure(.unavailable)
        }
    }

    private static let formSafe = CharacterSet(charactersIn: "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~")

    private static func encode(_ text: String) -> String {
        text.addingPercentEncoding(withAllowedCharacters: formSafe) ?? ""
    }
}

/// The site's key in the login Keychain, as a generic password only this app wrote. A rebuilt app
/// has a new signature, so macOS asks once whether it may still read the item.
nonisolated enum TaskToken {
    private static let service = "app.noitch.moodle"

    private static func account(_ site: String) -> String { URL(string: site)?.host ?? site }

    static func read(site: String) -> String? {
        let query: [CFString: Any] = [
            kSecClass: kSecClassGenericPassword,
            kSecAttrService: service,
            kSecAttrAccount: account(site),
            kSecReturnData: true,
            kSecMatchLimit: kSecMatchLimitOne,
        ]
        var item: CFTypeRef?
        guard SecItemCopyMatching(query as CFDictionary, &item) == errSecSuccess, let data = item as? Data else { return nil }
        return String(data: data, encoding: .utf8)
    }

    @discardableResult
    static func save(_ token: String, site: String) -> Bool {
        delete(site: site)
        let item: [CFString: Any] = [
            kSecClass: kSecClassGenericPassword,
            kSecAttrService: service,
            kSecAttrAccount: account(site),
            kSecAttrLabel: "Noitch: Moodle key for \(account(site))",
            kSecAttrAccessible: kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly,
            kSecValueData: Data(token.utf8),
        ]
        return SecItemAdd(item as CFDictionary, nil) == errSecSuccess
    }

    static func delete(site: String) {
        let query: [CFString: Any] = [kSecClass: kSecClassGenericPassword, kSecAttrService: service, kSecAttrAccount: account(site)]
        SecItemDelete(query as CFDictionary)
    }
}

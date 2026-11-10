import Foundation
import Security
import os

/// Asks Anthropic for Claude Code's five-hour and weekly limits, the numbers its `/usage` command
/// shows. This uses Claude Code's own login from the Keychain and an endpoint Anthropic doesn't
/// document: an approved exception, limited as described in docs/architecture.md.
///
/// - One request when the usage page is shown and one a minute while it stays shown; none otherwise.
/// - The token is read for each request and never stored, logged or refreshed. Refreshing would
///   replace Claude Code's own login and sign it out, so an expired token is only reported.
/// - After a Keychain refusal it stops asking until Noitch is opened again.
final class ClaudeLimitSource {
    private nonisolated static let logger = Logger(subsystem: "app.noitch.mac", category: "Usage")
    private nonisolated static let endpoint = URL(string: "https://api.anthropic.com/api/oauth/usage")!
    /// Shared across instances: a refusal should hold until the app is opened again.
    private static var keychainDenied = false

    private var task: Task<Void, Never>?

    func cancel() {
        task?.cancel()
        task = nil
    }

    /// Replaces any request still running. `completion` isn't called for a cancelled one.
    func fetch(now: Double, completion: @escaping (ToolLimits) -> Void) {
        cancel()
        guard !Self.keychainDenied else {
            completion(.failed(.keychainDenied))
            return
        }
        task = Task {
            let limits = await Self.request(now: now)
            guard !Task.isCancelled else { return }
            if limits.problem == .keychainDenied { Self.keychainDenied = true }
            completion(limits)
        }
    }

    private nonisolated enum LoginResult {
        case found(ClaudeUsageFormat.Login)
        case missing
        case denied
    }

    @concurrent
    private static func request(now: Double) async -> ToolLimits {
        let login: ClaudeUsageFormat.Login
        switch readLogin() {
        case .missing: return .failed(.signedOut)
        case .denied: return .failed(.keychainDenied)
        case let .found(found): login = found
        }
        if let expires = login.expiresAt, expires <= now { return .failed(.expired) }

        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 10
        configuration.httpAdditionalHeaders = ["User-Agent": "Noitch (https://github.com/muhfauzannn/noitch)"]
        let session = URLSession(configuration: configuration)
        defer { session.finishTasksAndInvalidate() }
        var request = URLRequest(url: endpoint)
        request.setValue("Bearer \(login.accessToken)", forHTTPHeaderField: "Authorization")
        request.setValue("oauth-2025-04-20", forHTTPHeaderField: "anthropic-beta")
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        do {
            let (data, response) = try await session.data(for: request)
            let status = (response as? HTTPURLResponse)?.statusCode ?? 0
            guard status == 200 else {
                logger.debug("Usage request answered \(status)")
                return .failed(status == 401 ? .expired : .unavailable)
            }
            return ClaudeUsageFormat.limits(fromResponse: Array(data)[...]) ?? .failed(.unavailable)
        } catch {
            logger.debug("Usage request failed: \(error.localizedDescription)")
            return .failed(.unavailable)
        }
    }

    /// Claude Code keeps its login as a generic password. The first read asks the user, through
    /// the system's own prompt, to let Noitch see it.
    private nonisolated static func readLogin() -> LoginResult {
        let query: [CFString: Any] = [
            kSecClass: kSecClassGenericPassword,
            kSecAttrService: "Claude Code-credentials",
            kSecReturnData: true,
            kSecMatchLimit: kSecMatchLimitOne,
        ]
        var item: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &item)
        switch status {
        case errSecSuccess:
            guard let data = item as? Data, let login = ClaudeUsageFormat.login(fromCredentials: Array(data)[...]) else { return .missing }
            return .found(login)
        case errSecItemNotFound:
            return .missing
        default:
            return .denied
        }
    }
}

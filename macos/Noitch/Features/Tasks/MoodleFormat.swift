import Foundation

/// One thing still to do on Moodle: an assignment to submit, a quiz to attempt, a forum post due.
/// Moodle drops it from the list once it is done, so every task here is still open.
nonisolated struct MoodleTask: Equatable, Sendable {
    var id: Int
    var title: String
    /// A short label for the course, such as "PBP".
    var course: String
    var module: TaskModule
    /// When it is due, in seconds since 1970.
    var due: Double
    /// The activity's page on the site.
    var url: String
}

nonisolated enum TaskModule: String, Equatable, Sendable {
    case assignment
    case quiz
    case forum
    case other

    init(moduleName: String) {
        switch moduleName {
        case "assign": self = .assignment
        case "quiz": self = .quiz
        case "forum", "hsuforum": self = .forum
        default: self = .other
        }
    }
}

/// What Moodle said instead of an answer.
nonisolated enum MoodleProblem: Error, Equatable, Sendable {
    /// The token was refused: signed out, or the key was reset on the site.
    case signedOut
    /// The username or password was wrong.
    case wrongLogin
    /// The site refused to issue a token for this account, or the service is off.
    case noToken(String)
    case maintenance
    /// Offline, a timeout, a server error or an answer that couldn't be read.
    case unavailable
}

/// Reads the answers of Moodle's web service. Pure, so every shape is tested.
nonisolated enum MoodleFormat {
    /// The answer to `login/token.php`.
    static func token(from data: Data) -> Result<String, MoodleProblem> {
        guard let object = json(data) as? [String: Any] else { return .failure(.unavailable) }
        if let token = object["token"] as? String, !token.isEmpty { return .success(token) }
        let code = object["errorcode"] as? String ?? ""
        if code == "invalidlogin" { return .failure(.wrongLogin) }
        if code == "sitemaintenance" { return .failure(.maintenance) }
        let message = (object["error"] as? String) ?? (object["message"] as? String) ?? "The site didn't issue a key."
        return .failure(.noToken(message))
    }

    struct SiteInfo: Equatable, Sendable {
        var userName: String
        var siteName: String
        var functions: Set<String>
    }

    /// The answer to `core_webservice_get_site_info`.
    static func siteInfo(from data: Data) -> Result<SiteInfo, MoodleProblem> {
        guard let object = json(data) as? [String: Any] else { return .failure(.unavailable) }
        if let problem = problem(in: object) { return .failure(problem) }
        guard let name = object["fullname"] as? String else { return .failure(.unavailable) }
        let functions = (object["functions"] as? [[String: Any]] ?? []).compactMap { $0["name"] as? String }
        return .success(SiteInfo(userName: name, siteName: object["sitename"] as? String ?? "", functions: Set(functions)))
    }

    /// The answer to `core_calendar_get_action_events_by_timesort`: what the Timeline block shows.
    static func tasks(from data: Data) -> Result<[MoodleTask], MoodleProblem> {
        guard let object = json(data) as? [String: Any] else { return .failure(.unavailable) }
        if let problem = problem(in: object) { return .failure(problem) }
        guard let events = object["events"] as? [[String: Any]] else { return .failure(.unavailable) }
        let tasks = events.compactMap { event -> MoodleTask? in
            guard let id = number(event["id"]), let due = number(event["timesort"]) ?? number(event["timestart"]) else { return nil }
            let course = event["course"] as? [String: Any]
            let title = (event["activityname"] as? String).flatMap { $0.isEmpty ? nil : $0 }
                ?? TaskRule.activityTitle(fromEventName: event["name"] as? String ?? "")
            // An item whose action can't be taken yet, such as a quiz not open, is still listed.
            let url = (event["url"] as? String) ?? ((event["action"] as? [String: Any])?["url"] as? String) ?? ""
            return MoodleTask(
                id: Int(id),
                title: TaskRule.plainText(title),
                course: TaskRule.courseLabel(shortName: course?["shortname"] as? String ?? "", fullName: course?["fullname"] as? String ?? ""),
                module: TaskModule(moduleName: event["modulename"] as? String ?? ""),
                due: due,
                url: url
            )
        }
        return .success(tasks)
    }

    /// Moodle reports errors with status 200 and an exception object.
    private static func problem(in object: [String: Any]) -> MoodleProblem? {
        guard object["exception"] != nil || object["errorcode"] != nil else { return nil }
        switch object["errorcode"] as? String {
        case "invalidtoken", "accessexception": return .signedOut
        case "sitemaintenance": return .maintenance
        default: return .unavailable
        }
    }

    private static func json(_ data: Data) -> Any? {
        try? JSONSerialization.jsonObject(with: data)
    }

    private static func number(_ value: Any?) -> Double? {
        if let number = value as? NSNumber { return number.doubleValue }
        if let text = value as? String { return Double(text) }
        return nil
    }
}

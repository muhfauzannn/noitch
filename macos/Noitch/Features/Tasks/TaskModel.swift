import Foundation

/// The rules behind the task list, the alerts and the reminders. Pure: values in, values out.
nonisolated enum TaskRule {
    /// Overdue work stays listed this long, so a missed deadline doesn't simply vanish.
    static let overdueKept: Double = 7 * 86400
    /// The page's load bars show this many days, from today.
    static let loadDays = 7
    /// Rows the page's list shows at once; scrolling moves through the rest.
    static let visibleRows = 3
    static let day: Double = 86400

    // MARK: Text

    /// Moodle before 4.0 has no activity name on an event, only "Tugas 3 is due".
    static func activityTitle(fromEventName name: String) -> String {
        for suffix in [" is due to be graded", " is due", " closes", " should be completed", " opens"] where name.hasSuffix(suffix) {
            return String(name.dropLast(suffix.count))
        }
        return name
    }

    /// Names arrive with HTML entities, and sometimes with tags from multi-language filters.
    static func plainText(_ text: String) -> String {
        var result = ""
        var inTag = false
        for character in text {
            if character == "<" { inTag = true; continue }
            if character == ">", inTag { inTag = false; continue }
            if !inTag { result.append(character) }
        }
        for (entity, value) in [("&amp;", "&"), ("&lt;", "<"), ("&gt;", ">"), ("&quot;", "\""), ("&#39;", "'"), ("&#039;", "'"), ("&nbsp;", " ")] {
            result = result.replacingOccurrences(of: entity, with: value)
        }
        return result.split(whereSeparator: \.isWhitespace).joined(separator: " ")
    }

    /// A course label short enough for a chip. A short code is kept; a long one gives way to the
    /// initials of the course's name: "CSGE602022 - Pemrograman Berbasis Platform (A)" is "PBP".
    static func courseLabel(shortName: String, fullName: String) -> String {
        let short = plainText(shortName)
        if !short.isEmpty, short.count <= 10 { return short }
        var name = plainText(fullName.isEmpty ? shortName : fullName)
        // Drops a leading course code ("CSGE602022 - ") and a trailing class ("(A)").
        if let dash = name.range(of: " - ") ?? name.range(of: " – ") {
            let head = name[..<dash.lowerBound]
            if head.contains(where: \.isNumber), !head.contains(" ") { name = String(name[dash.upperBound...]) }
        }
        if let open = name.lastIndex(of: "("), name.last == ")" { name = String(name[..<open]) }
        let small: Set<String> = ["dan", "and", "of", "the", "untuk", "di", "ke", "&", "-"]
        let initials = name.split(separator: " ")
            .filter { word in !small.contains(word.lowercased()) && word.first?.isLetter == true }
            .compactMap(\.first)
            .map { String($0).uppercased() }
            .joined()
        if initials.count >= 2 { return String(initials.prefix(5)) }
        return String((short.isEmpty ? name : short).prefix(10))
    }

    // MARK: The list

    /// What the page and the alerts show: open tasks of the chosen kinds, soonest first, without
    /// work more than a week overdue.
    static func visible(_ tasks: [MoodleTask], scope: TaskScope, now: Double) -> [MoodleTask] {
        tasks
            .filter { scope.includes($0.module) && $0.due >= now - overdueKept }
            .sorted { $0.due == $1.due ? $0.id < $1.id : $0.due < $1.due }
    }

    /// Tasks that weren't there before and are still ahead. Work found already overdue is listed,
    /// but isn't announced.
    static func newTasks(_ tasks: [MoodleTask], seen: Set<Int>, now: Double) -> [MoodleTask] {
        tasks.filter { !seen.contains($0.id) && $0.due > now }
    }

    static func urgency(due: Double, now: Double) -> Severity {
        let left = due - now
        if left < day { return .critical }
        if left < 3 * day { return .watch }
        return .calm
    }

    // MARK: Time

    /// "Overdue 2d", "Today 23:55", "Tomorrow 10:00", "Fri 10:00", "14 Nov".
    static func dueText(_ due: Double, now: Double, calendar: Calendar) -> String {
        if due < now { return "Overdue \(span(now - due, parts: 1))" }
        let dueDate = Date(timeIntervalSince1970: due)
        let days = calendar.dateComponents([.day], from: calendar.startOfDay(for: Date(timeIntervalSince1970: now)), to: calendar.startOfDay(for: dueDate)).day ?? 0
        let parts = calendar.dateComponents([.hour, .minute, .weekday, .day, .month], from: dueDate)
        let time = String(format: "%02d:%02d", parts.hour ?? 0, parts.minute ?? 0)
        switch days {
        case 0: return "Today \(time)"
        case 1: return "Tomorrow \(time)"
        case 2...6: return "\(weekdays[((parts.weekday ?? 1) - 1) % 7]) \(time)"
        default: return "\(parts.day ?? 1) \(months[((parts.month ?? 1) - 1) % 12])"
        }
    }

    /// "Thu 13 Nov, 23:55", for the page's main task.
    static func fullDue(_ due: Double, calendar: Calendar) -> String {
        let parts = calendar.dateComponents([.hour, .minute, .weekday, .day, .month], from: Date(timeIntervalSince1970: due))
        return String(format: "%@ %d %@, %02d:%02d", weekdays[((parts.weekday ?? 1) - 1) % 7], parts.day ?? 1,
                      months[((parts.month ?? 1) - 1) % 12], parts.hour ?? 0, parts.minute ?? 0)
    }

    /// The countdown in its two largest units: "1d 2h", "5h 20m", "45m". Under a minute is "0m".
    static func countdown(_ due: Double, now: Double) -> [(value: Int, unit: String)] {
        pieces(abs(due - now), parts: 2)
    }

    /// "2d", "3h", "1d 4h".
    static func span(_ seconds: Double, parts: Int) -> String {
        pieces(seconds, parts: parts).map { "\($0.value)\($0.unit)" }.joined(separator: " ")
    }

    private static func pieces(_ seconds: Double, parts: Int) -> [(value: Int, unit: String)] {
        let minutes = Int(seconds / 60)
        let all = [(minutes / 1440, "d"), (minutes % 1440 / 60, "h"), (minutes % 60, "m")]
        guard let first = all.firstIndex(where: { $0.0 > 0 }) else { return [(0, "m")] }
        return Array(all[first...].prefix(parts)).filter { $0.0 > 0 || $0.1 == all[first].1 }
    }

    /// Local days from today to the due day: 0 for today, 1 for tomorrow, negative before today.
    static func dayOffset(_ due: Double, now: Double, calendar: Calendar) -> Int {
        calendar.dateComponents([.day], from: calendar.startOfDay(for: Date(timeIntervalSince1970: now)),
                                to: calendar.startOfDay(for: Date(timeIntervalSince1970: due))).day ?? 0
    }

    // MARK: Reminders

    /// A reminder is "\(id)@\(lead)", so each task is reminded once per lead.
    static func reminderKey(_ task: MoodleTask, lead: Double) -> String { "\(task.id)@\(Int(lead))" }

    /// When the next reminder falls, if any.
    static func nextReminder(_ tasks: [MoodleTask], leads: [Double], reminded: Set<String>, now: Double) -> Double? {
        tasks.flatMap { task in leads.map { (task, $0) } }
            .filter { !reminded.contains(reminderKey($0, lead: $1)) && $0.due - $1 > now }
            .map { $0.due - $1 }
            .min()
    }

    /// Reminders that are due now: their time has come within `grace`, and the task is still ahead.
    /// A Mac that slept through one doesn't fire it hours late.
    static func dueReminders(_ tasks: [MoodleTask], leads: [Double], reminded: Set<String>, now: Double, grace: Double = 600) -> [(task: MoodleTask, lead: Double)] {
        tasks.flatMap { task in leads.map { (task: task, lead: $0) } }
            .filter { pair in
                let time = pair.task.due - pair.lead
                return !reminded.contains(reminderKey(pair.task, lead: pair.lead)) && time <= now + 1 && time > now - grace && pair.task.due > now
            }
            .sorted { $0.task.due < $1.task.due }
    }

    static let weekdays = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"]
    static let months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]
}

/// Which kinds of task the island tracks.
nonisolated enum TaskScope: String, CaseIterable, Sendable {
    case assignmentsAndQuizzes
    case everything

    func includes(_ module: TaskModule) -> Bool {
        self == .everything || module == .assignment || module == .quiz
    }

    var title: String {
        switch self {
        case .assignmentsAndQuizzes: "Assignments and quizzes"
        case .everything: "Everything with a due date"
        }
    }
}

/// The last list and the tasks already announced, kept on disk so a restart neither empties the
/// page nor announces everything again.
nonisolated struct TaskCacheRecord: Equatable, Sendable {
    var asOf: Double
    var tasks: [MoodleTask]
    /// Announced, or already there at sign-in: never announced again.
    var seen: Set<Int>
    /// Announced but not yet looked at on the page: the closed island keeps its dot for these.
    var unviewed: Set<Int> = []

    /// "asof 1794…", "seen 1 2 3", "unviewed 3", then one tab-separated line per task: id, due, module, course,
    /// title, url.
    func encode() -> String {
        func clean(_ text: String) -> String {
            text.replacingOccurrences(of: "\t", with: " ").replacingOccurrences(of: "\n", with: " ")
        }
        var lines = ["asof \(asOf)", "seen " + seen.sorted().map(String.init).joined(separator: " "),
                     "unviewed " + unviewed.sorted().map(String.init).joined(separator: " ")]
        for task in tasks {
            lines.append([String(task.id), String(task.due), task.module.rawValue, clean(task.course), clean(task.title), clean(task.url)].joined(separator: "\t"))
        }
        return lines.joined(separator: "\n")
    }

    static func decode(_ text: String) -> TaskCacheRecord? {
        var record = TaskCacheRecord(asOf: 0, tasks: [], seen: [])
        var valid = false
        for line in text.split(separator: "\n", omittingEmptySubsequences: true) {
            if line.hasPrefix("asof ") {
                guard let time = Double(line.dropFirst(5)) else { return nil }
                record.asOf = time
                valid = true
            } else if line.hasPrefix("unviewed") {
                record.unviewed = Set(line.dropFirst(8).split(separator: " ").compactMap { Int($0) })
            } else if line.hasPrefix("seen") {
                record.seen = Set(line.dropFirst(4).split(separator: " ").compactMap { Int($0) })
            } else {
                let fields = line.split(separator: "\t", omittingEmptySubsequences: false).map(String.init)
                guard fields.count == 6, let id = Int(fields[0]), let due = Double(fields[1]) else { continue }
                record.tasks.append(MoodleTask(id: id, title: fields[4], course: fields[3], module: TaskModule(rawValue: fields[2]) ?? .other, due: due, url: fields[5]))
            }
        }
        return valid ? record : nil
    }
}

/// What the tasks page shows, from what the watch knows. Pure; the scene draws it as it is.
nonisolated struct TasksPageModel: Equatable, Sendable {
    struct Row: Equatable, Sendable {
        var id: Int
        var title: String
        var course: String
        var due: String
        var severity: Severity
        var module: TaskModule
        var isNew: Bool
        var url: String
        /// The day's bar, when it falls in the next seven days.
        var day: Int?
    }

    /// One of the next seven days, as a bar as tall as its share of the work.
    struct DayLoad: Equatable, Sendable {
        var letter: String
        var count: Int
        /// Something due within a day falls on it.
        var hasUrgent: Bool
        var hasNew: Bool
        var isBusiest: Bool
    }

    struct Hero: Equatable, Sendable {
        var label: String
        var countdown: [Piece]
        var title: String
        var course: String
        var due: String
        var severity: Severity
        var url: String
    }

    struct Piece: Equatable, Sendable {
        var value: Int
        var unit: String
    }

    enum Content: Equatable, Sendable {
        /// A short message in the middle, with a button when `button` is set; `buttonAction` says what
        /// it does.
        case message(title: String, detail: String, button: String?, allClear: Bool)
        case list(hero: Hero, load: [DayLoad], rows: [Row], more: String)
    }

    enum ButtonAction: Equatable, Sendable {
        case openSettings
        case showOverdue
    }

    var content: Content
    /// Beside the bars: "Busiest: Thu · 3" over "5 due this week".
    var loadCaption = ["", ""]
    var buttonAction = ButtonAction.openSettings
    /// "Show 2 overdue" or "Hide overdue" beside the list, or empty when nothing is overdue.
    var overdueToggle = ""
    /// How many tasks the list scrolls through.
    var listCount = 0
    /// "Scele", "EMAS3" or "Moodle", for the button and messages.
    var siteName = "Scele"

    /// Overdue work is hidden until `showOverdue`: the page is about what can still be done.
    static func make(tasks allTasks: [MoodleTask], unviewed: Set<Int>, signedIn: Bool, checking: Bool, problem: MoodleProblem?,
                     asOf: Double?, now: Double, offset: Int, calendar: Calendar, showOverdue: Bool = false,
                     siteName site: String = "Scele", visibleRows: Int = TaskRule.visibleRows) -> TasksPageModel {
        var model = makeContent(tasks: allTasks, unviewed: unviewed, signedIn: signedIn, checking: checking, problem: problem, asOf: asOf, now: now,
                                offset: offset, calendar: calendar, showOverdue: showOverdue, site: site, visibleRows: max(1, visibleRows))
        model.siteName = site
        return model
    }

    private static func makeContent(tasks allTasks: [MoodleTask], unviewed: Set<Int>, signedIn: Bool, checking: Bool, problem: MoodleProblem?,
                                    asOf: Double?, now: Double, offset: Int, calendar: Calendar, showOverdue: Bool, site: String, visibleRows: Int) -> TasksPageModel {
        func clock(_ time: Double) -> String {
            let parts = calendar.dateComponents([.hour, .minute], from: Date(timeIntervalSince1970: time))
            return String(format: "%02d:%02d", parts.hour ?? 0, parts.minute ?? 0)
        }
        guard signedIn else {
            return TasksPageModel(content: .message(title: "See your \(site) tasks here", detail: "Sign in once in Settings. Your password isn't stored.", button: "Sign in…", allClear: false))
        }
        if problem == .signedOut {
            return TasksPageModel(content: .message(title: "Signed out of \(site)", detail: "The key was refused. Sign in again in Settings.", button: "Sign in…", allClear: false))
        }
        guard let asOf else {
            let title = problem == nil || checking ? "Checking \(site)…" : problem == .maintenance ? "\(site) is under maintenance" : "Couldn't reach \(site)"
            return TasksPageModel(content: .message(title: title, detail: checking ? "" : "Noitch will try again soon.", button: nil, allClear: false))
        }
        let weekday = calendar.component(.weekday, from: Date(timeIntervalSince1970: now))
        let overdue = allTasks.filter { $0.due < now }.count
        let tasks = showOverdue ? allTasks : allTasks.filter { $0.due >= now }
        let reveal = "Show \(overdue) overdue"
        guard let first = tasks.first else {
            let ahead = (overdue > 0 ? "Nothing else due" : "Nothing to do") + (problem == nil ? "" : " as of \(clock(asOf))")
            var model = TasksPageModel(content: .message(title: "All clear", detail: "\(ahead) · checked \(clock(asOf))",
                                                         button: overdue > 0 ? reveal : nil, allClear: true))
            model.buttonAction = .showOverdue
            return model
        }
        let stale = problem != nil ? " · as of \(clock(asOf))" : ""
        let hero = Hero(
            label: (first.due < now ? "Overdue" : "Next due") + stale,
            countdown: TaskRule.countdown(first.due, now: now).map { Piece(value: $0.value, unit: $0.unit) },
            title: first.title,
            course: first.course,
            due: TaskRule.fullDue(first.due, calendar: calendar),
            severity: TaskRule.urgency(due: first.due, now: now),
            url: first.url
        )
        // The week ahead counts what can still be done, whether or not overdue work is shown.
        func loadDay(_ task: MoodleTask) -> Int? {
            let offset = TaskRule.dayOffset(task.due, now: now, calendar: calendar)
            return task.due >= now && (0..<TaskRule.loadDays).contains(offset) ? offset : nil
        }
        var counts = Array(repeating: 0, count: TaskRule.loadDays)
        var urgent = Array(repeating: false, count: TaskRule.loadDays)
        var fresh = Array(repeating: false, count: TaskRule.loadDays)
        for task in allTasks {
            guard let day = loadDay(task) else { continue }
            counts[day] += 1
            urgent[day] = urgent[day] || TaskRule.urgency(due: task.due, now: now) == .critical
            fresh[day] = fresh[day] || unviewed.contains(task.id)
        }
        let busiest = counts.max() ?? 0
        let busiestDay = counts.firstIndex(of: busiest) ?? 0
        let load = (0..<TaskRule.loadDays).map { day in
            DayLoad(letter: String(TaskRule.weekdays[(weekday - 1 + day) % 7].prefix(1)), count: counts[day], hasUrgent: urgent[day],
                    hasNew: fresh[day], isBusiest: busiest > 0 && day == busiestDay)
        }
        let total = counts.reduce(0, +)
        let busiestName = busiestDay == 0 ? "today" : busiestDay == 1 ? "tomorrow" : TaskRule.weekdays[(weekday - 1 + busiestDay) % 7]
        let caption = total == 0 ? ["Nothing due", "in the next 7 days"] : ["Busiest: \(busiestName) · \(busiest)", "\(total) due in 7 days"]
        let start = min(max(offset, 0), max(tasks.count - visibleRows, 0))
        let rows = tasks[start...].prefix(visibleRows).map { task in
            Row(id: task.id, title: task.title, course: task.course, due: TaskRule.dueText(task.due, now: now, calendar: calendar),
                severity: TaskRule.urgency(due: task.due, now: now), module: task.module, isNew: unviewed.contains(task.id), url: task.url,
                day: loadDay(task))
        }
        let more: String
        if tasks.count <= visibleRows {
            more = ""
        } else if start == 0 {
            more = "+\(tasks.count - visibleRows) more · scroll"
        } else {
            more = "\(start + 1)–\(start + rows.count) of \(tasks.count)"
        }
        var model = TasksPageModel(content: .list(hero: hero, load: load, rows: Array(rows), more: more))
        model.loadCaption = caption
        model.overdueToggle = overdue == 0 ? "" : showOverdue ? "Hide overdue" : reveal
        model.listCount = tasks.count
        return model
    }
}

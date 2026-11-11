import AppKit

/// Owns what is known about the Moodle tasks, whether or not the island is open: the list, which
/// tasks are new, and when to ask again. Signed out, it holds nothing and schedules nothing.
///
/// Checks happen on these events only: the island opening or the page showing (at most every 10
/// minutes, or 2 for the page), the Mac waking, and one one-shot timer per check interval, re-armed
/// after each check. Reminders are one one-shot timer at the next reminder time, with no request.
final class TaskWatch {
    /// The list, the problem or the "checking" state changed.
    var onChange: (() -> Void)?
    /// Tasks seen for the first time, to announce on the closed island.
    var onNew: (([MoodleTask]) -> Void)?
    /// A deadline is a reminder's lead away.
    var onReminder: ((MoodleTask, Double) -> Void)?
    /// Whether any announced task hasn't been looked at on the page yet.
    var onUnviewedChange: (() -> Void)?

    private static let openThrottle: Double = 600
    private static let pageThrottle: Double = 120
    /// The first check after launch waits a little, so launching costs no more than before.
    private static let launchDelay: Double = 3
    private static let cacheFile = FileManager.default
        .urls(for: .cachesDirectory, in: .userDomainMask).first?
        .appendingPathComponent("Noitch/tasks", isDirectory: false)

    private(set) var settings: TasksSettings
    /// Off while the tasks page is turned off in settings: nothing is checked or announced.
    private var pageOn: Bool
    private var running: Bool { settings.isSignedIn && pageOn }
    private(set) var record: TaskCacheRecord?
    private(set) var problem: MoodleProblem?
    private(set) var isChecking = false
    private var lastCheck: Double?
    private var checkTask: Task<Void, Never>?
    private var checkTimer: Timer?
    private var reminderTimer: Timer?
    private var reminded: Set<String> = []
    private var wakeObserver: NSObjectProtocol?

    init(settings: TasksSettings, pageOn: Bool) {
        self.settings = settings
        self.pageOn = pageOn
        wakeObserver = NSWorkspace.shared.notificationCenter.addObserver(forName: NSWorkspace.didWakeNotification, object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.checkIfOlder(than: Self.openThrottle) }
        }
        start()
    }

    /// The tasks the page and alerts use, soonest first.
    var tasks: [MoodleTask] {
        TaskRule.visible(record?.tasks ?? [], scope: settings.scope, now: Date().timeIntervalSince1970)
    }

    var unviewed: Set<Int> { record?.unviewed ?? [] }

    func apply(_ settings: TasksSettings, pageOn: Bool) {
        let old = self.settings
        let wasOn = self.pageOn
        self.settings = settings
        self.pageOn = pageOn
        if settings.site != old.site || settings.accountName != old.accountName || pageOn != wasOn {
            stop(forget: !settings.isSignedIn || settings.site != old.site)
            start()
            onChange?()
            onUnviewedChange?()
            return
        }
        if settings.checkMinutes != old.checkMinutes { scheduleCheck() }
        if settings.scope != old.scope || settings.reminders != old.reminders {
            scheduleReminder()
            onChange?()
        }
    }

    func islandWillOpen() { checkIfOlder(than: Self.openThrottle) }

    func pageWillShow() { checkIfOlder(than: Self.pageThrottle) }

    /// The page has been looked at, so nothing is new any more.
    func markViewed() {
        guard var record, !record.unviewed.isEmpty else { return }
        record.unviewed = []
        self.record = record
        save()
        onUnviewedChange?()
    }

    private func start() {
        guard running else { return }
        record = Self.cacheFile.flatMap { try? String(contentsOf: $0, encoding: .utf8) }.flatMap(TaskCacheRecord.decode)
        lastCheck = record?.asOf
        let now = Date().timeIntervalSince1970
        let interval = settings.checkMinutes * 60
        if interval > 0, (lastCheck.map { now - $0 >= interval } ?? true) {
            arm(after: Self.launchDelay)
        } else {
            scheduleCheck()
        }
        scheduleReminder()
    }

    private func stop(forget: Bool) {
        checkTask?.cancel()
        checkTask = nil
        isChecking = false
        checkTimer?.invalidate()
        checkTimer = nil
        reminderTimer?.invalidate()
        reminderTimer = nil
        record = nil
        problem = nil
        lastCheck = nil
        reminded = []
        if forget, let file = Self.cacheFile { try? FileManager.default.removeItem(at: file) }
    }

    private func checkIfOlder(than seconds: Double) {
        guard running, !isChecking else { return }
        let now = Date().timeIntervalSince1970
        if let lastCheck, now - lastCheck < seconds { return }
        check()
    }

    private func check() {
        guard running, !isChecking else { return }
        checkTimer?.invalidate()
        checkTimer = nil
        isChecking = true
        let now = Date().timeIntervalSince1970
        lastCheck = now
        onChange?()
        let site = settings.site
        checkTask = Task { [weak self] in
            let result = await MoodleSource.tasks(site: site, now: now)
            guard !Task.isCancelled, let self else { return }
            finish(result)
        }
    }

    private func finish(_ result: Result<[MoodleTask], MoodleProblem>) {
        checkTask = nil
        isChecking = false
        let now = Date().timeIntervalSince1970
        switch result {
        case let .success(tasks):
            problem = nil
            let ids = Set(tasks.map(\.id))
            if var record {
                let fresh = TaskRule.newTasks(TaskRule.visible(tasks, scope: settings.scope, now: now), seen: record.seen, now: now)
                // Ids that left the list are forgotten, so the file stays as small as the list.
                record.seen = record.seen.intersection(ids).union(fresh.map(\.id))
                record.unviewed = record.unviewed.intersection(ids).union(fresh.map(\.id))
                record.tasks = tasks
                record.asOf = now
                self.record = record
                save()
                if !fresh.isEmpty {
                    onNew?(fresh)
                    onUnviewedChange?()
                }
            } else {
                // The first list after signing in is what was already there: nothing to announce.
                record = TaskCacheRecord(asOf: now, tasks: tasks, seen: ids)
                save()
            }
        case let .failure(failure):
            problem = failure
        }
        // A refused key won't start working by itself; signing in again rearms this.
        if problem != .signedOut { scheduleCheck() }
        scheduleReminder()
        onChange?()
    }

    private func save() {
        guard let record, let file = Self.cacheFile else { return }
        try? FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
        try? record.encode().write(to: file, atomically: true, encoding: .utf8)
    }

    // MARK: Timers

    /// One shot at the end of the interval since the last check. "Only when I look" arms nothing.
    private func scheduleCheck() {
        checkTimer?.invalidate()
        checkTimer = nil
        let interval = settings.checkMinutes * 60
        guard running, interval > 0, !isChecking else { return }
        let now = Date().timeIntervalSince1970
        arm(after: max((lastCheck ?? now) + interval - now, 1))
    }

    private func arm(after delay: Double) {
        checkTimer?.invalidate()
        let timer = Timer(timeInterval: delay, repeats: false) { [unowned self] _ in
            MainActor.assumeIsolated {
                checkTimer = nil
                check()
            }
        }
        // Lets the system fold this wakeup into others; a few minutes late is fine for this.
        timer.tolerance = min(300, delay * 0.2)
        RunLoop.main.add(timer, forMode: .common)
        checkTimer = timer
    }

    private func scheduleReminder() {
        reminderTimer?.invalidate()
        reminderTimer = nil
        let now = Date().timeIntervalSince1970
        guard running,
              let next = TaskRule.nextReminder(tasks, leads: settings.reminders.leads, reminded: reminded, now: now) else { return }
        let timer = Timer(timeInterval: max(next - now, 0.5), repeats: false) { [unowned self] _ in
            MainActor.assumeIsolated { remind() }
        }
        timer.tolerance = 20
        RunLoop.main.add(timer, forMode: .common)
        reminderTimer = timer
    }

    private func remind() {
        reminderTimer = nil
        let now = Date().timeIntervalSince1970
        let due = TaskRule.dueReminders(tasks, leads: settings.reminders.leads, reminded: reminded, now: now + 30)
        for pair in due { reminded.insert(TaskRule.reminderKey(pair.task, lead: pair.lead)) }
        // Reminders that fell while asleep are marked too, so they never fire late.
        for task in tasks {
            for lead in settings.reminders.leads where task.due - lead <= now { reminded.insert(TaskRule.reminderKey(task, lead: lead)) }
        }
        if let first = due.first { onReminder?(first.task, first.lead) }
        scheduleReminder()
    }

    #if DEBUG
    /// `--demo-tasks`: a sample list, no account and no network, for checking the page and alert.
    func loadDemo() {
        let now = Date().timeIntervalSince1970
        settings.accountName = "Demo"
        record = TaskCacheRecord(asOf: now, tasks: [
            MoodleTask(id: 1, title: "Tugas 3: REST API dengan Django", course: "PBP", module: .assignment, due: now + 26 * 3600, url: "https://scele.cs.ui.ac.id"),
            MoodleTask(id: 2, title: "Kuis 4: Graph Traversal", course: "SDA", module: .quiz, due: now + 2.5 * 86400, url: "https://scele.cs.ui.ac.id"),
            MoodleTask(id: 3, title: "Laporan Sprint 2", course: "PPL", module: .assignment, due: now + 3.2 * 86400, url: "https://scele.cs.ui.ac.id"),
            MoodleTask(id: 4, title: "Tugas Individu 2", course: "MPPI", module: .assignment, due: now + 6 * 86400, url: "https://scele.cs.ui.ac.id"),
            MoodleTask(id: 5, title: "Lab 7: Concurrency", course: "SisOp", module: .assignment, due: now + 9.1 * 86400, url: "https://scele.cs.ui.ac.id"),
            MoodleTask(id: 6, title: "Kuis 5", course: "SDA", module: .quiz, due: now + 9.3 * 86400, url: "https://scele.cs.ui.ac.id"),
            MoodleTask(id: 8, title: "Tugas 2: Wireframe", course: "PBP", module: .assignment, due: now - 2 * 86400, url: "https://scele.cs.ui.ac.id"),
        ], seen: [1, 2, 3, 4, 5, 6, 8], unviewed: [3])
        // Far in the future, so opening never asks the network: there is no key.
        lastCheck = now + 1e9
        onChange?()
        onUnviewedChange?()
        // A new task arrives a few seconds after launch, to show the alert.
        Timer.scheduledTimer(withTimeInterval: 4, repeats: false) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self, var record = self.record else { return }
                let task = MoodleTask(id: 7, title: "Tugas 4: Deployment", course: "PBP", module: .assignment, due: now + 4 * 86400, url: "https://scele.cs.ui.ac.id")
                record.tasks.append(task)
                record.seen.insert(7)
                record.unviewed.insert(7)
                self.record = record
                self.onChange?()
                self.onNew?([task])
                self.onUnviewedChange?()
            }
        }
    }
    #endif
}

import CoreGraphics
import Foundation
import Testing
@testable import Noitch

struct TasksTests {
    private static func data(_ text: String) -> Data { Data(text.utf8) }

    /// Jakarta, as Scele's users are: day boundaries and times are local.
    private static let calendar: Calendar = {
        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = TimeZone(identifier: "Asia/Jakarta")!
        return calendar
    }()

    /// Thu 12 Nov 2026, 21:00 in Jakarta.
    private static let now: Double = 1_794_492_000

    private static func task(_ id: Int, due: Double, module: TaskModule = .assignment) -> MoodleTask {
        MoodleTask(id: id, title: "Task \(id)", course: "PBP", module: module, due: due, url: "https://scele.cs.ui.ac.id/mod/assign/view.php?id=\(id)")
    }

    @Test func `taller layouts show more tasks and keep the footer inside`() {
        let compact = TasksLayout(size: CGSize(width: 580, height: 180), headerHeight: 38)
        let tall = TasksLayout(size: CGSize(width: 800, height: 300), headerHeight: 38)
        #expect(compact.visibleRows == 3)
        #expect(tall.visibleRows > compact.visibleRows)
        #expect(tall.more.maxY <= tall.size.height - 8)
        let tasks = (1...12).map { Self.task($0, due: Self.now + Double($0) * 3600) }
        let model = TasksPageModel.make(tasks: tasks, unviewed: [], signedIn: true, checking: false, problem: nil,
                                        asOf: Self.now, now: Self.now, offset: 99, calendar: Self.calendar, visibleRows: tall.visibleRows)
        guard case let .list(_, _, rows, _) = model.content else { Issue.record("Expected task list"); return }
        #expect(rows.count == tall.visibleRows)
        #expect(rows.last?.id == 12)
    }

    // MARK: Format

    @Test func `a token answer gives the token`() {
        #expect(MoodleFormat.token(from: Self.data(#"{"token":"abc123","privatetoken":null}"#)) == .success("abc123"))
    }

    @Test func `token errors say what went wrong`() {
        #expect(MoodleFormat.token(from: Self.data(#"{"error":"Invalid login, please try again","errorcode":"invalidlogin","stacktrace":null}"#)) == .failure(.wrongLogin))
        #expect(MoodleFormat.token(from: Self.data(#"{"error":"Web services must be enabled","errorcode":"enablewsdescription"}"#)) == .failure(.noToken("Web services must be enabled")))
        #expect(MoodleFormat.token(from: Self.data("<html>")) == .failure(.unavailable))
    }

    @Test func `site info gives the name and allowed functions`() throws {
        let json = #"{"sitename":"SCELE Fasilkom UI","username":"x","fullname":"Ana Putri","userid":7,"release":"3.9.2+","functions":[{"name":"core_calendar_get_action_events_by_timesort","version":"2020061500"},{"name":"core_webservice_get_site_info","version":"2020061500"}]}"#
        let info = try MoodleFormat.siteInfo(from: Self.data(json)).get()
        #expect(info.userName == "Ana Putri")
        #expect(info.siteName == "SCELE Fasilkom UI")
        #expect(info.functions.contains("core_calendar_get_action_events_by_timesort"))
    }

    @Test func `an invalid token reads as signed out`() {
        let json = #"{"exception":"moodle_exception","errorcode":"invalidtoken","message":"Invalid token - token not found"}"#
        #expect(MoodleFormat.tasks(from: Self.data(json)) == .failure(.signedOut))
        #expect(MoodleFormat.siteInfo(from: Self.data(json)) == .failure(.signedOut))
    }

    @Test func `maintenance is told apart`() {
        let json = #"{"exception":"moodle_exception","errorcode":"sitemaintenance","message":"Site is under maintenance"}"#
        #expect(MoodleFormat.tasks(from: Self.data(json)) == .failure(.maintenance))
    }

    @Test func `events from Moodle 3 take their title from the event name`() throws {
        let json = #"""
        {"events":[{"id":101,"name":"Tugas 3: REST API &amp; Django is due","description":"<p>x</p>","component":"mod_assign","modulename":"assign","instance":5,"eventtype":"due","timestart":1794589500,"timesort":1794589500,"course":{"id":3,"fullname":"CSGE602022 - Pemrograman Berbasis Platform (A)","shortname":"CSGE602022-PBP-A-2026"},"action":{"name":"Add submission","url":"https://scele.cs.ui.ac.id/mod/assign/view.php?id=55&action=editsubmission","itemcount":1,"actionable":true},"url":"https://scele.cs.ui.ac.id/mod/assign/view.php?id=55"},
        {"id":"102","name":"Kuis 4 closes","modulename":"quiz","timesort":"1794650000","course":{"shortname":"SDA","fullname":"Struktur Data dan Algoritma"},"url":"https://scele.cs.ui.ac.id/mod/quiz/view.php?id=56"},
        {"name":"no id"}],"firstid":101,"lastid":102}
        """#
        let tasks = try MoodleFormat.tasks(from: Self.data(json)).get()
        #expect(tasks.count == 2)
        #expect(tasks[0] == MoodleTask(id: 101, title: "Tugas 3: REST API & Django", course: "PBP", module: .assignment, due: 1_794_589_500,
                                       url: "https://scele.cs.ui.ac.id/mod/assign/view.php?id=55"))
        #expect(tasks[1].title == "Kuis 4")
        #expect(tasks[1].course == "SDA")
        #expect(tasks[1].module == .quiz)
        #expect(tasks[1].id == 102)
    }

    @Test func `events from Moodle 4 use the activity name`() throws {
        let json = #"{"events":[{"id":9,"name":"Laporan Sprint 2 is due","activityname":"Laporan Sprint 2","modulename":"assign","timesort":1794700000,"course":{"shortname":"PPL","fullname":"Proyek Perangkat Lunak"},"url":"u"}]}"#
        #expect(try MoodleFormat.tasks(from: Self.data(json)).get().first?.title == "Laporan Sprint 2")
    }

    // MARK: Text

    @Test func `course labels stay short`() {
        #expect(TaskRule.courseLabel(shortName: "PBP-A", fullName: "Pemrograman Berbasis Platform") == "PBP-A")
        #expect(TaskRule.courseLabel(shortName: "CSGE602022-PBP-A-2026", fullName: "CSGE602022 - Pemrograman Berbasis Platform (A)") == "PBP")
        #expect(TaskRule.courseLabel(shortName: "StrukturDataDanAlgoritma", fullName: "Struktur Data dan Algoritma") == "SDA")
        #expect(TaskRule.courseLabel(shortName: "LongCourseCode2026", fullName: "Kalkulus") == "LongCourse")
    }

    @Test func `names lose tags and entities`() {
        #expect(TaskRule.plainText("<span lang=\"en\">Quiz</span> &amp;  more") == "Quiz & more")
    }

    // MARK: The list

    @Test func `the list is soonest first, without other kinds or old work`() {
        let tasks = [
            Self.task(3, due: Self.now + 3 * 86400),
            Self.task(1, due: Self.now + 3600),
            Self.task(2, due: Self.now + 7200, module: .forum),
            Self.task(4, due: Self.now - 8 * 86400),
            Self.task(5, due: Self.now - 86400),
        ]
        #expect(TaskRule.visible(tasks, scope: .assignmentsAndQuizzes, now: Self.now).map(\.id) == [5, 1, 3])
        #expect(TaskRule.visible(tasks, scope: .everything, now: Self.now).map(\.id) == [5, 1, 2, 3])
    }

    @Test func `only unseen tasks still ahead are new`() {
        let tasks = [Self.task(1, due: Self.now + 60), Self.task(2, due: Self.now + 60), Self.task(3, due: Self.now - 60)]
        #expect(TaskRule.newTasks(tasks, seen: [1], now: Self.now).map(\.id) == [2])
    }

    @Test func `urgency rises as the deadline nears`() {
        #expect(TaskRule.urgency(due: Self.now + 4 * 86400, now: Self.now) == .calm)
        #expect(TaskRule.urgency(due: Self.now + 2 * 86400, now: Self.now) == .watch)
        #expect(TaskRule.urgency(due: Self.now + 3600, now: Self.now) == .critical)
        #expect(TaskRule.urgency(due: Self.now - 3600, now: Self.now) == .critical)
    }

    // MARK: Time

    @Test func `due times read as people say them`() {
        let calendar = Self.calendar
        #expect(TaskRule.dueText(Self.now + 3600, now: Self.now, calendar: calendar) == "Today 22:00")
        #expect(TaskRule.dueText(Self.now + 3 * 3600, now: Self.now, calendar: calendar) == "Tomorrow 00:00")
        #expect(TaskRule.dueText(Self.now + 2 * 86400, now: Self.now, calendar: calendar) == "Sat 21:00")
        #expect(TaskRule.dueText(Self.now + 10 * 86400, now: Self.now, calendar: calendar) == "22 Nov")
        #expect(TaskRule.dueText(Self.now - 2 * 86400 - 60, now: Self.now, calendar: calendar) == "Overdue 2d")
        #expect(TaskRule.fullDue(Self.now + 86400 + 10500, calendar: calendar) == "Fri 13 Nov, 23:55")
    }

    @Test func `the countdown keeps its two largest units`() {
        func text(_ seconds: Double) -> String {
            TaskRule.countdown(Self.now + seconds, now: Self.now).map { "\($0.value)\($0.unit)" }.joined(separator: " ")
        }
        #expect(text(86400 + 2 * 3600 + 300) == "1d 2h")
        #expect(text(5 * 3600 + 20 * 60) == "5h 20m")
        #expect(text(2 * 86400 + 59) == "2d")
        #expect(text(45 * 60) == "45m")
        #expect(text(20) == "0m")
    }

    @Test func `days count from today in local time`() {
        #expect(TaskRule.dayOffset(Self.now + 3600, now: Self.now, calendar: Self.calendar) == 0)
        #expect(TaskRule.dayOffset(Self.now + 3 * 3600, now: Self.now, calendar: Self.calendar) == 1)
        #expect(TaskRule.dayOffset(Self.now - 22 * 3600, now: Self.now, calendar: Self.calendar) == -1)
    }

    // MARK: Reminders

    @Test func `reminders fall a day and an hour before, once each`() {
        let tasks = [Self.task(1, due: Self.now + 2 * 86400), Self.task(2, due: Self.now + 2 * 3600)]
        let leads = [86400.0, 3600]
        #expect(TaskRule.nextReminder(tasks, leads: leads, reminded: [], now: Self.now) == Self.now + 3600)
        #expect(TaskRule.nextReminder(tasks, leads: leads, reminded: ["2@3600"], now: Self.now) == Self.now + 86400)
        #expect(TaskRule.nextReminder(tasks, leads: [], reminded: [], now: Self.now) == nil)
        let due = TaskRule.dueReminders(tasks, leads: leads, reminded: [], now: Self.now + 3600)
        #expect(due.map { TaskRule.reminderKey($0.task, lead: $0.lead) } == ["2@3600"])
        // Slept through it: an hour late is too late.
        #expect(TaskRule.dueReminders(tasks, leads: leads, reminded: [], now: Self.now + 7100).isEmpty)
    }

    // MARK: Cache

    @Test func `the cache keeps the list and what was seen`() {
        var tab = Self.task(1, due: 1_794_589_500)
        tab.title = "With\ta tab"
        let record = TaskCacheRecord(asOf: 1_794_492_000, tasks: [tab, Self.task(2, due: 1_794_600_000, module: .quiz)], seen: [1, 2, 9], unviewed: [2])
        let decoded = TaskCacheRecord.decode(record.encode())
        #expect(decoded?.seen == [1, 2, 9])
        #expect(decoded?.unviewed == [2])
        #expect(decoded?.asOf == 1_794_492_000)
        #expect(decoded?.tasks.count == 2)
        #expect(decoded?.tasks[0].title == "With a tab")
        #expect(decoded?.tasks[1].module == .quiz)
        #expect(TaskCacheRecord.decode("nonsense") == nil)
    }

    // MARK: Layout

    @Test func `the alert slip has the music's shape, and the dot adds narrow wings`() {
        let display = IslandGeometry.Display(frame: CGRect(x: 0, y: 0, width: 1512, height: 982), notchHeight: 32, notchWidth: 185, menuBarHeight: 32)
        let live = LiveLayout(collapsed: CGSize(width: 185, height: 32))
        var geometry = IslandGeometry(display: display)
        geometry.taskAlert = .slip
        #expect(geometry.pillSize(for: .collapsed) == live.size(withLyric: true))
        geometry.isLive = true
        #expect(geometry.pillSize(for: .collapsed) == live.size(withLyric: true))
        geometry.taskAlert = .dot
        #expect(geometry.pillSize(for: .collapsed) == CGSize(width: live.size.width + 28, height: 32))
        geometry.isLive = false
        #expect(geometry.pillSize(for: .collapsed) == CGSize(width: 185 + 28, height: 32))
        #expect(geometry.pillSize(for: .expanded) == IslandGeometry.expandedSize)
    }

    @Test func `the dot sits in its own wing, with or without music`() {
        let layout = TaskAlertLayout(collapsed: CGSize(width: 185, height: 32))
        let pillRight = layout.size.width / 2 + 185 / 2 + TaskAlertLayout.dotWing
        let alone = layout.dot(beside: false)
        #expect(alone.minX > layout.size.width / 2 + 185 / 2)
        #expect(alone.maxX < pillRight)
        let withMusic = layout.dot(beside: true)
        #expect(withMusic.minX > layout.size.width / 2 + layout.live.size.width / 2)
        #expect(withMusic.maxX < layout.size.width)
    }

    @Test func `the tasks page fits its body`() {
        let layout = TasksLayout(size: IslandGeometry.expandedSize, headerHeight: 38)
        #expect(layout.more.maxY <= layout.size.height - 4)
        #expect(layout.openButton.maxY <= layout.size.height - 4)
        #expect(layout.row(0).minY >= layout.load.maxY)
        #expect(layout.barLetter(0).maxY <= layout.row(0).minY)
        #expect(layout.bar(TaskRule.loadDays - 1, share: 1).maxX < layout.loadCaption.minX)
        // The all-clear tick stays below the header band, clear of the notch.
        #expect(layout.check.minY >= 38)
        #expect(layout.message(withCheck: true).minY >= layout.check.maxY)
        #expect(layout.messageButton(withCheck: true).maxY <= layout.size.height - 8)
        #expect(layout.messageDetail(withCheck: true).maxY <= layout.messageButton(withCheck: true).minY)
        #expect(layout.rowDue(0).maxX <= layout.size.width - TasksLayout.sidePadding)
        // Clear of the shared header's tabs.
        #expect(layout.bodyTop >= 38)
    }

    // MARK: Page

    @Test func `the page says what to do when there is nothing to list`() {
        func make(signedIn: Bool = true, checking: Bool = false, problem: MoodleProblem? = nil, asOf: Double? = nil, tasks: [MoodleTask] = []) -> TasksPageModel.Content {
            TasksPageModel.make(tasks: tasks, unviewed: [], signedIn: signedIn, checking: checking, problem: problem, asOf: asOf,
                                now: Self.now, offset: 0, calendar: Self.calendar).content
        }
        #expect(make(signedIn: false) == .message(title: "See your Scele tasks here", detail: "Sign in once in Settings. Your password isn't stored.", button: "Sign in…", allClear: false))
        if case let .message(title, _, button, _) = make(problem: .signedOut, asOf: Self.now) {
            #expect(title == "Signed out of Scele")
            #expect(button == "Sign in…")
        } else {
            Issue.record("expected a message")
        }
        if case let .message(title, _, _, _) = make(checking: true) { #expect(title == "Checking Scele…") }
        if case let .message(title, _, _, _) = make(problem: .maintenance) { #expect(title == "Scele is under maintenance") }
        if case let .message(title, detail, _, allClear) = make(asOf: Self.now - 60) {
            #expect(title == "All clear")
            #expect(detail == "Nothing to do · checked 20:59")
            #expect(allClear)
        }
    }

    @Test func `the list leads with the next task and scrolls three at a time`() {
        let tasks = (1...5).map { Self.task($0, due: Self.now + Double($0) * 3 * 3600) }
        let model = TasksPageModel.make(tasks: tasks, unviewed: [2], signedIn: true, checking: false, problem: nil, asOf: Self.now,
                                        now: Self.now, offset: 0, calendar: Self.calendar)
        guard case let .list(hero, load, rows, more) = model.content else {
            Issue.record("expected a list")
            return
        }
        #expect(hero.label == "Next due")
        #expect(hero.countdown == [.init(value: 3, unit: "h")])
        #expect(hero.severity == .critical)
        // Due at 00:00, 03:00, 06:00, 09:00 and 12:00 on the 13th, all tomorrow.
        #expect(load.map(\.count) == [0, 5, 0, 0, 0, 0, 0])
        #expect(load.map(\.letter).prefix(3) == ["T", "F", "S"])
        #expect(load[1].isBusiest && load[1].hasNew && load[1].hasUrgent)
        #expect(model.loadCaption == ["Busiest: tomorrow · 5", "5 due in 7 days"])
        #expect(rows.map(\.id) == [1, 2, 3])
        #expect(rows[0].day == 1)
        #expect(more == "+2 more · scroll")
        let scrolled = TasksPageModel.make(tasks: tasks, unviewed: [], signedIn: true, checking: false, problem: .unavailable, asOf: Self.now - 600,
                                           now: Self.now, offset: 9, calendar: Self.calendar)
        guard case let .list(staleHero, _, scrolledRows, scrolledMore) = scrolled.content else { return }
        #expect(scrolledRows.map(\.id) == [3, 4, 5])
        #expect(scrolledMore == "3–5 of 5")
        #expect(staleHero.label == "Next due · as of 20:50")
    }

    @Test func `overdue work stays hidden until asked for`() {
        let tasks = [Self.task(1, due: Self.now - 2 * 86400), Self.task(2, due: Self.now - 3600), Self.task(3, due: Self.now + 86400 * 2)]
        let hidden = TasksPageModel.make(tasks: tasks, unviewed: [], signedIn: true, checking: false, problem: nil, asOf: Self.now,
                                         now: Self.now, offset: 0, calendar: Self.calendar)
        guard case let .list(hero, _, rows, _) = hidden.content else {
            Issue.record("expected a list")
            return
        }
        #expect(rows.map(\.id) == [3])
        #expect(hero.label == "Next due")
        #expect(hidden.overdueToggle == "Show 2 overdue")
        #expect(hidden.listCount == 1)
        let shown = TasksPageModel.make(tasks: tasks, unviewed: [], signedIn: true, checking: false, problem: nil, asOf: Self.now,
                                        now: Self.now, offset: 0, calendar: Self.calendar, showOverdue: true)
        guard case let .list(overdueHero, _, allRows, _) = shown.content else { return }
        #expect(allRows.map(\.id) == [1, 2, 3])
        #expect(overdueHero.label == "Overdue")
        #expect(shown.overdueToggle == "Hide overdue")
        // Only overdue work left: all clear, with the button to see it.
        let onlyOverdue = TasksPageModel.make(tasks: Array(tasks.prefix(2)), unviewed: [], signedIn: true, checking: false, problem: nil,
                                              asOf: Self.now, now: Self.now, offset: 0, calendar: Self.calendar)
        #expect(onlyOverdue.content == .message(title: "All clear", detail: "Nothing else due · checked 21:00", button: "Show 2 overdue", allClear: true))
        #expect(onlyOverdue.buttonAction == .showOverdue)
        // Nothing overdue: no toggle.
        let none = TasksPageModel.make(tasks: [tasks[2]], unviewed: [], signedIn: true, checking: false, problem: nil, asOf: Self.now,
                                       now: Self.now, offset: 0, calendar: Self.calendar)
        #expect(none.overdueToggle.isEmpty)
    }
}

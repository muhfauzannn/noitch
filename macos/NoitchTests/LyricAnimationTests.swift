import QuartzCore
import Testing
@testable import Noitch

struct LyricAnimationTests {
    private func descendants(_ layer: CALayer) -> [CALayer] {
        (layer.sublayers ?? []).flatMap { [$0] + descendants($0) }
    }

    @Test func readingsTravelWithTheirOriginalLines() throws {
        let scene = HomeScene(layout: HomeLayout(size: IslandGeometry.expandedSize, headerHeight: 38), scale: 2)
        var model = HomeModel()
        model.isOnStage = true
        model.lyricIndex = 0
        model.stageLines = ["", "first", "second", "third"]
        model.stageRomanizations = ["", "first reading", "second reading", "third reading"]
        scene.apply(model)
        let textLayers = descendants(scene.root).compactMap { $0 as? CATextLayer }
        let first = try #require(textLayers.first { $0.string as? String == "first reading" })
        let second = try #require(textLayers.first { $0.string as? String == "second reading" })
        let firstOriginal = try #require(first.superlayer as? CATextLayer)
        let secondOriginal = try #require(second.superlayer as? CATextLayer)
        #expect(firstOriginal.string as? String == "first")
        #expect(secondOriginal.string as? String == "second")
        let previousPosition = firstOriginal.position

        model.lyricIndex = 1
        model.stageLines = ["first", "second", "third", "fourth"]
        model.stageRomanizations = ["first reading", "second reading", "third reading", "fourth reading"]
        scene.apply(model)
        let active = try #require(descendants(scene.root).compactMap { $0 as? CATextLayer }.first { $0.string as? String == "second reading" })
        #expect((active.superlayer as? CATextLayer)?.string as? String == "second")
        #expect(active.opacity == 0.7)
        if !Motion.reduceMotion {
            #expect(first.superlayer === firstOriginal)
            #expect(second.superlayer === secondOriginal)
            #expect(first.string as? String == "first reading")
            #expect(second.string as? String == "second reading")
            #expect(firstOriginal.position.y < previousPosition.y)
            #expect(first.opacity == 0)
            #expect(firstOriginal.animation(forKey: "position") != nil)
            #expect(secondOriginal.animation(forKey: "position") != nil)
            #expect(second.animation(forKey: "opacity")?.duration == secondOriginal.animation(forKey: "position")?.duration)
        }

        // Seeking replaces whole pairs; a Latin-only current line must not inherit the old reading.
        model.lyricIndex = 9
        model.stageLines = ["before", "Latin only", "after", ""]
        model.stageRomanizations = ["", "", "later reading", ""]
        scene.apply(model)
        let current = try #require(descendants(scene.root).compactMap { $0 as? CATextLayer }.first { $0.string as? String == "Latin only" })
        #expect((current.sublayers?.first as? CATextLayer)?.string as? String == "")
    }
}

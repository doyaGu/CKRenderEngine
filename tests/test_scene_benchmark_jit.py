"""Checks reporting boundaries without a GPU or timing-sensitive assertions."""
import copy
import unittest

from scene_benchmark_jit import aggregate, distribution, mode_order, paired_foreground, summarize_profile


class SceneBenchmarkTest(unittest.TestCase):
    def test_mode_order_balances_positions_and_predecessors_in_six_runs(self):
        orders = [mode_order(i) for i in range(6)]
        self.assertEqual(orders[0][0], "off")  # Establish the image reference first.
        self.assertEqual(len(set(orders)), 6)
        for mode in ("off", "fragment", "on"):
            for position in range(3):
                self.assertEqual(sum(order[position] == mode for order in orders), 2)
            for other in ("off", "fragment", "on"):
                if other != mode:
                    self.assertEqual(sum((mode, other) in tuple(zip(order, order[1:]))
                                         for order in orders), 2)
        self.assertEqual(mode_order(6), orders[0])

    def setUp(self):
        self.profile = dict(warmupFrames=2, presentEveryFrame=True, waitVBlankRequested=False,
            requestedWidth=640, requestedHeight=480, allMeasuredFramesFocused=True,
            coldStartMilliseconds=dict(firstRender=10), timingScope="Render", driverName="test",
            driverDescription="test", frames=[dict(index=i, warmup=i < 2, width=640, height=480,
                renderMilliseconds=duration, rasterizerStats=dict(drawCalls=10, primitives=20))
                for i, duration in enumerate((10, 8, 1, 2, 3, 4))])
        self.stats = dict(compile_queued=1, compile_completed=1, ready=55, positiont=55)

    def test_percentiles_use_nearest_rank(self):
        d = distribution(list(range(1, 101)))
        self.assertEqual((d["median"], d["p95"], d["p99"]), (50.5, 95, 99))
        for values in ([], [float("nan")], [-1], [float("inf")]):
            with self.assertRaises(ValueError):
                distribution(values)

    def test_warmup_is_excluded_and_jit_bound_is_conservative(self):
        result = summarize_profile(self.profile, self.stats, "on", "composite_2d", 2, 4)
        self.assertEqual(result["measuredRenderMilliseconds"]["median"], 2.5)
        self.assertEqual(result["measuredDrawCalls"], 40)
        self.assertEqual(result["measuredReadySelectionsLowerBound"], 35)

    def test_jit_used_only_during_warmup_is_not_enough(self):
        self.stats["ready"] = 20
        with self.assertRaisesRegex(ValueError, "measured interval"):
            summarize_profile(self.profile, self.stats, "on", "composite_2d", 2, 4)

    def test_vertex_work_only_during_warmup_is_not_enough(self):
        self.stats["positiont"] = 20
        with self.assertRaisesRegex(ValueError, "required generated vertices"):
            summarize_profile(self.profile, self.stats, "on", "composite_2d", 2, 4)

    def test_incomplete_or_wrong_mode_evidence_is_rejected(self):
        for change in (dict(compile_completed=0), dict(pipeline_failed=1), dict(compile_failed=1)):
            with self.assertRaises(ValueError):
                summarize_profile(self.profile, dict(self.stats, **change), "on", "composite_2d", 2, 4)
        with self.assertRaisesRegex(ValueError, "disabled vertex"):
            summarize_profile(self.profile, self.stats, "fragment", "composite_2d", 2, 4)
        profile = copy.deepcopy(self.profile)
        profile["frames"][2]["warmup"] = True
        with self.assertRaisesRegex(ValueError, "boundary"):
            summarize_profile(profile, self.stats, "on", "composite_2d", 2, 4)

    def test_aggregation_does_not_mix_drivers_or_failed_runs(self):
        summary = summarize_profile(self.profile, self.stats, "on", "composite_2d", 2, 4)
        summary["coldStartMilliseconds"]["engineInit"] = 20
        rows = [dict(driver=driver, scene="composite_2d", mode="on", issues=issues, summary=summary)
                for driver, issues in (("vulkan", []), ("vulkan", []), ("vulkan", ["failed"]), ("direct3d12", []))]
        result = aggregate(rows)
        self.assertEqual([r["repetitions"] for r in result], [2, 1])
        self.assertEqual(result[0]["medianOfRunP99Ms"], 4)

    def test_foreground_summary_excludes_any_loss_of_focus(self):
        summary = summarize_profile(self.profile, self.stats, "on", "composite_2d", 2, 4)
        summary["coldStartMilliseconds"]["engineInit"] = 20
        row = dict(driver="vulkan", scene="composite_2d", mode="on", issues=[], summary=summary)
        unfocused = copy.deepcopy(row)
        unfocused["summary"]["allMeasuredFramesFocused"] = False
        unfocused["summary"]["measuredRenderMilliseconds"]["median"] = 100
        self.assertEqual(aggregate([row, unfocused])[0]["repetitions"], 2)
        result = aggregate([row, unfocused], foreground_only=True)
        self.assertEqual(result[0]["repetitions"], 1)
        self.assertEqual(result[0]["medianOfRunMediansMs"], 2.5)

    def test_paired_foreground_requires_all_three_modes_in_the_same_run(self):
        rows = [dict(driver="vulkan", scene="composite_2d", repeat=repeat, mode=mode,
                     issues=[], summary=dict(allMeasuredFramesFocused=True))
                for repeat in (1, 2, 3) for mode in ("off", "fragment", "on")]
        rows[3]["summary"]["allMeasuredFramesFocused"] = False
        rows.pop()
        self.assertEqual([r["repeat"] for r in paired_foreground(rows)], [1, 1, 1])


if __name__ == "__main__":
    unittest.main()

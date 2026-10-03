"""Checks reporting boundaries without a GPU or timing-sensitive assertions."""
import copy
import unittest

from scene_benchmark_jit import (aggregate, aggregate_comparisons, distribution, mode_order,
                                 paired_comparisons, paired_foreground, summarize_profile,
                                 SNAPSHOT_FIELDS, summarize_jit_interval)


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

    def test_paired_deltas_are_computed_before_aggregation(self):
        rows = []
        for repeat, (disabled, full) in enumerate(((10, 19), (20, 31), (30, 15)), 1):
            for mode, value in (("off", disabled), ("fragment", disabled), ("on", full)):
                rows.append(dict(driver="vulkan", scene="composite_2d", repeat=repeat,
                    mode=mode, issues=[], summary=dict(allMeasuredFramesFocused=True,
                        measuredRenderMilliseconds=dict(median=value, p95=value, p99=value))))
        comparisons = paired_comparisons(rows)
        self.assertEqual(len(comparisons), 27)
        result = [r for r in aggregate_comparisons(comparisons)
                  if r["baselineMode"] == "off" and r["mode"] == "on" and r["metric"] == "p99"][0]
        # Median of paired differences is +9; subtracting group medians gives -1.
        self.assertEqual(result["medianPairedDeltaMs"], 9)
        self.assertEqual(result["medianPairedChangePercent"], 55)
        self.assertEqual((result["higherRepetitions"], result["lowerRepetitions"]), (2, 1))
        self.assertEqual((result["minimumPairedDeltaMs"], result["maximumPairedDeltaMs"]), (-15, 11))
        rows[0]["summary"]["allMeasuredFramesFocused"] = False
        self.assertEqual({r["repeat"] for r in paired_comparisons(rows)}, {2, 3})

    def test_zero_baseline_keeps_absolute_delta_without_inventing_percentage(self):
        rows = [dict(driver="vulkan", scene="composite_2d", repeat=1, mode=mode,
            issues=[], summary=dict(allMeasuredFramesFocused=True,
                measuredRenderMilliseconds=dict(median=value, p95=value, p99=value)))
                for mode, value in (("off", 0), ("fragment", 1), ("on", 2))]
        result = [r for r in aggregate_comparisons(paired_comparisons(rows))
                  if r["baselineMode"] == "off" and r["mode"] == "on"][0]
        self.assertEqual(result["medianPairedDeltaMs"], 2)
        self.assertIsNone(result["medianPairedChangePercent"])
        self.assertEqual(result["percentageRepetitions"], 0)

    def add_snapshots(self):
        before = dict.fromkeys(SNAPSHOT_FIELDS, 0)
        before.update(Requests=20, Specialized=20, PipelineSelections=20, PipelineReady=20,
                      PositionTReady=20, CompileQueued=3, CompileCompleted=3,
                      VertexCompileCompleted=18, PipelineQueued=6, PipelineCompleted=6, SynchronousRequests=2)
        after = dict(before, Requests=60, Specialized=60, PipelineSelections=60, PipelineReady=60, PositionTReady=60)
        self.profile["jitInterval"] = dict(schemaVersion=1, firstFrame=2, endFrameExclusive=6,
                                           before=before, after=after)
        return before, after

    def test_snapshots_prove_the_measured_interval_without_changing_cold_counters(self):
        self.add_snapshots()
        result = summarize_profile(self.profile, self.stats, "on", "composite_2d", 2, 4, require_steady=True)
        self.assertTrue(result["measuredJit"]["steady"])
        self.assertEqual(result["measuredReadySelectionsLowerBound"], 40)
        self.assertEqual(result["measuredVertexSelectionsLowerBounds"]["positiont"], 40)
        self.assertEqual(result["measuredJit"]["delta"]["CompileCompleted"], 0)

    def test_missing_snapshots_are_unknown_and_fail_strict_measurement(self):
        self.profile["jitInterval"] = None
        self.assertIsNone(summarize_jit_interval(self.profile, "on", "composite_2d", 40))
        with self.assertRaisesRegex(ValueError, "requires boundary snapshots"):
            summarize_jit_interval(self.profile, "on", "composite_2d", 40, True)

    def test_jobs_pending_at_measurement_start_cannot_be_hidden_by_teardown(self):
        before, after = self.add_snapshots()
        before["CompileQueued"] += 1
        after["CompileQueued"] += 1
        after["CompileCompleted"] += 1
        result = summarize_jit_interval(self.profile, "on", "composite_2d", 40)
        self.assertEqual(result["pendingBefore"]["shader"], 1)
        self.assertFalse(result["noShaderCompilation"])
        with self.assertRaisesRegex(ValueError, "not steady"):
            summarize_profile(self.profile, self.stats, "on", "composite_2d", 2, 4, require_steady=True)

    def test_late_shader_pipeline_or_synchronous_creation_fails_strict_measurement(self):
        for counters in (("CompileQueued", "CompileCompleted", "VertexCompileCompleted"),
                         ("PipelineQueued", "PipelineCompleted"), ("SynchronousRequests",)):
            with self.subTest(counters=counters):
                _, after = self.add_snapshots()
                for counter in counters:
                    after[counter] += 1
                with self.assertRaisesRegex(ValueError, "not steady"):
                    summarize_jit_interval(self.profile, "on", "composite_2d", 40, True)

    def test_fallback_inside_measurement_is_reported_and_rejected_in_strict_mode(self):
        _, after = self.add_snapshots()
        after["PipelineReady"] -= 1
        after["PositionTReady"] -= 1
        after["ShaderPending"] += 1
        result = summarize_jit_interval(self.profile, "on", "composite_2d", 40)
        self.assertEqual(result["fallbackSelections"], 1)
        self.assertFalse(result["allMeasuredDrawsUsedJit"])
        with self.assertRaisesRegex(ValueError, "not steady"):
            summarize_jit_interval(self.profile, "on", "composite_2d", 40, True)

    def test_invalid_snapshot_boundary_or_decreasing_counter_is_rejected(self):
        self.add_snapshots()
        self.profile["jitInterval"]["firstFrame"] = 3
        with self.assertRaisesRegex(ValueError, "boundaries"):
            summarize_jit_interval(self.profile, "on", "composite_2d", 40)
        _, after = self.add_snapshots()
        after["Evictions"] = -1
        with self.assertRaisesRegex(ValueError, "counter"):
            summarize_jit_interval(self.profile, "on", "composite_2d", 40)
        before, _ = self.add_snapshots()
        before["Evictions"] = 1
        with self.assertRaisesRegex(ValueError, "decreased"):
            summarize_jit_interval(self.profile, "on", "composite_2d", 40)

    def test_disabled_modes_and_required_vertices_are_checked_at_boundaries(self):
        _, after = self.add_snapshots()
        with self.assertRaisesRegex(ValueError, "disabled vertex"):
            summarize_jit_interval(self.profile, "fragment", "composite_2d", 40)
        after["PositionTReady"] = 20
        with self.assertRaisesRegex(ValueError, "generated vertices absent"):
            summarize_jit_interval(self.profile, "on", "composite_2d", 40)

    def test_steady_disabled_run_does_not_claim_jit_draws(self):
        before, after = self.add_snapshots()
        for snapshot, requests in ((before, 20), (after, 60)):
            snapshot.update(dict.fromkeys(SNAPSHOT_FIELDS, 0))
            snapshot.update(Requests=requests, Unavailable=requests, SynchronousRequests=2)
        result = summarize_jit_interval(self.profile, "off", "composite_2d", 40, True)
        self.assertTrue(result["steady"])
        self.assertIsNone(result["allMeasuredDrawsUsedJit"])
        self.assertEqual(result["delta"]["PipelineReady"], 0)

    def test_inconsistent_snapshot_accounting_and_failures_are_rejected(self):
        _, after = self.add_snapshots()
        after["PipelineSelections"] -= 1
        with self.assertRaisesRegex(ValueError, "inconsistent"):
            summarize_jit_interval(self.profile, "on", "composite_2d", 40)
        _, after = self.add_snapshots()
        after["CompileFailed"] = 1
        with self.assertRaisesRegex(ValueError, "failure"):
            summarize_jit_interval(self.profile, "on", "composite_2d", 40)


if __name__ == "__main__":
    unittest.main()

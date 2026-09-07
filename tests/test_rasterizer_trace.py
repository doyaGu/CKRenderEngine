"""Validate timeline accounting with synthetic intervals, not GPU timing assumptions."""
import importlib.util
from contextlib import closing
from pathlib import Path
import sqlite3
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True

spec = importlib.util.spec_from_file_location("rasterizer_trace", Path(__file__).parents[1]/"tools/summarize_rasterizer_trace.py")
trace = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = trace
spec.loader.exec_module(trace)


class RasterizerTraceTest(unittest.TestCase):
    def queue_fixture(self):
        ranges = [trace.Range(0,100000,"CKRE.SDL.NativeSubmit"),
                  trace.Range(200000,300000,"CKRE.SDL.Acquire"),
                  trace.Range(400000,500000,"CKRE.SDL.NativeSubmit"),
                  trace.Range(700000,1800000,"CKRE.SDL.Acquire")]
        execute = [(20000,30000,1,99),(450000,460000,2,99)]
        gpu = [(1500000,1600000,1,99),(1700000,1900000,2,99)]
        return ranges, execute, gpu

    def test_queue_model_partitions_delayed_gpu_start(self):
        result = trace.correlate_acquires(*self.queue_fixture(),2)
        self.assertFalse(result["fence_identity_observed"])
        self.assertEqual(result["matched_native_submissions"],2)
        self.assertEqual(result["excluded"],{"capture_boundary":1})
        group = result["groups"]["over_1ms"]
        self.assertEqual(group["count"],1)
        self.assertEqual(group["candidate_submit_to_gpu_start_us"]["total"],1480)
        self.assertEqual([group["partition_us"][key]["total"] for key in ("queued","executing","after")],
                         [800,100,200])

    def test_queue_model_distinguishes_completed_and_still_running_candidates(self):
        ranges, execute, gpu = self.queue_fixture()
        for span, expected, before, after in [((100000,150000),[0,0,1100],1,0),
                                            ((1700000,1900000),[1000,100,0],0,1)]:
            gpu[0] = (*span,1,99)
            group = trace.correlate_acquires(ranges,execute,gpu,2)["groups"]["all"]
            self.assertEqual([group["partition_us"][key]["total"] for key in ("queued","executing","after")],expected)
            self.assertEqual(group["candidate_finished_before_acquire"],before)
            self.assertEqual(group["candidate_finishes_after_acquire"],after)

    def test_missing_pairs_do_not_shift_the_queue(self):
        ranges, execute, gpu = self.queue_fixture()
        for missing in (execute[:1],execute[1:]):
            result = trace.correlate_acquires(ranges,missing,gpu,2)
            self.assertEqual(result["groups"]["all"]["count"],0)
            self.assertEqual(result["excluded"]["missing_or_ambiguous_submission_pair"],1)

    def test_queue_correlation_is_scoped_to_native_queue(self):
        ranges, execute, gpu = self.queue_fixture()
        # Another queue may reuse a correlation number without being our batch.
        result = trace.correlate_acquires(ranges,execute,gpu+[(100,200,1,88)],2)
        self.assertEqual(result["groups"]["all"]["count"],1)
        result = trace.correlate_acquires(ranges,execute,gpu+[gpu[0]],2)
        self.assertEqual(result["groups"]["all"]["count"],0)
        execute[1] = (*execute[1][:3],88)
        gpu[1] = (*gpu[1][:3],88)
        result = trace.correlate_acquires(ranges,execute,gpu,2)
        self.assertEqual(result["excluded"]["multiple_queues"],1)

    def test_duplicate_api_correlation_and_multiple_batches_are_rejected(self):
        ranges, execute, gpu = self.queue_fixture()
        result = trace.correlate_acquires(ranges,execute+[(600000,610000,1,99)],gpu,2)
        self.assertEqual(result["groups"]["all"]["count"],0)
        result = trace.correlate_acquires(ranges,execute+[(35000,40000,3,99)],gpu,2)
        self.assertEqual(result["groups"]["all"]["count"],0)

    def test_nonpresent_submission_is_not_silently_counted_as_a_frame(self):
        ranges, execute, gpu = self.queue_fixture()
        ranges.pop(1)
        result = trace.correlate_acquires(ranges,execute,gpu,2)
        self.assertEqual(result["excluded"]["not_one_acquire_per_submission"],1)
        with self.assertRaisesRegex(ValueError,"queue depth"):
            trace.correlate_acquires(ranges,execute,gpu,4)

    def test_nested_ranges_partition_wall_time(self):
        ranges = [trace.Range(0,100,"frame"),trace.Range(10,70,"draw"),
                  trace.Range(20,40,"material"),trace.Range(45,65,"ffp"),trace.Range(50,60,"record")]
        result=trace.decompose(ranges)
        self.assertEqual(result["frame"]["exclusive"],[40])
        self.assertEqual(result["draw"]["exclusive"],[20])
        self.assertEqual(result["ffp"]["exclusive"],[10])
        self.assertEqual(sum(sum(v["exclusive"]) for v in result.values()),100)
        self.assertEqual(trace.decompose(ranges),result)

    def test_crossing_ranges_are_rejected(self):
        with self.assertRaisesRegex(ValueError,"crossing"):
            trace.decompose([trace.Range(0,100,"frame"),trace.Range(10,60,"a"),trace.Range(30,70,"b")])

    def test_database_excludes_partial_frames_and_unions_waits(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/"trace.sqlite"
            with closing(sqlite3.connect(path)) as db, db:
                db.executescript('''
                    CREATE TABLE META_DATA_CAPTURE(name TEXT,value TEXT);
                    INSERT INTO META_DATA_CAPTURE VALUES('RUN_DURATION_MS','1');
                    INSERT INTO META_DATA_CAPTURE VALUES('ENVIRONMENT','must never appear');
                    CREATE TABLE StringIds(id INTEGER,value TEXT);
                    INSERT INTO StringIds VALUES(1,'Win32 Wait API');
                    CREATE TABLE NVTX_EVENTS(start INTEGER,end INTEGER,text TEXT,globalTid INTEGER,uint64Value INTEGER,textId INTEGER);
                    CREATE TABLE OSRT_API(start INTEGER,end INTEGER,globalTid INTEGER,nameId INTEGER);
                    INSERT INTO OSRT_API VALUES(80,90,42,1),(82,88,42,1);
                ''')
                rows=[(-100,5,"CKRE.CK3D.Render",None),(0,3,"CKRE.Orphan",None),
                      (10,110,"CKRE.CK3D.Render",None),(20,70,"CKRE.Draw",None),
                      (70,100,"CKRE.SDL.NativeSubmit",None),(200,300,"CKRE.CK3D.Render",None),
                      (55,None,"CKRE.Batch.Draws",4),(4,None,"CKRE.Batch.Draws",99),
                      (999900,1000100,"CKRE.CK3D.Render",None)]
                db.executemany('INSERT INTO NVTX_EVENTS VALUES(?,?,?,42,?,NULL)',rows)
            result=trace.summarize(path,42)
            self.assertEqual(result["complete_render_frames"],2)
            self.assertEqual(result["partial_ranges"],2)
            self.assertEqual(result["ranges_outside_complete_frames"],1)
            self.assertEqual(result["partition_error_ns"],0)
            self.assertEqual(result["counters"]["CKRE.Batch.Draws"]["per_frame"],2)
            self.assertAlmostEqual(result["wait_api_wall_us_by_exclusive_scope"]["CKRE.SDL.NativeSubmit"],.01)
            self.assertNotIn("must never appear",str(result))


if __name__ == "__main__":
    unittest.main()

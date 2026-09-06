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

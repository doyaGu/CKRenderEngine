import argparse
import json
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


TOOL = None
FRAMES = Path(__file__).parent / "reference" / "frames" / "golden" / "windows"
GOLDEN_SCRIPT = Path(__file__).parent / "scene_capture_golden.cmake"


class SceneImageComparisonTest(unittest.TestCase):
    def setUp(self):
        if TOOL is None:
            raise RuntimeError("Provide the scene-capture executable to run image comparisons")
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.actual = self.root / "actual"
        self.expected = self.root / "expected"
        self.actual.mkdir()
        self.expected.mkdir()
        self.name = "alpha_blend.png"
        for directory in (self.actual, self.expected):
            shutil.copyfile(FRAMES / self.name, directory / self.name)

    def compare(self, *arguments):
        return subprocess.run(
            [str(TOOL), "--compare-only", "--scene", "alpha_blend",
             "--out", str(self.actual), "--compare", str(self.expected),
             "--threshold", "2", "--min-pass", "0.995", *arguments],
            capture_output=True, text=True, timeout=15,
        )

    def test_identical_frames_pass_without_loading_a_renderer(self):
        result = self.compare()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("1 compared, 1 passed, 0 failed, 0 without reference", result.stdout)

    def test_different_frames_fail_the_pixel_comparison(self):
        shutil.copyfile(FRAMES / "clear_subviewport.png", self.actual / self.name)
        self.assertEqual(self.compare().returncode, 5)

    def test_missing_reference_cannot_pass_an_empty_comparison(self):
        (self.expected / self.name).unlink()
        self.assertEqual(self.compare().returncode, 4)

    def test_missing_capture_and_corrupt_images_are_image_errors(self):
        (self.actual / self.name).unlink()
        self.assertEqual(self.compare().returncode, 4)
        (self.actual / self.name).write_bytes(b"invalid PNG")
        self.assertEqual(self.compare().returncode, 4)

    def test_invalid_thresholds_and_pass_ratios_are_rejected(self):
        for threshold in ("junk", "-1", "256", "1.5", "999999999999999999999999"):
            with self.subTest(threshold=threshold):
                self.assertEqual(self.compare("--threshold", threshold).returncode, 1)
        for ratio in ("junk", "0", "-1", "nan", "inf", "1.01", "1e-100"):
            with self.subTest(ratio=ratio):
                self.assertEqual(self.compare("--min-pass", ratio).returncode, 1)

    def test_reference_directory_is_required_and_scene_exclusions_are_rejected(self):
        self.assertEqual(subprocess.run(
            [str(TOOL), "--compare-only"], capture_output=True, timeout=15,
        ).returncode, 1)
        self.assertEqual(self.compare("--skip", "alpha_blend").returncode, 1)

    def test_capture_mode_cannot_pass_when_every_comparison_was_excluded(self):
        listing = subprocess.run(
            [str(TOOL), "--list-scenes"], capture_output=True, text=True, check=True, timeout=15,
        ).stdout
        scenes = re.findall(r"^(\w+)\s+oracle=", listing, re.MULTILINE)
        self.assertTrue(scenes)
        result = subprocess.run(
            [str(TOOL), "--render-engine-dir", str(self.root / "missing-renderer"),
             "--scene", "all", "--skip", ",".join(scenes), "--out", str(self.actual),
             "--compare", str(self.expected)], capture_output=True, text=True, timeout=15,
        )
        self.assertEqual(result.returncode, 4, result.stdout + result.stderr)

    def run_golden(self, references, output):
        return subprocess.run(
            ["cmake", f"-DTOOL={TOOL}", f"-DGOLDEN_ROOT={references}",
             f"-DOUT_DIR={output}", f"-DENGINE_DIR={self.root / 'missing-renderer'}",
             "-P", str(GOLDEN_SCRIPT)], capture_output=True, text=True, timeout=15,
        )

    def test_capture_output_cannot_overwrite_reviewed_frames(self):
        result = self.run_golden(self.expected, self.expected)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must not overlap", result.stdout + result.stderr)

    def test_manifest_cannot_omit_a_registered_scene(self):
        references = self.root / "references"
        reference_directory = references / platform.system().lower()
        shutil.copytree(FRAMES, reference_directory)
        manifest_path = reference_directory / "baseline.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        removed = manifest["scenes"].pop(0)
        (reference_directory / (removed["name"] + ".png")).unlink()
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        result = self.run_golden(references, self.actual)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("every registered scene exactly once", result.stdout + result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("tool", type=Path)
    options, arguments = parser.parse_known_args()
    TOOL = options.tool.resolve()
    unittest.main(argv=[sys.argv[0], *arguments])

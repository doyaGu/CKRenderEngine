"""Exercise external-reference gates without requiring a GPU or Virtools DLLs."""
import contextlib
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import scene_capture_jit


class SceneCaptureReferenceTest(unittest.TestCase):
    def run_capture(self, external=True, off_exit=0, off_comparisons=5, scene="test_scene", lit_draws=0,
                    off_failed_pixels=0, off_max_diff=1):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            reference = root / "reference"
            reference.mkdir()
            output = root / "captures"
            args = ["scene_capture_jit.py", "--tool", str(root / "tool"),
                    "--engine-dir", str(root), "--out", str(output), "--drivers", "vulkan"]
            if external:
                args += ["--reference-dir", str(reference)]
            captures = []

            def run(command, **kwargs):
                if "--list-scenes" in command:
                    return subprocess.CompletedProcess(command, 0, scene + " oracle=yes\n")
                captures.append(command)
                enabled = kwargs["env"]["CKRE_SDL_GPU_FF_JIT"] == "1"
                stats = "FFJIT_STATS ready=1 selected=1" if enabled else "FFJIT_STATS ready=0 selected=0"
                if enabled and kwargs["env"]["CKRE_SDL_GPU_FF_VERTEX_JIT"] == "1":
                    stats += f" lit={lit_draws}"
                count = 5 if enabled else off_comparisons
                comparisons = ""
                if "--compare" in command:
                    names = [scene] + [f"{scene}.frame-{frame}" for frame in (1, 5, 30, 120)]
                    for index, name in enumerate(names[:count]):
                        failed = off_failed_pixels if not enabled and index == 0 else 0
                        maximum = off_max_diff if not enabled and index == 0 else 1
                        comparisons += (f"[{name}] pass {100 * (1 - failed / 307200):.6f}% "
                                        f"(threshold 2, min 100.00%, max diff {maximum}, "
                                        f"failed pixels {failed}, masked 0) {'FAIL' if failed else 'OK'}\n")
                return subprocess.CompletedProcess(command, 0 if enabled else off_exit, stats + "\n" + comparisons)

            with patch("sys.argv", args), patch.object(scene_capture_jit.subprocess, "run", side_effect=run), contextlib.redirect_stdout(io.StringIO()):
                code = scene_capture_jit.main()
            rows = json.loads((output / "results.json").read_text())
            return code, captures, rows, reference, output

    def test_external_reference_checks_all_three_modes(self):
        code, captures, rows, reference, _ = self.run_capture()
        self.assertEqual(code, 0)
        self.assertEqual(len(captures), 3)
        for command, row in zip(captures, rows):
            self.assertEqual(command[command.index("--compare") + 1], str(reference))
            self.assertEqual(command[command.index("--threshold") + 1], "2")
            self.assertEqual(command[command.index("--min-pass") + 1], "1")
            self.assertIn("--require-all", command)
            self.assertEqual(row["reference"], str(reference))
            self.assertEqual(row["comparisons"], 5)

    def test_off_mode_oracle_failure_fails_the_run(self):
        code, _, rows, _, _ = self.run_capture(off_exit=5)
        self.assertEqual(code, 1)
        self.assertIn("capture/compare exit 5", rows[0]["issues"])

    def test_report_retains_single_pixel_failure_evidence(self):
        code, _, rows, _, _ = self.run_capture(off_exit=5, off_failed_pixels=1, off_max_diff=11)
        self.assertEqual(code, 1)
        self.assertEqual(rows[0]["max_channel_diff"], 11)
        self.assertEqual(rows[0]["image_comparisons"][0],
                         dict(image="test_scene", max_channel_diff=11, failed_pixels=1, masked_pixels=0))
        self.assertEqual(len(rows[0]["image_comparisons"]), 5)
        self.assertTrue(all(image["failed_pixels"] == 0 for image in rows[0]["image_comparisons"][1:]))
        self.assertIn("capture/compare exit 5", rows[0]["issues"])

    def test_missing_off_mode_checkpoint_fails_the_run(self):
        code, _, rows, _, _ = self.run_capture(off_comparisons=4)
        self.assertEqual(code, 1)
        self.assertIn("expected final image and four checkpoints", rows[0]["issues"])

    def test_default_keeps_same_engine_parity(self):
        code, captures, rows, _, output = self.run_capture(external=False)
        self.assertEqual(code, 0)
        self.assertNotIn("--compare", captures[0])
        self.assertIsNone(rows[0]["reference"])
        for command in captures[1:]:
            self.assertEqual(command[command.index("--compare") + 1], str(output / "vulkan" / "off"))

    def test_reference_cannot_be_overwritten_by_capture(self):
        for reference in ("out", "out/vulkan/off", "."):
            args = ["scene_capture_jit.py", "--tool", "unused", "--engine-dir", ".",
                    "--out", "out", "--reference-dir", reference]
            with patch("sys.argv", args), patch.object(scene_capture_jit.subprocess, "run") as run, contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    scene_capture_jit.main()
                self.assertEqual(error.exception.code, 2)
                run.assert_not_called()

    def test_spotlight_parity_requires_generated_lighting(self):
        for draws in (0, 1):
            with self.subTest(lit_draws=draws):
                code, _, rows, _, _ = self.run_capture(scene="lighting_spotlight", lit_draws=draws)
                self.assertEqual(code, int(draws == 0))
                self.assertEqual(rows[0]["issues"], [])
                self.assertEqual(rows[1]["issues"], [])
                self.assertEqual(rows[2]["issues"], [] if draws else ["no generated lit vertex draw"])


if __name__ == "__main__":
    unittest.main()

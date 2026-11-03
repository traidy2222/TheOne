import tempfile
import unittest
from pathlib import Path
from trace import read_frames, summarize, compatibility


class CaptureComparisonTests(unittest.TestCase):
    def test_contexts_do_not_mix_focus_resolution_or_present_mode(self):
        frames = {}
        for i, (focus, width, mode) in enumerate([(0, 1920, 2), (1, 1920, 2), (1, 1280, 2), (1, 1920, 0)]):
            frames[i] = dict(phase="gameplay", focused=focus, width=width, height=1080,
                             present_mode=mode, visible=1, frame_wall=i+1)
        groups = summarize(frames, ["frame_wall"])
        self.assertEqual(len(groups), 4)
        self.assertTrue(all(g["frame_wall"]["count"] == 1 for g in groups.values()))

    def test_incomplete_or_nonfinite_capture_rejected(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)/"trace.csv"
            for row in ["0,gameplay,fence,1", "0,gameplay,frame_wall,nan"]:
                path.write_text("frame,phase,metric,value\n"+row+"\n")
                with self.assertRaises(ValueError):
                    read_frames(path)
            path.write_text("frame,phase,metric,value\n-1,startup,frame_wall,100\n0,gameplay,frame_wall,5\n")
            self.assertEqual(list(read_frames(path)), [0])

    def test_build_drift_and_missing_frames_are_exposed(self):
        good = dict(schema=1, gpu="fixture", scene_sha256="scene", exit_code=0,
                    source_unchanged=True, binary_unchanged=True, frames_requested=100,
                    frames_measured=100, source_before={"sources_newer_than_executable": []})
        self.assertEqual(compatibility(good, good), [])
        bad = dict(good, binary_unchanged=False, frames_measured=99,
                   source_before={"sources_newer_than_executable": ["changed.cpp"]})
        issues = compatibility(good, bad)
        self.assertEqual(len(issues), 3)


if __name__ == "__main__":
    unittest.main()

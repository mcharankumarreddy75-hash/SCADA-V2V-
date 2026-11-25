"""Runs tools/log_analyzer.py on SYNTHETIC samples from tools/make_sample_data.py into a temp folder.

Checks that every requested PNG is written and that summary.md is produced and marked SYNTHETIC.
Needs tools/ on PYTHONPATH (tests/host/run_tests.py sets it).
"""
import shutil
import tempfile
import unittest
from pathlib import Path

import log_analyzer
import make_sample_data


class LogAnalyzerSampleTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="v2v_analyzer_"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_samples_produce_pngs_and_summary(self):
        samples = self.tmp / "sample"
        make_sample_data.generate(samples)
        out = self.tmp / "out"
        rc = log_analyzer.main([
            "--db", str(samples / "sample_v2v.db"),
            "--rtt", str(samples / "sample_rtt.csv"),
            "--distance-error", str(samples / "sample_distance_tape.csv"),
            "--imu-log", str(samples / "sample_imu_normal_drive.csv"),
            "--out-dir", str(out),
        ])
        self.assertEqual(rc, 0)

        expected = [
            "pdr_vs_distance.png", "rssi_vs_distance.png", "ttc_at_warning.png", "alarm_counts.png",
            "latency_sample_rtt.png", "distance_error.png",
            "imu_thresholds_sample_imu_normal_drive.png",
        ]
        for name in expected:
            png = out / name
            self.assertTrue(png.exists(), name)
            with open(png, "rb") as f:
                self.assertEqual(f.read(8), b"\x89PNG\r\n\x1a\n", name)

        summary = (out / "summary.md").read_text(encoding="utf-8")
        self.assertIn("SYNTHETIC", "\n".join(summary.splitlines()[:6]))
        for sid in ("<!-- section:db -->", "<!-- section:inputs -->",
                    "<!-- section:rtt-sample_rtt -->", "<!-- section:distance-sample_distance_tape -->",
                    "<!-- section:imu-sample_imu_normal_drive -->"):
            self.assertIn(sid, summary)

    def test_summary_sections_are_replaced_not_duplicated(self):
        samples = self.tmp / "sample"
        make_sample_data.generate(samples)
        out = self.tmp / "out"
        args = ["--rtt", str(samples / "sample_rtt.csv"), "--out-dir", str(out)]
        self.assertEqual(log_analyzer.main(args), 0)
        self.assertEqual(log_analyzer.main(args), 0)
        summary = (out / "summary.md").read_text(encoding="utf-8")
        self.assertEqual(summary.count("<!-- section:rtt-sample_rtt -->"), 1)

    def test_db_battery_runtime_and_sos_reference(self):
        samples = self.tmp / "sample"
        make_sample_data.generate(samples)
        out = self.tmp / "out"
        rc = log_analyzer.main([
            "--db", str(samples / "sample_v2v.db"),
            "--sos-ref", str(make_sample_data.SOS_REF[0]), str(make_sample_data.SOS_REF[1]),
            "--out-dir", str(out),
        ])
        self.assertEqual(rc, 0)
        png = out / "battery_runtime.png"
        self.assertTrue(png.exists())
        with open(png, "rb") as f:
            self.assertEqual(f.read(8), b"\x89PNG\r\n\x1a\n")

        summary = (out / "summary.md").read_text(encoding="utf-8")
        self.assertIn("### A13 battery runtime", summary)
        self.assertIn("| vehicle | start % | end % | duration h | time to low battery h |", summary)
        # the sample run drains to 10 % and never reaches cut-off, so the verdict must be incomplete
        self.assertIn("incomplete", summary)
        self.assertIn("### A8 / A9 alarm position vs reference", summary)
        self.assertIn("| time | vehicle | type | lat | lon | distance m | <= 5 m |", summary)
        self.assertIn("| PASS |", summary)
        self.assertIn("| FAIL |", summary)  # the 9 m alarm in the sample

    def test_missing_input_is_reported_and_fails(self):
        out = self.tmp / "out"
        rc = log_analyzer.main(["--db", str(self.tmp / "nope.db"), "--out-dir", str(out)])
        self.assertEqual(rc, 1)
        summary = (out / "summary.md").read_text(encoding="utf-8")
        self.assertIn("MISSING", summary)


if __name__ == "__main__":
    unittest.main()

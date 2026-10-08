import os
import platform
from pathlib import Path
import subprocess
import tempfile
import unittest


EXECUTABLE = Path(__file__).resolve().parents[1] / "fetch"


class FetchTests(unittest.TestCase):
    def run_fetch(self, args, home=None):
        env = os.environ.copy()
        if home:
            env["HOME"] = str(home)
        return subprocess.run(
            [str(EXECUTABLE), *args], env=env, capture_output=True, timeout=10
        )

    def test_help_and_version(self):
        self.assertIn(b"Usage:", self.run_fetch(["--help"]).stdout)
        self.assertIn(platform.system().encode(), self.run_fetch(["--version"]).stdout)

    def test_invalid_arguments(self):
        for args in [
            ["--frames", "-1"], ["--frames", "junk"], ["--frames", "2147483648"],
            ["--height", "0"], ["--size", "nan"], ["--depth", "inf"],
            ["--speed", "1junk"], ["--logo"], ["--unknown"],
        ]:
            with self.subTest(args=args):
                result = self.run_fetch(args)
                self.assertNotEqual(result.returncode, 0)
                self.assertTrue(result.stderr)

    def test_shading_and_cli_precedence(self):
        with tempfile.TemporaryDirectory(prefix="fetch-test-") as directory:
            home = Path(directory)
            config_dir = home / ".config" / "fetch"
            config_dir.mkdir(parents=True)
            (config_dir / "logo.txt").write_text("  @@  \n@@@@@@\n  @@  \n", encoding="utf-8")
            config = config_dir / "config"
            config.write_text("size=1\ndepth=1\nspeed=1\nheight=20\n")
            args = ["--no-info", "--frames", "1", "--no-color"]
            baseline = self.run_fetch(args, home)
            self.assertEqual(baseline.returncode, 0)
            self.assertNotIn(b"\x00", baseline.stdout)
            config.write_text("size=2\ndepth=2\nspeed=2\nheight=30\n")
            override = self.run_fetch(
                args + ["--size", "1", "--depth", "1", "--speed", "1", "--height", "20"], home
            )
            self.assertEqual(override.stdout, baseline.stdout)
            for mode in ["ascii", "blocks", "sextants"]:
                with self.subTest(mode=mode):
                    result = self.run_fetch(args + ["--shading-mode", mode], home)
                    self.assertEqual(result.returncode, 0)
                    self.assertTrue(result.stdout)

    def test_minimal_logos(self):
        with tempfile.TemporaryDirectory(prefix="fetch-test-") as directory:
            home = Path(directory)
            config_dir = home / ".config" / "fetch"
            config_dir.mkdir(parents=True)
            for logo in ["@\n", "@@@\n", "@\n@\n@\n"]:
                with self.subTest(logo=logo):
                    (config_dir / "logo.txt").write_text(logo)
                    result = self.run_fetch(["--no-info", "--frames", "1"], home)
                    self.assertEqual(result.returncode, 0)
                    self.assertTrue(result.stdout)


if __name__ == "__main__":
    unittest.main()

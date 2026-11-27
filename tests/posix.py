import os
import platform
import pty
import fcntl
import re
import select
import signal
import struct
import termios
import time
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

    def run_in_pty(self, args, home, rows, cols, resize_to=None):
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        env = os.environ.copy()
        env["HOME"] = str(home)
        proc = subprocess.Popen([str(EXECUTABLE), *args], env=env, stdin=slave,
                                stdout=slave, stderr=slave, start_new_session=True)
        os.close(slave)
        out, start = b"", time.monotonic()
        while True:
            if resize_to and time.monotonic() - start > 0.5:
                fcntl.ioctl(master, termios.TIOCSWINSZ,
                            struct.pack("HHHH", resize_to[0], resize_to[1], 0, 0))
                proc.send_signal(signal.SIGWINCH)
                resize_to = None
            if select.select([master], [], [], 0.05)[0]:
                try:
                    chunk = os.read(master, 65536)
                except OSError:
                    break
                if not chunk:
                    break
                out += chunk
            elif proc.poll() is not None:
                break
        proc.wait(timeout=10)
        os.close(master)
        return out

    def last_frame_rows(self, out):
        frame = out.decode("utf-8", "replace").split("\x1b[H")[-1]
        frame = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", frame)
        return sum(1 for line in frame.split("\n") if line.strip())

    def test_help_and_version(self):
        self.assertIn(b"Usage:", self.run_fetch(["--help"]).stdout)
        self.assertIn(platform.system().encode(), self.run_fetch(["--version"]).stdout)

    def test_invalid_arguments(self):
        for args in [
            ["--frames", "-1"], ["--frames", "junk"], ["--frames", "2147483648"],
            ["--frames", "1.5"], ["--height", "0"], ["--height", "201"],
            ["--size", "nan"], ["--size", "0.4"], ["--size", "5.1"],
            ["--depth", "inf"], ["--depth", "0"], ["--depth", "10.1"],
            ["--speed", "1junk"], ["--speed", "1e9999"], ["--speed", "1001"],
            ["--logo"], ["--unknown"],
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
            args = ["--no-info", "--frames", "3", "--no-color"]
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

    def test_zero_and_negative_config_speed(self):
        with tempfile.TemporaryDirectory(prefix="fetch-test-") as directory:
            home = Path(directory)
            config_dir = home / ".config" / "fetch"
            config_dir.mkdir(parents=True)
            (config_dir / "logo.txt").write_text("  @@  \n@@@@@@\n  @@  \n")
            config = config_dir / "config"
            args = ["--no-info", "--frames", "3", "--no-color"]
            for speed in ["0", "-20"]:
                with self.subTest(speed=speed):
                    config.write_text("speed=20\n")
                    baseline = self.run_fetch(args + ["--speed", speed], home)
                    config.write_text(f"speed={speed}\n")
                    configured = self.run_fetch(args, home)
                    self.assertEqual(baseline.returncode, 0)
                    self.assertEqual(configured.returncode, 0)
                    self.assertEqual(configured.stdout, baseline.stdout)

    def test_rotation_cli_precedence(self):
        with tempfile.TemporaryDirectory(prefix="fetch-test-") as directory:
            home = Path(directory)
            config_dir = home / ".config" / "fetch"
            config_dir.mkdir(parents=True)
            (config_dir / "logo.txt").write_text("  @@  \n@@@@@@\n  @@  \n")
            config = config_dir / "config"
            args = ["--no-info", "--frames", "3", "--no-color", "--speed", "20"]
            for axis, config_axis in [("x", "y"), ("y", "x")]:
                with self.subTest(axis=axis):
                    config.write_text("")
                    baseline = self.run_fetch(args + [f"--rotate-{axis}"], home)
                    config.write_text(f"spin={config_axis}\n")
                    override = self.run_fetch(args + [f"--rotate-{axis}"], home)
                    self.assertEqual(baseline.returncode, 0)
                    self.assertEqual(override.returncode, 0)
                    self.assertEqual(override.stdout, baseline.stdout)

    def test_invalid_config_numbers(self):
        with tempfile.TemporaryDirectory(prefix="fetch-test-") as directory:
            home = Path(directory)
            config_dir = home / ".config" / "fetch"
            config_dir.mkdir(parents=True)
            (config_dir / "logo.txt").write_text("  @@  \n@@@@@@\n  @@  \n")
            config = config_dir / "config"
            args = ["--no-info", "--frames", "3", "--no-color"]
            baseline = self.run_fetch(args, home)
            self.assertEqual(baseline.returncode, 0)
            for key, value in [("height", "0"), ("size", "nan"),
                               ("depth", "inf"), ("speed", "1junk")]:
                with self.subTest(key=key):
                    config.write_text(f"{key}={value}\n")
                    result = self.run_fetch(args, home)
                    self.assertEqual(result.returncode, 0)
                    self.assertIn(key.encode(), result.stderr)
                    self.assertEqual(result.stdout, baseline.stdout)

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

    def test_size_kept_after_resize(self):
        with tempfile.TemporaryDirectory(prefix="fetch-test-") as directory:
            home = Path(directory)
            config_dir = home / ".config" / "fetch"
            config_dir.mkdir(parents=True)
            (config_dir / "logo.txt").write_text(("@" * 30 + "\n") * 12)
            args = ["--no-info", "--no-color", "--rotate-y", "--size", "1.5",
                    "--height", "24", "--frames", "30"]
            fresh = self.last_frame_rows(self.run_in_pty(args, home, 30, 80))
            resized = self.last_frame_rows(
                self.run_in_pty(args, home, 20, 80, resize_to=(30, 80)))
            self.assertGreater(fresh, 0)
            self.assertLessEqual(abs(resized - fresh), 1)


if __name__ == "__main__":
    unittest.main()

import os
import fcntl
import json
import platform
import pty
import re
import select
import signal
import struct
import sys
import termios
import time
from pathlib import Path
import subprocess
import tempfile
import unittest


EXECUTABLE = Path(__file__).resolve().parents[1] / "fetch"


class FetchTests(unittest.TestCase):
    def run_fetch(self, args, home=None, env_overrides=None, input_data=None):
        env = os.environ.copy()
        if home:
            env["HOME"] = str(home)
        if env_overrides:
            env.update(env_overrides)
        return subprocess.run(
            [str(EXECUTABLE), *args], env=env, input=input_data,
            capture_output=True, timeout=10
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

    def test_benchmark_repeatable(self):
        with tempfile.TemporaryDirectory(prefix="fetch-test-") as directory:
            home = Path(directory)
            args = ["--benchmark", "--frames", "25"]
            baseline = self.run_fetch(args, home, {"PATH": ""}, input_data=b"quit")
            self.assertEqual(baseline.returncode, 0, baseline.stderr)
            self.assertEqual(baseline.stderr, b"")
            self.assertNotIn(b"\x1b", baseline.stdout)
            self.assertRegex(baseline.stdout, rb"Rendered 25 frames in [0-9.]+ seconds")
            self.assertRegex(baseline.stdout, rb"[0-9.]+ FPS, [0-9.]+ ms/frame")
            config_dir = home / ".config" / "fetch"
            config_dir.mkdir(parents=True)
            (config_dir / "config").write_text("size=invalid\nspin=z\nshading_mode=blocks\n")
            (config_dir / "logo.txt").write_text("@@\n")
            repeated = self.run_fetch(args, home, {"PATH": ""}, input_data=b"")
            self.assertEqual(repeated.returncode, 0, repeated.stderr)
            self.assertEqual(repeated.stderr, b"")
            self.assertEqual(baseline.stdout.splitlines()[-1],
                             repeated.stdout.splitlines()[-1])
            rotated = self.run_fetch(args + ["--rotate-z"], home)
            self.assertEqual(rotated.returncode, 0, rotated.stderr)
            self.assertNotEqual(baseline.stdout.split(b"checksum: ")[-1],
                                rotated.stdout.split(b"checksum: ")[-1])
            blocks = self.run_fetch(args + ["--shading-mode", "blocks"], home)
            self.assertEqual(blocks.returncode, 0, blocks.stderr)
            self.assertNotIn(b"\x1b", blocks.stdout)

    def test_benchmark_frame_limits(self):
        result = self.run_fetch(["--benchmark"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"Rendered 1000 frames", result.stdout)
        for args in [["--benchmark", "--frames", "0"],
                     ["--frames", "0", "--benchmark"]]:
            with self.subTest(args=args):
                result = self.run_fetch(args)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b"positive frame count", result.stderr)
                self.assertEqual(result.stdout, b"")

    def test_duration_limits(self):
        for extra in [[], ["--frames", "0"], ["--frames", "2147483647"]]:
            with self.subTest(extra=extra):
                result = self.run_fetch(["--benchmark", "--duration", "0.1"] + extra)
                self.assertEqual(result.returncode, 0, result.stderr)
                match = re.search(rb"Rendered (\d+) frames in ([0-9.]+) seconds", result.stdout)
                self.assertIsNotNone(match)
                self.assertGreater(int(match[1]), 0)
                self.assertLess(int(match[1]), 2147483647)
                self.assertGreaterEqual(float(match[2]), 0.1)
                self.assertNotIn(b"\x1b", result.stdout)
        result = self.run_fetch(["--duration", "10", "--benchmark", "--frames", "1"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"Rendered 1 frames", result.stdout)

        with tempfile.TemporaryDirectory(prefix="fetch-test-") as directory:
            home = Path(directory)
            config_dir = home / ".config" / "fetch"
            config_dir.mkdir(parents=True)
            (config_dir / "logo.txt").write_text("@@@\n")
            result = self.run_fetch(["--no-info", "--duration", "0.1"], home,
                                    input_data=b"")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(b"\x1b[H", result.stdout)
            self.assertTrue(result.stdout.endswith(b"\x1b[?1002l\x1b[?1006l\x1b[?25h"))

    def test_invalid_duration(self):
        for value in ["0", "-1", "nan", "inf", "oops", "1s", "86401"]:
            with self.subTest(value=value):
                result = self.run_fetch(["--benchmark", "--duration", value])
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b"--duration", result.stderr)
                self.assertEqual(result.stdout, b"")
        result = self.run_fetch(["--duration"])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"requires an argument", result.stderr)

    def test_logo_arguments_without_shell(self):
        with tempfile.TemporaryDirectory(prefix="fetch-test-") as directory:
            home = Path(directory)
            stub = home / "fastfetch"
            log = home / "arguments.jsonl"
            marker = home / "injected"
            stub.write_text(
                f"#!{sys.executable}\n"
                "import json, os, sys\n"
                "with open(os.environ['FETCH_TEST_LOG'], 'a') as log:\n"
                "    log.write(json.dumps(sys.argv[1:]) + '\\n')\n"
                "if '--print-logos' in sys.argv:\n"
                "    print('gentoo:\\n@@@')\n"
                "elif os.environ['FETCH_TEST_MODE'] == 'colored':\n"
                "    print('@@@')\n"
                "elif os.environ['FETCH_TEST_MODE'] == 'large':\n"
                "    print('@@@\\n' * 2000)\n"
                "print('suppressed stderr', file=sys.stderr)\n"
            )
            stub.chmod(0o755)
            env = {"PATH": str(home), "FETCH_TEST_LOG": str(log),
                   "FETCH_TEST_MODE": "colored"}
            for name in ["gentoo", "name with spaces", "name'with\"quotes",
                         "x" * 300,
                         f"gentoo; printf injected > {marker}; #",
                         f"$(printf injected > {marker})",
                         f"`printf injected > {marker}`"]:
                with self.subTest(name=name):
                    log.unlink(missing_ok=True)
                    result = self.run_fetch(
                        ["--logo", name, "--no-info", "--frames", "1"], home, env
                    )
                    self.assertEqual(result.returncode, 0)
                    self.assertEqual(result.stderr, b"")
                    self.assertFalse(marker.exists())
                    self.assertEqual(
                        [json.loads(line) for line in log.read_text().splitlines()],
                        [["-c", "none", "-l", name, "-s", "break", "--pipe", "false"]],
                    )
            for mode in ["fallback", "large", "missing"]:
                with self.subTest(mode=mode):
                    log.unlink(missing_ok=True)
                    env["FETCH_TEST_MODE"] = mode
                    if mode == "missing":
                        stub.unlink()
                    result = self.run_fetch(
                        ["--logo", "gentoo", "--no-info", "--frames", "1"], home, env
                    )
                    self.assertEqual(result.returncode, 0)
                    self.assertEqual(result.stderr, b"")
                    self.assertTrue(result.stdout)
                    if mode == "fallback":
                        calls = [json.loads(line) for line in log.read_text().splitlines()]
                        self.assertEqual(len(calls), 2)
                        self.assertEqual(calls[1], ["-c", "none", "--print-logos"])

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

    def test_show_title(self):
        with tempfile.TemporaryDirectory(prefix="fetch-test-") as directory:
            home = Path(directory)
            config_dir = home / ".config" / "fetch"
            config_dir.mkdir(parents=True)
            (config_dir / "logo.txt").write_text("  XX  \nXXXXXX\n  XX  \n")
            config = config_dir / "config"
            for value in [None, "1", "yes", "true", "0", "no", "false", "0 (hide title)"]:
                for box in [0, 1]:
                    with self.subTest(value=value, box=box):
                        settings = f"box={box}\nseparator=~\ncustom_Test=visible\n"
                        if value is not None:
                            settings += f"show_title={value}\n"
                        config.write_text(settings)
                        result = self.run_fetch(
                            ["--frames", "1", "--height", "20", "--shading-chars", "X"], home
                        )
                        self.assertEqual(result.returncode, 0)
                        output = re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", result.stdout)
                        shown = value in [None, "1", "yes", "true"]
                        self.assertEqual(b"@" in output, shown)
                        self.assertEqual(b"~~" in output, shown)
                        self.assertIn(b"Test: visible", output)
                        self.assertEqual("╭".encode() in output, bool(box))

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
            (config_dir / "logo.txt").write_text("@@    \n@@@@@@\n  @@  \n")
            config = config_dir / "config"
            args = ["--no-info", "--frames", "3", "--no-color", "--speed", "20"]
            for axis, config_axis in [("x", "yz"), ("y", "xz"), ("z", "xy")]:
                with self.subTest(axis=axis):
                    config.write_text("")
                    baseline = self.run_fetch(args + [f"--rotate-{axis}"], home)
                    config.write_text(f"spin={config_axis}\n")
                    override = self.run_fetch(args + [f"--rotate-{axis}"], home)
                    self.assertEqual(baseline.returncode, 0)
                    self.assertEqual(override.returncode, 0)
                    self.assertEqual(override.stdout, baseline.stdout)

            config.write_text("")
            default_spin = self.run_fetch(args, home)
            rot_z_flag = self.run_fetch(args + ["--rotate-z"], home)
            config.write_text("spin=z\n")
            rot_z_cfg = self.run_fetch(args, home)
            self.assertEqual(rot_z_cfg.returncode, 0)
            self.assertEqual(rot_z_cfg.stdout, rot_z_flag.stdout)
            self.assertNotEqual(rot_z_cfg.stdout, default_spin.stdout)

            config.write_text("spin=xyz\n")
            rot_xyz_cfg = self.run_fetch(args, home)
            self.assertEqual(rot_xyz_cfg.returncode, 0)
            self.assertNotEqual(rot_xyz_cfg.stdout, default_spin.stdout)
            self.assertNotEqual(rot_xyz_cfg.stdout, rot_z_cfg.stdout)

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

    def test_truncated_utf8_info(self):
        with tempfile.TemporaryDirectory(prefix="fetch-test-") as directory:
            home = Path(directory)
            config_dir = home / ".config" / "fetch"
            config_dir.mkdir(parents=True)
            (config_dir / "logo.txt").write_text("# distro: gentoo\n@@@\n")
            (config_dir / "config").write_text("---\n" * 29 + "locale\n")
            for prefix in range(4):
                with self.subTest(prefix=prefix):
                    result = self.run_fetch(
                        ["--frames", "1"], home, {"LANG": "A" * prefix + "😀" * 250}
                    )
                    self.assertEqual(result.returncode, 0)
                    self.assertEqual(result.stderr, b"")
                    result.stdout.decode("utf-8")

    def test_terminal_controls_in_config(self):
        with tempfile.TemporaryDirectory(prefix="fetch-test-") as directory:
            home = Path(directory)
            config_dir = home / ".config" / "fetch"
            config_dir.mkdir(parents=True)
            (config_dir / "logo.txt").write_text("# distro: gentoo\n@@@\n")
            payload = (b"\x1b]52;c;YXVkaXQ=\x07\x1bPpayload\x1b\\"
                       b"\x9d52;c;YXVkaXQ=\x07\r")
            for box in [0, 1]:
                with self.subTest(box=box):
                    (config_dir / "config").write_bytes(
                        f"show_title=0\nbox={box}\ncustom_Test=".encode()
                        + "é█\x1b[31mvisible\x1b[0m".encode() + payload + b"\n"
                    )
                    result = self.run_fetch(["--frames", "1"], home)
                    self.assertEqual(result.returncode, 0)
                    self.assertEqual(result.stderr, b"")
                    self.assertIn("é█\x1b[31mvisible\x1b[0m".encode(), result.stdout)
                    for control in [b"\x1b]", b"\x1bP", b"\x07", b"\x9d52;", b"\r"]:
                        self.assertNotIn(control, result.stdout)
                    result.stdout.decode("utf-8")
            for key in ["size", "height", "shading_mode"]:
                with self.subTest(diagnostic=key):
                    (config_dir / "config").write_bytes(key.encode() + b"=" + payload + b"\n")
                    result = self.run_fetch(["--frames", "1"], home)
                    for control in [b"\x1b]", b"\x1bP", b"\x07", b"\x9d52;", b"\r"]:
                        self.assertNotIn(control, result.stderr)

    def test_color_validation(self):
        with tempfile.TemporaryDirectory(prefix="fetch-test-") as directory:
            home = Path(directory)
            config_dir = home / ".config" / "fetch"
            config_dir.mkdir(parents=True)
            (config_dir / "logo.txt").write_text("# distro: gentoo\n@@@\n")
            config = config_dir / "config"
            settings = "show_title=0\ncustom_Test=visible\n"
            config.write_text(settings)
            baseline = self.run_fetch(["--frames", "1"], home)
            for key in ["label_color", "logo_inner", "logo_outer"]:
                for value in ["31J", "31m\x1b]52;c;YXVkaXQ=\x07", "9" * 64, ""]:
                    with self.subTest(key=key, value=value):
                        config.write_text(settings + f"{key}={value}\n")
                        result = self.run_fetch(["--frames", "1"], home)
                        self.assertEqual(result.returncode, 0)
                        self.assertIn(key.encode(), result.stderr)
                        self.assertEqual(result.stdout, baseline.stdout)
            for value in ["red", "38;5;123", "38;2;1;2;3"]:
                with self.subTest(valid_color=value):
                    config.write_text(settings + f"label_color={value}\n")
                    result = self.run_fetch(["--frames", "1"], home)
                    code = "31" if value == "red" else value
                    self.assertEqual(result.returncode, 0)
                    self.assertEqual(result.stderr, b"")
                    self.assertIn(f"\x1b[1;{code}mTest".encode(), result.stdout)

    def test_escape_number_limits(self):
        with tempfile.TemporaryDirectory(prefix="fetch-test-") as directory:
            home = Path(directory)
            config_dir = home / ".config" / "fetch"
            config_dir.mkdir(parents=True)
            (config_dir / "logo.txt").write_bytes(
                b"# distro: gentoo\n\x1b[" + b"9" * 100 + b"m@@@\n"
            )
            args = ["--no-info", "--frames", "1"]
            result = self.run_fetch(args, home)
            self.assertEqual(result.returncode, 0)
            self.assertEqual(result.stderr, b"")
            for payload in [
                b"\x1b[<" + b"9" * 20 + b";1;1M",
                b"\x1b[<0;1;1M\x1b[<32;" + b"9" * 20 + b";1M",
                b"\x1b[<0;1;1M\x1b[<32;1;" + b"9" * 20 + b"M",
                b"\x1b[<0;",
                b"\x1b[<" + b"0" * 200,
                b"\x1b[<" + b"0" * 125,
            ]:
                with self.subTest(payload=payload):
                    result = self.run_fetch(args, home, input_data=payload)
                    self.assertEqual(result.returncode, 0)
                    self.assertEqual(result.stderr, b"")

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

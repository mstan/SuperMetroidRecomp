"""Malformed captures must fail before the helper starts the game."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("replay", Path(__file__).resolve().parents[1] / "tools/replay_diagnostic.py")
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)


class ReplayInspectorTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / "capture.replay"
        self.state = self.path.with_name(self.path.name + ".sav")
        self.state.write_bytes(b"checkpoint")
        self.header = "SMREPLAY 1\nBUILD version revision engine target\nROM 12345678\nMODE lle\n"
        self.checkpoint = f"S 0 10 {replay.digest(b'checkpoint'):08x}\n"
        self.input = "I 12 01000000 1f 4 120\n"
        self.complete = "H 13 00000001 00000002 00000003 00000004\nV 13 512 2.6666666666666665\n"

    def tearDown(self):
        self.temp.cleanup()

    def write(self, records):
        self.path.write_text(self.header + self.checkpoint + records)

    def test_complete_and_interrupted_input(self):
        self.write(self.input + self.complete)
        self.assertEqual(len(replay.inspect(self.path)), 8)
        self.write(self.input)
        self.assertEqual(len(replay.inspect(self.path)), 6)

    def test_damaged_missing_and_out_of_bounds_checkpoints(self):
        self.write(self.input + self.complete)
        self.state.write_bytes(b"corruption")
        with self.assertRaisesRegex(ValueError, "damaged"):
            replay.inspect(self.path)
        self.state.write_bytes(b"short")
        with self.assertRaisesRegex(ValueError, "extent"):
            replay.inspect(self.path)
        self.state.unlink()
        with self.assertRaises(OSError):
            replay.inspect(self.path)

    def test_discontinuity_requires_checkpoint(self):
        self.write(self.input + self.complete + self.input)
        with self.assertRaisesRegex(ValueError, "discontinuity"):
            replay.inspect(self.path)
        self.write(self.input + self.complete + self.checkpoint + self.input + self.complete)
        replay.inspect(self.path)

    def test_bad_settings_or_missing_execution(self):
        for value in ((self.input+self.complete).replace("512", "513"),
                      (self.input+self.complete).replace("2.6666666666666665", "nan"),
                      self.input.replace("120", "123"), self.complete, "", "I broken\n"):
            self.write(value)
            with self.assertRaises(ValueError):
                replay.inspect(self.path)

    def test_simulation_viewport(self):
        self.header = self.header.replace("SMREPLAY 1", "SMREPLAY 2")
        for extra in (0, 43, 96, 213, 214):
            self.write(self.input.rstrip() + f" {extra}\n" + self.complete)
            replay.inspect(self.path)
        for extra in (-1, 215, 65535):
            self.write(self.input.rstrip() + f" {extra}\n" + self.complete)
            with self.assertRaisesRegex(ValueError, "simulation viewport"):
                replay.inspect(self.path)

    def test_mods_identity(self):
        self.header = self.header.replace("SMREPLAY 1", "SMREPLAY 3").replace(
            "MODE lle", "MODS 0badf00d\nMODE lle")
        self.write(self.input.rstrip() + " 0\n" + self.complete)
        replay.inspect(self.path)
        self.header = self.header.replace("MODS 0badf00d\n", "")
        self.write(self.input.rstrip() + " 0\n" + self.complete)
        with self.assertRaisesRegex(ValueError, "header"):
            replay.inspect(self.path)


if __name__ == "__main__":
    unittest.main()

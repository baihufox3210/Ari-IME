#!/usr/bin/env python3
"""Regression tests for the VChewing/XiaoMai portable dictionary converter."""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
import sys
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

import convert_vchewing_dictionary as converter  # noqa: E402


class ConverterTests(unittest.TestCase):
    def test_hyphenated_syllables_and_fourth_tone_round_trip(self) -> None:
        result = converter.convert_text(
            "妳好\tㄋㄧˇ-ㄏㄠˇ\n測試\tㄘㄜˋ-ㄕˋ\n"
        )

        self.assertEqual(result.invalid, [])
        self.assertEqual(
            [(entry.phrase, entry.reading) for entry in result.entries],
            [("妳好", "ㄋㄧˇ ㄏㄠˇ"), ("測試", "ㄘㄜˋ ㄕˋ")],
        )
        self.assertIn("妳好\tㄋㄧˇ ㄏㄠˇ", converter.render(result.entries))

    def test_normalizes_light_tone_and_reports_untransferred_weight(self) -> None:
        result = converter.convert_text("我們\tㄨㄛˇ-ㄇㄣ˙\t-1.5\n")

        self.assertEqual(result.invalid, [])
        self.assertEqual(result.weighted_count, 1)
        self.assertEqual(result.entries[0].reading, "ㄨㄛˇ ㄇㄣ·")

    def test_rejects_pinyin_unsegmented_readings_and_empty_syllables(self) -> None:
        result = converter.convert_text(
            "你好\tni3-hao3\n"
            "你好\tㄋㄧˇㄏㄠˇ\n"
            "你好\tㄋㄧˇ--ㄏㄠˇ\n"
        )

        self.assertEqual(result.entries, [])
        self.assertEqual(len(result.invalid), 3)

    def test_deduplicates_rows_and_rejects_invalid_weights(self) -> None:
        result = converter.convert_text(
            "妳好\tㄋㄧˇ-ㄏㄠˇ\n"
            "妳好\tㄋㄧˇ ㄏㄠˇ\t-2.0\n"
            "妳好\tㄋㄧˇ-ㄏㄠˇ\t-10\n"
        )

        self.assertEqual(len(result.entries), 1)
        self.assertEqual(result.duplicate_count, 1)
        self.assertEqual(result.weighted_count, 1)
        self.assertEqual(len(result.invalid), 1)

    def test_invalid_input_does_not_write_partial_output(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ari-vchewing-test-") as temp:
            root = Path(temp)
            source = root / "source.tsv"
            destination = root / "ari.tsv"
            source.write_text(
                "妳好\tㄋㄧˇ-ㄏㄠˇ\n你好\tni3-hao3\n", encoding="utf-8"
            )
            errors = StringIO()
            with redirect_stderr(errors), redirect_stdout(StringIO()):
                status = converter.main([str(source), str(destination)])

            self.assertEqual(status, 2)
            self.assertFalse(destination.exists())
            self.assertIn("No output written", errors.getvalue())

    def test_skip_invalid_requires_force_to_replace_existing_output(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ari-vchewing-test-") as temp:
            root = Path(temp)
            source = root / "source.tsv"
            destination = root / "ari.tsv"
            source.write_text(
                "妳好\tㄋㄧˇ-ㄏㄠˇ\n你好\tni3-hao3\n", encoding="utf-8"
            )
            with redirect_stderr(StringIO()), redirect_stdout(StringIO()):
                first_status = converter.main(
                    [str(source), str(destination), "--skip-invalid"]
                )
                overwrite_status = converter.main(
                    [str(source), str(destination), "--skip-invalid"]
                )
                force_status = converter.main(
                    [str(source), str(destination), "--skip-invalid", "--force"]
                )

            self.assertEqual(first_status, 2)
            self.assertEqual(overwrite_status, 1)
            self.assertEqual(force_status, 2)
            self.assertIn("妳好\tㄋㄧˇ ㄏㄠˇ", destination.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main(verbosity=2)

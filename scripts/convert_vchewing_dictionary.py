#!/usr/bin/env python3
"""Convert VChewing/XiaoMai-style Bopomofo TSV entries to Ari IME format.

The converter deliberately handles only Bopomofo readings. Pinyin rows are
reported instead of guessed, because romanization can be ambiguous without the
source input method's selected scheme and normalization rules.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
import os
from pathlib import Path
import sys
import tempfile
import unicodedata


ARI_HEADER = "# Ari IME user dictionary v1"
ALLOWED_READING = {
    *(chr(codepoint) for codepoint in range(0x3105, 0x312A)),
    "ˇ", "ˊ", "ˋ", "˙", "·", "ˉ", "ˌ", "˗",
}
LIGHT_TONE_ALIASES = {"˙": "·"}


@dataclass(frozen=True)
class ConvertedEntry:
    phrase: str
    reading: str
    line: int


@dataclass
class Conversion:
    entries: list[ConvertedEntry]
    invalid: list[tuple[int, str]]
    duplicate_count: int = 0
    weighted_count: int = 0


def split_columns(line: str) -> list[str]:
    if "\t" in line:
        return [field.strip() for field in line.split("\t")]
    return line.split()


def convert_reading(source: str) -> str:
    normalized = unicodedata.normalize("NFC", source).strip()
    if (
        not normalized
        or normalized.startswith("-")
        or normalized.endswith("-")
        or "--" in normalized
    ):
        raise ValueError("reading has an empty syllable around a hyphen")
    syllables = normalized.replace("-", " ").split()
    syllables = [
        "".join(LIGHT_TONE_ALIASES.get(char, char) for char in syllable)
        for syllable in syllables
    ]
    if not syllables or any(
        not syllable or any(char not in ALLOWED_READING for char in syllable)
        for syllable in syllables
    ):
        raise ValueError(
            "reading must contain Bopomofo symbols, not pinyin or layout keys"
        )
    return " ".join(syllables)


def parse_weight(value: str, line_number: int) -> Decimal:
    try:
        weight = Decimal(value)
    except InvalidOperation as error:
        raise ValueError(
            f"weight must be numeric in [-9.5, 0] on line {line_number}"
        ) from error
    if not weight.is_finite() or weight < Decimal("-9.5") or weight > Decimal("0"):
        raise ValueError(f"weight must be in [-9.5, 0] on line {line_number}")
    return weight


def convert_text(source: str) -> Conversion:
    result = Conversion(entries=[], invalid=[])
    seen: set[tuple[str, str]] = set()
    for line_number, raw_line in enumerate(source.splitlines(), start=1):
        line = raw_line.strip("\r")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        fields = split_columns(line)
        if len(fields) not in (2, 3):
            result.invalid.append(
                (line_number, "expected phrase, reading, and optional weight")
            )
            continue
        phrase, source_reading = fields[:2]
        if not phrase or "\t" in phrase:
            result.invalid.append((line_number, "phrase is empty or contains a tab"))
            continue
        if len(fields) == 3:
            try:
                parse_weight(fields[2], line_number)
            except ValueError as error:
                result.invalid.append((line_number, str(error)))
                continue
            result.weighted_count += 1
        try:
            reading = convert_reading(source_reading)
        except ValueError as error:
            result.invalid.append((line_number, str(error)))
            continue
        if len(reading.split(" ")) != len(phrase):
            result.invalid.append(
                (
                    line_number,
                    "separate one Bopomofo syllable per phrase character "
                    "with hyphens or spaces",
                )
            )
            continue
        key = (phrase, reading)
        if key in seen:
            result.duplicate_count += 1
            continue
        seen.add(key)
        result.entries.append(ConvertedEntry(phrase, reading, line_number))
    return result


def render(entries: list[ConvertedEntry]) -> str:
    lines = [
        ARI_HEADER,
        "# Converted from VChewing/XiaoMai-style Bopomofo TSV;",
        "# source weights are not transferred.",
        "# One entry per line: phrase<TAB>Bopomofo syllables separated by spaces",
    ]
    lines.extend(f"{entry.phrase}\t{entry.reading}" for entry in entries)
    return "\n".join(lines) + "\n"


def write_output(path: Path, content: str, force: bool) -> None:
    if path.exists() and not force:
        raise FileExistsError(
            f"output already exists: {path} (pass --force to replace it)"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as output:
            output.write(content)
        os.replace(temporary_name, path)
    except BaseException:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="VChewing/XiaoMai-style TSV file")
    parser.add_argument("output", type=Path, nargs="?", help="Ari TSV destination")
    parser.add_argument("--dry-run", action="store_true", help="validate and report without writing")
    parser.add_argument("--skip-invalid", action="store_true", help="write valid rows and report rejected rows")
    parser.add_argument("--force", action="store_true", help="replace an existing output file")
    args = parser.parse_args(argv)

    if args.dry_run and args.output:
        parser.error("omit OUTPUT with --dry-run")
    if not args.dry_run and not args.output:
        parser.error("OUTPUT is required unless --dry-run is used")
    if args.skip_invalid and args.dry_run:
        parser.error("--skip-invalid has no effect with --dry-run")

    try:
        source = args.source.read_text(encoding="utf-8-sig")
    except (OSError, UnicodeError) as error:
        print(f"convert-vchewing: cannot read {args.source}: {error}", file=sys.stderr)
        return 1

    conversion = convert_text(source)
    print(
        f"valid={len(conversion.entries)} duplicates={conversion.duplicate_count} "
        f"source_weights_not_transferred={conversion.weighted_count} "
        f"rejected={len(conversion.invalid)}"
    )
    for line_number, reason in conversion.invalid:
        print(f"line {line_number}: {reason}", file=sys.stderr)

    if conversion.invalid and not args.skip_invalid:
        print("No output written; fix the rejected rows or pass --skip-invalid.", file=sys.stderr)
        return 2
    if args.dry_run:
        return 0
    try:
        write_output(args.output, render(conversion.entries), args.force)
    except OSError as error:
        print(f"convert-vchewing: {error}", file=sys.stderr)
        return 1
    print(f"wrote {len(conversion.entries)} entries to {args.output}")
    return 0 if not conversion.invalid else 2


if __name__ == "__main__":
    raise SystemExit(main())

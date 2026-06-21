#!/usr/bin/env python3
"""
Generate skeleton .ts files for RMG translations.

Takes the Russian .ts file (which has the most complete coverage)
and produces parallel skeleton files for each requested language,
where every <translation> tag is replaced with an empty
<translation type="unfinished"></translation>.

The resulting files are valid Qt translation sources that:
  - lrelease can compile to .qm
  - Qt Linguist can open for translators to fill in
  - qt6_add_translations() picks up at build time

Usage:
    python3 generate_skeleton_ts.py
"""

import os
import re
import sys
from pathlib import Path

# Map locale code -> (filename suffix, language attribute for <TS>)
# These match the languages GLideN64 ships translations for, plus
# Russian which we already have as a "complete" file.
LANGUAGES = [
    ("de",    "de_DE"),
    ("fr",    "fr_FR"),
    ("es",    "es_ES"),
    ("it",    "it_IT"),
    ("pt_BR", "pt_BR"),
    ("ja",    "ja_JP"),
    ("zh",    "zh_CN"),
    ("pl",    "pl_PL"),
    ("ko",    "ko_KR"),
]

SOURCE_FILE = "RMG_ru.ts"
OUTPUT_DIR  = "."  # same directory as the source file

def strip_translations(xml_text: str, target_lang: str, filename_code: str) -> str:
    """
    Given the contents of a .ts file, return a new .ts file
    where:
      - the language="..." attribute on <TS> is set to target_lang
      - every <translation>...</translation> body is replaced with
        an empty body and a type="unfinished" attribute
      - the leading comment header is rewritten to describe this skeleton file

    filename_code is the part between "RMG_" and ".ts" in the output
    filename, e.g. "de" or "pt_BR". It's used to render the lupdate
    hint at the bottom of the header comment.
    """
    # Replace the language attribute on <TS ...>
    text = re.sub(
        r'<TS\s+([^>]*?)language="[^"]*"([^>]*)>',
        lambda m: f'<TS {m.group(1)}language="{target_lang}"{m.group(2)}>',
        xml_text,
        count=1,
    )

    # Replace every <translation>...</translation> with unfinished empty body.
    text = re.sub(
        r'<translation(\s[^>]*)?>[^<]*</translation>',
        '<translation type="unfinished"></translation>',
        text,
    )

    # Rewrite the leading comment header so the file describes itself,
    # not the Russian file it was derived from.
    new_header = (
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<!DOCTYPE TS>\n"
        "<!--\n"
        "  RMG " + target_lang + " translation (skeleton).\n"
        "  Auto-generated from RMG_ru.ts by generate_skeleton_ts.py.\n"
        "\n"
        "  All translations are currently marked as type=\"unfinished\",\n"
        "  which means Qt will fall back to the source English strings\n"
        "  for every message until a translator fills them in (e.g. via\n"
        "  Qt Linguist). The file is still picked up by qt6_add_translations()\n"
        "  at build time and produces a valid .qm file, so the language\n"
        "  appears in the Settings dialog immediately.\n"
        "\n"
        "  To regenerate/refresh source strings from the code:\n"
        "      lupdate Source/RMG -ts Source/RMG/Translations/RMG_" + filename_code + ".ts\n"
        "-->\n"
    )
    # Replace from the start of file up to (but not including) <TS
    end_of_header = text.index('<TS')
    text = new_header + text[end_of_header:]

    return text


def main() -> int:
    script_dir = Path(__file__).resolve().parent
    source_path = script_dir / SOURCE_FILE

    if not source_path.exists():
        print(f"ERROR: source file {source_path} not found", file=sys.stderr)
        return 1

    source_xml = source_path.read_text(encoding="utf-8")

    for code, lang_attr in LANGUAGES:
        out_path = script_dir / f"RMG_{code}.ts"
        skeleton = strip_translations(source_xml, lang_attr, code)
        out_path.write_text(skeleton, encoding="utf-8")
        print(f"Wrote {out_path}  ({len(skeleton)} bytes, language={lang_attr})")

    return 0


if __name__ == "__main__":
    sys.exit(main())

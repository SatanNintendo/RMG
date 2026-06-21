#!/usr/bin/env python3
"""
Create a skeleton .ts file for a NEW language, based on RMG_en.ts.

Usage:
    python3 generate_skeleton_ts.py <locale_code> [<language_attr>]

Examples:
    python3 generate_skeleton_ts.py uk uk_UK    # Ukrainian
    python3 generate_skeleton_ts.py fr fr_FR    # French
    python3 generate_skeleton_ts.py pt_BR pt_BR # Brazilian Portuguese

This will create RMG_<locale_code>.ts with:
  - language attribute set to <language_attr> (defaults to <locale_code>)
  - every message from RMG_en.ts copied over
  - <translation> body left identical to <source> (English fallback)
    but marked type="unfinished" so the new language shows up in
    Settings immediately while a translator fills it in via Qt Linguist.

After running this script, open the new .ts file in Qt Linguist to
fill in the actual translations, then rebuild RMG.
"""

import re
import sys
from pathlib import Path


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    locale_code = sys.argv[1]
    language_attr = sys.argv[2] if len(sys.argv) >= 3 else locale_code

    script_dir = Path(__file__).resolve().parent
    source_path = script_dir / "RMG_en.ts"
    output_path = script_dir / f"RMG_{locale_code}.ts"

    if not source_path.exists():
        print(f"ERROR: source file {source_path} not found", file=sys.stderr)
        return 1

    source_xml = source_path.read_text(encoding="utf-8")

    # Replace the language attribute on <TS ...>
    text = re.sub(
        r'<TS\s+([^>]*?)language="[^"]*"([^>]*)>',
        lambda m: f'<TS {m.group(1)}language="{language_attr}"{m.group(2)}>',
        source_xml,
        count=1,
    )

    # Mark every <translation> as type="unfinished" (keeping the body).
    # The body stays as the English source so Qt Linguist shows the
    # original text as a starting point for the translator.
    text = re.sub(
        r'<translation(\s[^>]*)?>',
        '<translation type="unfinished">',
        text,
    )

    output_path.write_text(text, encoding="utf-8")
    print(f"Wrote {output_path}  ({len(text)} bytes, language={language_attr})")
    print(f"Open it in Qt Linguist to start translating.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

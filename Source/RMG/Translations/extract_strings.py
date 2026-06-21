#!/usr/bin/env python3
"""
Extract all translatable strings from RMG source code.

Scans:
  - All .ui files in Source/RMG/, Source/RMG-Input/, Source/RMG-Audio/
    (extracts <class> for context, <string> for messages)
  - All .cpp files in the same directories
    (extracts tr("...") calls, using the C++ class name as context)

Outputs a sorted, deduplicated list of (context, source_string) pairs
to stdout, one per line, in the format:
    CONTEXT \t SOURCE_STRING

This is used to generate the complete RMG_ru.ts file.
"""

import os
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from collections import defaultdict


def extract_ui_strings(ui_path):
    """Extract (context, [strings]) from a .ui file.
    Context = the <class> element value.
    Strings = all <string>...</string> element values (text only).
    """
    tree = ET.parse(ui_path)
    root = tree.getroot()

    # The <class>Foo</class> element is a direct child of <ui>
    class_elem = root.find("class")
    if class_elem is None or not class_elem.text:
        return None, []

    context = class_elem.text.strip()

    # Collect all <string> elements anywhere in the tree.
    # We want the text content; skip empty strings and pure-whitespace.
    strings = []
    for string_elem in root.iter("string"):
        text = string_elem.text
        if text and text.strip():
            strings.append(text.strip())

    return context, strings


def extract_cpp_tr_strings(cpp_path):
    """Extract tr("...") calls from a C++ file.

    Returns a list of (context, source_string) pairs where context is
    the enclosing C++ class name (guessed from the file path and the
    class definition found in the file).
    """
    text = Path(cpp_path).read_text(encoding="utf-8", errors="replace")

    # Find the class name: look for "class Foo : public" or "class Foo {"
    # near the top of the file. If none found, use the filename stem.
    class_name = Path(cpp_path).stem
    m = re.search(r'^class\s+(\w+)\s*:', text, re.MULTILINE)
    if m:
        class_name = m.group(1)

    # Find tr("...") calls.
    # This regex handles:
    #   tr("hello")
    #   tr("hello", "world")  -- only captures first arg
    #   tr("hello \"world\"") -- handles escaped quotes
    # It does NOT handle multi-line strings or string concatenation,
    # which are rare in this codebase.
    results = []
    # Match tr( followed by a quoted string
    # The string can contain escaped quotes \"
    pattern = r'\btr\s*\(\s*"((?:[^"\\]|\\.)*)"\s*[,)]'
    for m in re.finditer(pattern, text):
        raw = m.group(1)
        # Unescape: \" -> ", \\ -> \, \n -> newline etc.
        # We keep the raw form for .ts (Qt handles XML escaping)
        results.append((class_name, raw))

    return results


def main():
    repo_root = Path(__file__).resolve().parent.parent.parent.parent  # RMG repo root

    # Directories to scan
    scan_dirs = [
        repo_root / "Source" / "RMG",
        repo_root / "Source" / "RMG-Input",
        repo_root / "Source" / "RMG-Audio",
    ]

    # context -> set of source strings
    all_strings = defaultdict(set)

    for scan_dir in scan_dirs:
        if not scan_dir.exists():
            continue

        # Process .ui files
        for ui_file in scan_dir.rglob("*.ui"):
            context, strings = extract_ui_strings(ui_file)
            if context:
                for s in strings:
                    all_strings[context].add(s)

        # Process .cpp files (for tr() calls)
        for cpp_file in scan_dir.rglob("*.cpp"):
            pairs = extract_cpp_tr_strings(cpp_file)
            for ctx, s in pairs:
                all_strings[ctx].add(s)

    # Output: one line per (context, string), sorted
    for context in sorted(all_strings.keys()):
        for s in sorted(all_strings[context]):
            # Tab-separated for easy parsing
            print(f"{context}\t{s}")


if __name__ == "__main__":
    main()

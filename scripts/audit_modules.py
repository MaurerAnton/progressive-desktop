#!/usr/bin/env python3
"""scripts/audit_modules.py — classify progressive_native modules into tiers.

Tiers:
  A — Real hand-written implementations (parse real Matrix JSON, real logic).
  B — "auto-generated real implementations" — templated echo, ~434 lines,
      parses id/type/value/ts/n/flag and echoes them back. Looks real, isn't.
  C — Pure hash/size stubs (5-30 lines, no logic, just echoes {"ok":true,"sz":N}).
  D — Android-only JNI glue (requires JNIEnv*, can't compile on desktop).

Usage:
    ./scripts/audit_modules.py            # summary
    ./scripts/audit_modules.py --csv      # CSV to stdout
    ./scripts/audit_modules.py --verbose  # per-file reasons
"""
import argparse
import csv
import os
import re
import sys
from pathlib import Path

# Resolve the submodule root regardless of where the script is invoked from.
SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
CPP_ROOT = REPO_ROOT / "third_party" / "progressive-android-experiments" / "progressive" / "src" / "main" / "cpp"
SRC_DIR = CPP_ROOT / "src"

# --- Classification rules ---

JNI_INCLUDE_RE = re.compile(r'#include\s+<jni\.h>')
JNI_USAGE_RE = re.compile(r'\b(JNIEnv|JNIEXPORT|JNICALL|jobject|jclass|jstring|jint|jlong|jboolean|jbyteArray)\b')
ANDROID_LOG_RE = re.compile(r'#include\s+<android/log\.h>')

# Tier B: auto-generated template marker
AUTOGEN_RE = re.compile(r'auto-generated\s+real\s+implementations', re.IGNORECASE)
# Tier B: the canonical template parses these 6 fields in this order
TEMPLATE_FIELDS_RE = re.compile(r'parseJsonStringValue\(json,\s*"id"')
TEMPLATE_FIELDS_RE2 = re.compile(r'parseJsonStringValue\(json,\s*"type"')
TEMPLATE_FIELDS_RE3 = re.compile(r'parseJsonInt64Value\(json,\s*"ts"')

# Tier C: hash echo — every function hashes the input and returns {"fn":...,"h":hash}
HASH_CALL_RE = re.compile(r'std::hash<std::string>')
# Tier C: size echo — every function returns {"ok":true,"sz":N} (no hash, just input size)
SIZE_CALL_RE = re.compile(r'json\.size\(\)')

# Broken-header detector: headers like `std::string int(...)`, `std::string struct(...)`,
# `std::string bool(...)`, `std::string std(...)`, `std::string vector(...)` are
# auto-generated garbage — keywords/C++ identifiers as function names. These break
# any TU that includes the header, even if the .cpp body is real.
BROKEN_HEADER_RE = re.compile(
    r'std::string\s+(int|bool|struct|class|std|vector|string|char|short|long|float|double|unsigned|signed|auto|void)\s*\('
)

def classify(path: Path) -> tuple[str, str]:
    """Return (tier, reason) for a single .cpp file."""
    try:
        text = path.read_text(errors='replace')
    except OSError as e:
        return ('X', f'read-error: {e}')

    lines = text.splitlines()
    line_count = len(lines)

    # --- Tier D: JNI glue (heavy JNI usage, not just android/log.h) ---
    if JNI_INCLUDE_RE.search(text):
        jni_hits = len(JNI_USAGE_RE.findall(text))
        if jni_hits >= 3:
            return ('D', f'jni.h + {jni_hits} JNI type refs')

    name = path.name
    if name.startswith('jni_bridge') or re.match(r'jni_stubs_part\d+\.cpp$', name):
        return ('D', 'jni bridge/stubs by filename')

    # --- Tier C: broken header (auto-generated garbage declarations) ---
    # Check the corresponding .hpp file in include/progressive/.
    # If the header is broken, the .cpp can't be safely included/linked.
    stem = path.stem  # e.g. "poll_vote_utils" from "poll_vote_utils.cpp"
    header_path = path.parent.parent / 'include' / 'progressive' / f'{stem}.hpp'
    if header_path.is_file():
        try:
            header_text = header_path.read_text(errors='replace')
            broken_hits = len(BROKEN_HEADER_RE.findall(header_text))
            if broken_hits > 0:
                return ('C', f'broken header ({broken_hits} garbage decls in {stem}.hpp)')
        except OSError:
            pass

    # --- Tier C: hash echo stub ---
    # Stub files call std::hash<std::string>{}(json) in every function.
    # Real modules never do this. Threshold: >5 hash calls = stub.
    hash_count = len(HASH_CALL_RE.findall(text))
    if hash_count > 5:
        return ('C', f'hash echo stub ({hash_count} std::hash calls, {line_count} lines)')

    # --- Tier C: size echo stub (short file, returns {"ok":true,"sz":N}) ---
    # e.g. emoji_utils.cpp (7 lines), event_filter_utils.cpp (5 lines).
    # No std::hash, but every function returns the input size as "sz".
    # Signal: short file (<50 lines) with high density of json.size() calls
    # and the "ok":true echo pattern.
    if line_count < 50:
        size_count = len(SIZE_CALL_RE.findall(text))
        if size_count >= 2 and '"ok":true' in text:
            return ('C', f'size echo stub ({size_count} json.size() calls, {line_count} lines)')

    # --- Tier B: auto-generated template ---
    if AUTOGEN_RE.search(text):
        return ('B', 'auto-generated template header')

    # Heuristic: the template pattern always parses id+type+ts in sequence
    if (TEMPLATE_FIELDS_RE.search(text)
            and TEMPLATE_FIELDS_RE2.search(text)
            and TEMPLATE_FIELDS_RE3.search(text)
            and 400 <= line_count <= 450):
        return ('B', f'template pattern ({line_count} lines, id/type/ts fields)')

    # --- Tier A: real implementation ---
    return ('A', f'real ({line_count} lines)')


def main():
    parser = argparse.ArgumentParser(description='Classify progressive_native modules into tiers A/B/C/D.')
    parser.add_argument('--csv', action='store_true', help='Output CSV instead of summary')
    parser.add_argument('--tsv', action='store_true', help='Output TSV (tab-separated, no quoting) instead of summary')
    parser.add_argument('--verbose', action='store_true', help='Print per-file classification')
    parser.add_argument('--src-dir', default=str(SRC_DIR), help=f'Source directory (default: {SRC_DIR})')
    args = parser.parse_args()

    src_dir = Path(args.src_dir)
    if not src_dir.is_dir():
        print(f'!! Source directory not found: {src_dir}', file=sys.stderr)
        print('   Did you run: git submodule update --init --recursive', file=sys.stderr)
        return 1

    cpp_files = sorted(src_dir.glob('*.cpp'))
    if not cpp_files:
        print(f'!! No .cpp files in {src_dir}', file=sys.stderr)
        return 1

    results = []
    for f in cpp_files:
        tier, reason = classify(f)
        results.append((f.name, tier, reason, f.stat().st_size))

    if args.csv:
        w = csv.writer(sys.stdout)
        w.writerow(['file', 'tier', 'reason', 'bytes'])
        for row in results:
            w.writerow(row)
        return 0

    if args.tsv:
        # TSV — no quoting, tab-delimited. Easier to parse from CMake.
        for name, tier, reason, size in results:
            # reason may contain commas but not tabs (we control it)
            reason_clean = reason.replace('\t', ' ')
            sys.stdout.write(f'{name}\t{tier}\t{reason_clean}\t{size}\n')
        return 0

    # Summary
    from collections import Counter
    tier_counts = Counter(r[1] for r in results)
    tier_bytes = {}
    for name, tier, reason, size in results:
        tier_bytes[tier] = tier_bytes.get(tier, 0) + size

    total = len(results)
    print(f'progressive_native module audit')
    print(f'  source dir: {src_dir}')
    print(f'  total .cpp files: {total}')
    print()
    print(f'  {"Tier":<6}{"Count":>8}{"%":>8}{"Bytes":>12}  Description')
    print(f'  {"-"*5:<6}{"-"*7:>8}{"-"*6:>8}{"-"*11:>12}  {"-"*40}')
    descriptions = {
        'A': 'Real hand-written implementations',
        'B': 'Auto-generated templates (echo)',
        'C': 'Pure stubs (hash/size echo)',
        'D': 'Android JNI glue (excluded on desktop)',
        'X': 'Error',
    }
    for tier in ['A', 'B', 'C', 'D', 'X']:
        count = tier_counts.get(tier, 0)
        if count == 0:
            continue
        pct = 100.0 * count / total
        size = tier_bytes.get(tier, 0)
        size_str = f'{size:,}' if size < 1024*1024 else f'{size/1024/1024:.1f}M'
        print(f'  {tier:<6}{count:>8}{pct:>7.1f}%{size_str:>12}  {descriptions.get(tier, "?")}')
    print()

    if args.verbose:
        for tier in ['A', 'B', 'C', 'D']:
            tier_files = sorted([r for r in results if r[1] == tier])
            if not tier_files:
                continue
            print(f'--- Tier {tier} ({len(tier_files)} files) ---')
            for name, _, reason, size in tier_files:
                print(f'  {name:<50} {size:>8} bytes  ({reason})')
            print()

    return 0


if __name__ == '__main__':
    sys.exit(main())

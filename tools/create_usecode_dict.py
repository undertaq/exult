#!/usr/bin/env python3
"""
Exult Usecode Dialogue Translation Dictionary Generator
=======================================================
Reads extracted usecode dialogue text (from dump_usecode_text.py) and translates
English strings to Traditional Chinese via any OpenAI-compatible API.
Outputs a tab-separated dictionary for the runtime string replacement engine:

    <PATCH>/usecode_translations.txt

Format:
    English source text<tab>Chinese translation text

Usage:
  ── NVIDIA NIM (default) ──
    set NVIDIA_API_KEY=nvapi-...
    python tools/create_usecode_dict.py --input dialogue_dump.txt

  ── Local server (LM Studio, Ollama, vLLM, etc.) ──
    python tools/create_usecode_dict.py --input dialogue_dump.txt ^
        --base-url http://localhost:1234/v1 --api-key ""

  ── Other OpenAI-compatible API ──
    python tools/create_usecode_dict.py --input dialogue_dump.txt ^
        --base-url https://api.example.com/v1 --api-key sk-... --model gpt-4o

Options:
  --input FILE       Input dumped dialogue text (default: bg_usecode_dialogue.txt)
  --output FILE      Output translation dictionary (default: usecode_translations.txt)
  --api-key KEY      API key (default: env NVIDIA_API_KEY; pass "" for local servers)
  --base-url URL     API endpoint (default: https://integrate.api.nvidia.com/v1)
  --model NAME       Model name (default: meta/llama-3.1-70b-instruct)
  --batch-size N     Strings per API call (default: 15)
  --min-length N     Minimum string length to translate (default: 4)
  --dry-run          Show what would be translated without calling API
  --resume           Skip strings already present in output file
  --no-skip-names    Translate proper names too (default: skip names)
  --text-only        Skip strings with formatting markers (~, *, %)
"""

import argparse
import os
import re
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple


# ── Input Parsing ──────────────────────────────────────────────────────────

# Pattern for dumped dialogue format: "  NNN.  text"
DUMP_LINE_RE = re.compile(r'^\s*\d+\.\s+(.*)')


def parse_dump_file(filepath: str) -> List[str]:
    """
    Parse a dumped dialogue text file (from dump_usecode_text.py).
    Returns a list of unique English strings.
    """
    strings: List[str] = []
    seen: Set[str] = set()

    # Read all lines
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    # Skip header comments (lines starting with #)
    lines = []
    for line in content.splitlines():
        stripped = line.strip()
        if stripped.startswith('#'):
            continue
        lines.append(line)

    for line in lines:
        m = DUMP_LINE_RE.match(line)
        if m:
            text = m.group(1).strip()
            # Skip empty / single-char / pure numeric
            if len(text) < 2:
                continue
            if text not in seen:
                seen.add(text)
                strings.append(text)

    print(f"  Parsed {len(strings)} unique strings from dump file")
    return strings


def read_existing_dict(filepath: str) -> Dict[str, str]:
    """
    Read an existing translation dictionary.
    Returns: { source_text: target_text }
    """
    existing: Dict[str, str] = {}
    if not os.path.exists(filepath):
        return existing

    with open(filepath, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n\r')
            if not line or line.startswith('#'):
                continue
            tab = line.find('\t')
            if tab < 0:
                continue
            src = line[:tab].strip()
            tgt = line[tab + 1:].strip()
            if src and tgt:
                existing[src] = tgt

    print(f"  Read {len(existing)} existing translations from {filepath}")
    return existing


# ── String Filtering ───────────────────────────────────────────────────────

HAS_FORMATTING_RE = re.compile(r'[~*%]')


def is_formatting_only(s: str) -> bool:
    """Is the string just formatting characters?"""
    cleaned = s.replace('~', '').replace('*', '').replace('%', '')
    cleaned = re.sub(r'%[sdixc%]', '', cleaned)
    return len(cleaned.strip()) == 0


def is_proper_name(s: str) -> bool:
    """
    Heuristic: detect strings that look like NPC names rather than dialogue.
    """
    # Single capitalized word (name) — but could also be "Hello"
    words = s.split()
    if len(words) == 1 and words[0][0].isupper() and len(words[0]) <= 20:
        # If it's a single capitalized word, it's likely a name or title
        return True
    # Short uppercase strings are codes/names
    if len(s) <= 15 and s.isupper() and ' ' not in s:
        return True
    return False


# ── LLM Translation ────────────────────────────────────────────────────────


def translate_batch(
    texts: List[Tuple[int, str]],
    api_key: str,
    base_url: str,
    model: str,
    max_retries: int = 3,
) -> List[Tuple[int, str]]:
    """
    Translate a batch of (index, english_text) pairs via OpenAI-compatible API.
    Returns: [(index, chinese_text), ...]
    """
    if not texts:
        return []

    try:
        from openai import OpenAI
    except ImportError:
        print("\n  ERROR: 'openai' package not installed. Run: pip install openai")
        sys.exit(1)

    client = OpenAI(api_key=api_key, base_url=base_url)

    # Build translation prompt
    text_block = "\n".join(f"[{i}] {t}" for i, t in texts)

    prompt = f"""You are a game localization expert translating Ultima VII (1993) dialogue and game text from English to Traditional Chinese (zh-TW).

These strings come from the game's usecode (scripted dialogue, books, signs, item descriptions). They may contain special formatting markers that MUST be preserved EXACTLY:

  ~   = page break in conversation (like a new paragraph)
  *   = player must click to continue
  %s, %d, %i, %x, %c, %% = C format specifiers (preserve exactly)
  ~~  = double page break
  --  = speaker attribution prefix

CRITICAL RULES:
1. Preserve ALL formatting characters EXACTLY as they appear: ~, *, %s, %d, --, ~~, etc.
2. Keep proper names (people, cities, places, character names) in English — do NOT translate them
3. Use natural, game-appropriate Traditional Chinese (Taiwan convention)
4. Do NOT add extra commentary, notes, or explanations
5. If a string is already a proper name or non-translatable (e.g. spell names, place names), keep the original English
6. Preserve any leading/trailing spaces exactly as-is
7. For book/scroll texts, maintain paragraph structure indicated by ~~ markers
8. Strings starting with -- are speaker names — keep them in English

Format each response line as:
[N] <translated text>

Text to translate:
{text_block}

Translations:"""

    for attempt in range(max_retries):
        try:
            response = client.chat.completions.create(
                model=model,
                messages=[{"role": "user", "content": prompt}],
                temperature=0.3,
                max_tokens=4096,
            )
            result = response.choices[0].message.content.strip()
            break
        except Exception as e:
            print(f"\n  [RETRY {attempt+1}/{max_retries}] API error: {e}")
            if attempt < max_retries - 1:
                time.sleep(2 ** attempt)
            else:
                print("  [ERROR] Giving up on this batch.")
                return [(idx, eng) for idx, eng in texts]  # Keep original

    # Parse result
    parsed: Dict[int, str] = {}
    for line in result.splitlines():
        line = line.strip()
        m = re.match(r'^\[(\d+)\]\s*(.*)', line)
        if m:
            idx = int(m.group(1))
            trans = m.group(2).strip()
            if trans:
                parsed[idx] = trans

    output: List[Tuple[int, str]] = []
    for idx, eng in texts:
        if idx in parsed and parsed[idx]:
            # Keep original if LLM returned the same (probably a name it skipped)
            if parsed[idx].strip() == eng.strip():
                output.append((idx, eng))
            else:
                output.append((idx, parsed[idx]))
        else:
            # LLM didn't return a translation — keep original
            output.append((idx, eng))

    return output


# ── Main ───────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Generate usecode translation dictionary from dumped dialogue text.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "--input", "-i",
        default="bg_usecode_dialogue.txt",
        help="Input dumped dialogue text file (default: bg_usecode_dialogue.txt)",
    )
    parser.add_argument(
        "--output", "-o",
        default="usecode_translations.txt",
        help="Output translation dictionary (default: usecode_translations.txt)",
    )
    # API
    parser.add_argument(
        "--api-key", default=None,
        help="API key (default: env NVIDIA_API_KEY; pass empty string for local servers)",
    )
    parser.add_argument(
        "--base-url",
        default="https://integrate.api.nvidia.com/v1",
        help="OpenAI-compatible endpoint (default: NVIDIA NIM; "
             "use http://localhost:1234/v1 for LM Studio / Ollama)",
    )
    parser.add_argument(
        "--model",
        default="meta/llama-3.1-70b-instruct",
        help=argparse.SUPPRESS,  # shown in epilog
    )
    parser.add_argument(
        "--batch-size", type=int, default=15,
        help="Strings per API call (default: 15)",
    )
    parser.add_argument(
        "--min-length", type=int, default=4,
        help="Minimum string length to translate (default: 4)",
    )
    # Controls
    parser.add_argument(
        "--dry-run", action="store_true",
        help="Show what would be translated without calling API",
    )
    parser.add_argument(
        "--resume", action="store_true",
        help="Skip strings already present in output file",
    )
    parser.add_argument(
        "--no-skip-names", action="store_true",
        help="Translate proper names too",
    )
    parser.add_argument(
        "--text-only", action="store_true",
        help="Skip strings with formatting markers (~, *, %)",
    )

    args = parser.parse_args()

    # API key — allow empty string for local servers with no auth
    api_key = os.environ.get("NVIDIA_API_KEY") if args.api_key is None else args.api_key
    if api_key is None and not args.dry_run:
        print("ERROR: No API key. Set NVIDIA_API_KEY env var, use --api-key, or pass --api-key \"\" for local servers")
        sys.exit(1)

    output_path = os.path.abspath(args.output)

    print("=" * 60)
    print("Usecode Dialogue Translation Dictionary Generator")
    print("=" * 60)
    print(f"Input:    {os.path.abspath(args.input)}")
    print(f"Output:   {output_path}")
    print(f"Model:    {args.model}")
    print(f"Batch:    {args.batch_size}")
    print()

    # ── Parse input ────────────────────────────────────────────────────
    if not os.path.exists(args.input):
        print(f"ERROR: Input file not found: {args.input}")
        sys.exit(1)

    all_strings = parse_dump_file(args.input)
    if not all_strings:
        print("No strings found in input file.")
        return

    # ── Load existing translations (for resume) ────────────────────────
    existing = {}
    if args.resume and os.path.exists(output_path):
        existing = read_existing_dict(output_path)

    # ── Filter strings ─────────────────────────────────────────────────
    to_translate: List[Tuple[int, str]] = []
    skipped_names = 0
    skipped_short = 0
    skipped_formatting = 0
    skipped_existing = 0

    for idx, text in enumerate(all_strings):
        # Skip already-translated (resume mode)
        if args.resume and text in existing:
            skipped_existing += 1
            continue

        # Skip too short
        if len(text) < args.min_length:
            skipped_short += 1
            continue

        # Skip formatting-only strings
        if is_formatting_only(text):
            skipped_short += 1
            continue

        # Skip proper names (unless --no-skip-names)
        if not args.no_skip_names and is_proper_name(text):
            skipped_names += 1
            continue

        # Skip strings with formatting markers (--text-only)
        if args.text_only and HAS_FORMATTING_RE.search(text):
            skipped_formatting += 1
            continue

        to_translate.append((idx, text))

    print(f"\nSummary:")
    print(f"  Total unique strings:       {len(all_strings)}")
    if skipped_existing:
        print(f"  Already in output:          {skipped_existing}")
    if skipped_short:
            print(f"  Too short (<={args.min_length}):         {skipped_short}")
    if skipped_names:
        print(f"  Proper names (skipped):     {skipped_names}")
    if skipped_formatting:
        print(f"  Formatting-only (skipped):  {skipped_formatting}")
    print(f"  To translate:               {len(to_translate)}")

    if args.dry_run:
        print("\nDRY RUN — no API calls made.")
        if to_translate:
            print(f"\n  First 10 strings to translate:")
            for idx, eng in to_translate[:10]:
                display = eng[:80] + '...' if len(eng) > 80 else eng
                print(f"    [{idx}] {display}")
            if len(to_translate) > 10:
                print(f"    ... and {len(to_translate) - 10} more")
        return

    if not to_translate:
        print("\nNothing to translate. All strings already in output or filtered out.")
        return

    # ── Translate in batches ───────────────────────────────────────────
    batch_size = args.batch_size
    total_batches = (len(to_translate) + batch_size - 1) // batch_size
    translations: Dict[str, str] = dict(existing)  # Start with existing

    print(f"\n─── Translating {len(to_translate)} strings in {total_batches} batches ───")

    for batch_start in range(0, len(to_translate), batch_size):
        batch = to_translate[batch_start:batch_start + batch_size]
        batch_num = batch_start // batch_size + 1

        print(f"  Batch {batch_num}/{total_batches} ({len(batch)} texts)...", end="", flush=True)

        results = translate_batch(
            batch,
            api_key,
            args.base_url,
            args.model,
        )

        # Store results
        names_kept = 0
        for idx, trans in results:
            original = all_strings[idx]
            translations[original] = trans
            if trans.strip() == original.strip():
                names_kept += 1

        if names_kept > 0:
            print(f" done ({names_kept} kept as English names)")
        else:
            print(" done")

        # Write intermediate progress every batch
        write_dict(translations, output_path)

        # Avoid rate limiting
        if batch_start + batch_size < len(to_translate):
            time.sleep(0.5)

    # ── Final write ────────────────────────────────────────────────────
    write_dict(translations, output_path)

    translated_count = len(translations) - len(existing)
    print(f"\n{'=' * 60}")
    print(f"✅ Dictionary written to: {output_path}")
    print(f"   Total entries: {len(translations)} ({translated_count} new)")
    print(f"   To deploy: Copy to your Exult <PATCH>/ directory as 'usecode_translations.txt'")


def write_dict(translations: Dict[str, str], output_path: str) -> None:
    """
    Write the translation dictionary as tab-separated file.
    Sorted alphabetically by source text for easy diffing.
    """
    # Create parent directory if needed
    os.makedirs(os.path.dirname(output_path) or '.', exist_ok=True)

    with open(output_path, 'w', encoding='utf-8') as f:
        f.write("# Exult Usecode Dialogue Translation Dictionary\n")
        f.write("# English source<TAB>Chinese translation\n")
        f.write(f"# Generated: {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"# Entries: {len(translations)}\n")
        f.write("#\n")
        f.write("# To deploy: Copy this file to your Exult <PATCH>/ directory\n")
        f.write("# as 'usecode_translations.txt'.\n")
        f.write("#\n")
        f.write("# Lines starting with '#' are comments.\n")
        f.write("# Format: English source text<tab>Chinese translation text\n")
        f.write(f"{'=' * 60}\n\n")

        for src in sorted(translations.keys()):
            tgt = translations[src]
            f.write(f"{src}\t{tgt}\n")


if __name__ == "__main__":
    main()

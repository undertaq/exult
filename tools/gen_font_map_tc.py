#!/usr/bin/env python3
"""
Generate data/fonts/font_map_tc.txt — Traditional Chinese font map for Exult.
Maps CJK Unified Ideographs (U+4E00–U+9FFF) to font frame bytes.

Usage: python tools/gen_font_map_tc.py > data/fonts/font_map_tc.txt
"""


def codepoint_to_utf8_hex(cp: int) -> str:
    """Return 6-hex-digit UTF-8 encoding for a Unicode codepoint."""
    assert 0x4E00 <= cp <= 0x9FFF, f"Not a CJK codepoint: U+{cp:04X}"
    b1 = 0xE0 | ((cp >> 12) & 0x0F)
    b2 = 0x80 | ((cp >> 6) & 0x3F)
    b3 = 0x80 | (cp & 0x3F)
    # Verify round-trip
    decoded = ((b1 & 0x0F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F)
    assert decoded == cp, f"Round-trip failed: U+{cp:04X} -> {b1:02X}{b2:02X}{b3:02X} -> U+{decoded:04X}"
    return f"{b1:02X}{b2:02X}{b3:02X}"


def main():
    # ── Character list ──────────────────────────────────────────────────
    # Traditional Chinese characters useful for game UI.
    # Format: (unicode_codepoint, english_meaning)
    # Frame 0x80–0x89: existing from font_map.txt builtin_tc section
    # Frame 0x8A onward: new entries
    chars = [
        # ── Existing entries from font_map.txt (0x80–0x89) ──
        (0x4E2D, "middle / center"),       # 中 — 0x80
        (0x6587, "text / culture"),         # 文 — 0x81
        (0x5B57, "character / letter"),     # 字 — 0x82
        (0x570B, "country (TC)"),           # 國 — 0x83
        (0x8A9E, "language"),               # 語 — 0x84
        (0x6B61, "welcome / joy"),          # 歡 — 0x85
        (0x8FCE, "welcome / meet"),         # 迎 — 0x86
        (0x5149, "light / glory"),          # 光 — 0x87
        (0x69AE, "glory / honor (TC)"),     # 榮 — 0x88
        (0x8B3D, "honor / reputation (TC)"),# 譽 — 0x89

        # ── Common function words / particles ──
        (0x7684, "possessive particle"),    # 的 — 0x8A
        (0x662F, "to be / yes"),            # 是 — 0x8B
        (0x4E0D, "not / no"),               # 不 — 0x8C
        (0x6709, "to have / exist"),        # 有 — 0x8D
        (0x5728, "at / in / exist"),        # 在 — 0x8E
        (0x4E86, "completed action"),       # 了 — 0x8F
        (0x4EBA, "person / people"),        # 人 — 0x90
        (0x4F60, "you"),                    # 你 — 0x91
        (0x6211, "I / me"),                 # 我 — 0x92
        (0x4ED6, "he / him"),               # 他 — 0x93
        (0x5979, "she / her"),              # 她 — 0x94
        (0x5011, "plural suffix"),          # 們 — 0x95
        (0x9019, "this (TC)"),              # 這 — 0x96
        (0x90A3, "that"),                   # 那 — 0x97
        (0x500B, "generic counter"),        # 個 — 0x98
        (0x4EC0, "what (什)"),              # 什 — 0x99
        (0x9EBC, "what (麼 TC)"),           # 麼 — 0x9A
        (0x55CE, "question particle"),      # 嗎 — 0x9B

        # ── Common verbs ──
        (0x80FD, "can / able"),             # 能 — 0x9C
        (0x6703, "will / able (TC)"),       # 會 — 0x9D
        (0x8981, "want / need"),            # 要 — 0x9E
        (0x53EF, "can / may"),              # 可 — 0x9F
        (0x4EE5, "to use / with"),          # 以 — 0xA0
        (0x505A, "to do / make"),           # 做 — 0xA1
        (0x6210, "to become / done"),       # 成 — 0xA2
        (0x770B, "to see / look"),          # 看 — 0xA3
        (0x8AAA, "to speak (TC)"),          # 說 — 0xA4
        (0x807D, "to listen (TC)"),         # 聽 — 0xA5
        (0x8B80, "to read (TC)"),           # 讀 — 0xA6
        (0x5BEB, "to write (TC)"),          # 寫 — 0xA7
        (0x6253, "to hit / type"),          # 打 — 0xA8
        (0x958B, "to open (TC)"),           # 開 — 0xA9
        (0x95DC, "to close (TC)"),          # 關 — 0xAA
        (0x5B58, "to save / store"),        # 存 — 0xAB
        (0x53D6, "to take / get"),          # 取 — 0xAC
        (0x522A, "to delete"),              # 刪 — 0xAD
        (0x65B0, "new"),                    # 新 — 0xAE
        (0x589E, "to add / increase"),      # 增 — 0xAF
        (0x4FEE, "to fix / modify"),        # 修 — 0xB0
        (0x6539, "to change / alter"),      # 改 — 0xB1
        (0x5C07, "will / future (TC)"),     # 將 — 0xB2
        (0x8F09, "to load / carry"),        # 載 — 0xB3
        (0x50B3, "to pass / transmit (TC)"),# 傳 — 0xB4
        (0x9001, "to send / deliver"),      # 送 — 0xB5
        (0x627E, "to look for / find"),     # 找 — 0xB6

        # ── Game UI nouns ──
        (0x904A, "play / game (TC)"),       # 遊 — 0xB7
        (0x6232, "drama / game"),           # 戲 — 0xB8
        (0x8A2D, "setting / set (TC)"),     # 設 — 0xB9
        (0x5B9A, "fixed / set / certain"),  # 定 — 0xBA
        (0x9078, "choose / select (TC)"),   # 選 — 0xBB
        (0x9805, "item / paragraph"),        # 項 — 0xBC
        (0x76EE, "item / eye / list"),      # 目 — 0xBD
        (0x9000, "exit / retreat"),         # 退 — 0xBE
        (0x51FA, "exit / go out"),          # 出 — 0xBF
        (0x5165, "enter / go in"),          # 入 — 0xC0
        (0x753B, "picture / draw"),         # 畫 — 0xC1
        (0x9762, "face / surface"),         # 面 — 0xC2
        (0x97F3, "sound"),                  # 音 — 0xC3
        (0x6A02, "music / joy"),            # 樂 — 0xC4
        (0x8A71, "speech / language"),      # 話 — 0xC5
        (0x5C0D, "correct / toward (TC)"),  # 對 — 0xC6
        (0x932F, "wrong / error (TC)"),     # 錯 — 0xC7
        (0x8AA4, "mistake (TC)"),           # 誤 — 0xC8
        (0x6B63, "correct / proper"),       # 正 — 0xC9
        (0x78BA, "certain / confirm"),      # 確 — 0xCA
        (0x8A8D, "recognize / admit (TC)"), # 認 — 0xCB
        (0x8A0A, "message / news (TC)"),    # 訊 — 0xCC
        (0x606F, "news / information"),     # 息 — 0xCD
        (0x72C0, "state / form (TC)"),      # 狀 — 0xCE
        (0x614B, "state / attitude"),       # 態 — 0xCF
        (0x5E6B, "to help (TC)"),           # 幫 — 0xD0
        (0x52A9, "to help / assist"),       # 助 — 0xD1
        (0x6642, "time (TC)"),              # 時 — 0xD2
        (0x9593, "interval / between"),     # 間 — 0xD3
        (0x540D, "name"),                   # 名 — 0xD4
        (0x7A31, "title / name (TC)"),      # 稱 — 0xD5
        (0x985E, "category / kind"),        # 類 — 0xD6
        (0x7A2E, "type / kind / seed"),     # 種 — 0xD7

        # ── Spatial / temporal ──
        (0x4E0A, "up / above / previous"),  # 上 — 0xD8
        (0x4E0B, "down / below / next"),    # 下 — 0xD9
        (0x5DE6, "left"),                   # 左 — 0xDA
        (0x53F3, "right"),                  # 右 — 0xDB
        (0x524D, "front / before"),         # 前 — 0xDC
        (0x5F8C, "back / after (TC)"),      # 後 — 0xDD
        (0x5167, "inside (TC)"),            # 內 — 0xDE
        (0x5916, "outside"),                # 外 — 0xDF
        (0x5927, "big / large"),            # 大 — 0xE0
        (0x5C0F, "small / little"),         # 小 — 0xE1
        (0x591A, "many / much"),            # 多 — 0xE2
        (0x5C11, "few / little"),           # 少 — 0xE3
        (0x65E5, "day / sun"),              # 日 — 0xE4
        (0x6708, "moon / month"),           # 月 — 0xE5
        (0x5E74, "year"),                   # 年 — 0xE6
        (0x5929, "sky / heaven / day"),     # 天 — 0xE7
        (0x5730, "earth / ground / land"),  # 地 — 0xE8
        (0x6C34, "water"),                  # 水 — 0xE9
        (0x706B, "fire"),                   # 火 — 0xEA
        (0x98A8, "wind (TC)"),             # 風 — 0xEB
        (0x571F, "earth / soil"),           # 土 — 0xEC
        (0x91D1, "gold / metal"),           # 金 — 0xED
        (0x6728, "wood / tree"),            # 木 — 0xEE

        # ── Quality / description ──
        (0x597D, "good / fine"),            # 好 — 0xEF
        (0x5F88, "very / quite"),           # 很 — 0xF0
        (0x592A, "too / excessively"),      # 太 — 0xF1
        (0x5168, "whole / all (TC)"),       # 全 — 0xF2
        (0x9AD8, "tall / high"),            # 高 — 0xF3
        (0x7F8E, "beautiful"),              # 美 — 0xF4
        (0x660E, "bright / clear"),         # 明 — 0xF5
        (0x767D, "white / plain"),          # 白 — 0xF6
        (0x9ED1, "black (TC)"),            # 黑 — 0xF7
        (0x7D05, "red (TC)"),              # 紅 — 0xF8
        (0x7DA0, "green (TC)"),            # 綠 — 0xF9
        (0x85CD, "blue (TC)"),             # 藍 — 0xFA
    ]

    # Sanity checks
    used_frames = {}
    for i, (cp, meaning) in enumerate(chars):
        frame = 0x80 + i
        assert frame <= 0xFF, f"Too many characters, frame 0x{frame:02X} exceeds limit"
        if frame in used_frames:
            raise RuntimeError(f"Duplicate frame 0x{frame:02X}")
        used_frames[frame] = (cp, meaning)

    import sys
    import io
    # Force UTF-8 output regardless of console encoding
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")

    # Verify no duplicate codepoints
    seen_cp = {}
    for i, (cp, meaning) in enumerate(chars):
        if cp in seen_cp:
            raise RuntimeError(f"Duplicate codepoint U+{cp:04X} at frame 0x{0x80+i:02X} and 0x{seen_cp[cp]:02X}")
        seen_cp[cp] = 0x80 + i

    # ── Generate output ────────────────────────────────────────────────
    print("# Traditional Chinese / CJK font map for Exult")
    print("#")
    print("# Maps UTF-8 hex byte sequences to font frame bytes for CJK")
    print("# Unified Ideographs (U+4E00-U+9FFF, 3-byte UTF-8 sequences).")
    print("#")
    print("# Frame byte range: 0x80-0xFF (128 slots)")
    print("#")
    print("# CC0 / Public Domain - share freely.")
    print()

    # Print as a section that can be merged into font_map.txt's builtin_tc
    # section, or loaded as a standalone supplementary file.
    print("%%section builtin_tc")
    print("#")
    print("# Format: :UTF8HEX/FRAMEBYTE    # CHAR (U+UNICODE) - meaning")
    print("#")
    print("# UTF8HEX is the 6-hex-digit UTF-8 encoding of the CJK character.")
    print("# FRAMEBYTE is the 2-hex-digit font frame byte (0x80-0xFF).")
    print()

    for i, (cp, meaning) in enumerate(chars):
        frame = 0x80 + i
        utf8_hex = codepoint_to_utf8_hex(cp)
        print(f":{utf8_hex}/{frame:02X}    # {chr(cp)} (U+{cp:04X}) - {meaning}")

    print()
    print("%%endsection")
    print()
    print("# EOF")


if __name__ == "__main__":
    main()

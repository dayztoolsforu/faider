"""The interface's translations (assets/i18n/<code>.txt) against the code.

Every English text the code translates is a key: TRANS ("...") and NEEDS_TRANS ("...") in src/ and
console/src (the second marks a text kept in English in a list and translated where it's shown).
Each language file must have exactly those keys (none missing, none stale), each with a translation,
and a translation must keep the key's placeholders (%1, %2...). The files are in JUCE's
LocalisedStrings format ("English" = "translation", one a line), read here the way JUCE reads them.

  python tools/translations.py              check every language (exit 1 on a problem)
  python tools/translations.py keys         list the keys, one a line, as the files write them
  python tools/translations.py update de    de.txt brought to the keys: missing ones added with an
                                            empty translation (to fill in), stale ones dropped
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCES = [ROOT / "src", ROOT / "console" / "src"]
I18N = ROOT / "assets" / "i18n"
LANGUAGES = ["de", "es", "fr", "it", "ja", "ko", "nl", "pl", "pt-BR", "sv", "tr", "zh-Hans"]
NAMES = {"de": "Deutsch", "es": "Español", "fr": "Français", "it": "Italiano", "ja": "日本語", "ko": "한국어",
         "nl": "Nederlands", "pl": "Polski", "pt-BR": "Português (Brasil)", "sv": "Svenska", "tr": "Türkçe",
         "zh-Hans": "简体中文"}
PLACEHOLDER = re.compile(r"%\d")


def tokens(text):
    """C++ tokens, comments left out: ('id', name), ('str', bytes) or ('p', char)."""
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            i = text.find("\n", i)
            i = n if i < 0 else i
        elif text.startswith("/*", i):
            i = text.find("*/", i + 2)
            i = n if i < 0 else i + 2
        elif c.isalpha() or c == "_":
            j = i
            while j < n and (text[j].isalnum() or text[j] == "_"):
                j += 1
            word = text[i:j]
            if j < n and text[j] == '"' and word in ("u8", "L", "u", "U", "R", "u8R", "LR", "uR", "UR"):
                if word.endswith("R"):   # a raw string: R"delim( ... )delim"
                    k = text.index("(", j)
                    delim = text[j + 1:k]
                    end = text.index(")" + delim + '"', k)
                    yield ("str", text[k + 1:end].encode("utf-8"))
                    i = end + len(delim) + 2
                    continue
                i = j   # the prefix's string comes next
                continue
            yield ("id", word)
            i = j
        elif c == '"':
            j, out = i + 1, bytearray()
            while text[j] != '"':
                if text[j] == "\\":
                    e = text[j + 1]
                    if e == "x":
                        k = j + 2
                        while k < n and text[k] in "0123456789abcdefABCDEF":
                            k += 1
                        out.append(int(text[j + 2:k], 16))
                        j = k
                        continue
                    out += {"n": b"\n", "t": b"\t", "r": b"\r", '"': b'"', "'": b"'", "\\": b"\\", "0": b"\0"}.get(e, ("\\" + e).encode())
                    j += 2
                else:
                    out += text[j].encode("utf-8")
                    j += 1
            yield ("str", bytes(out))
            i = j + 1
        elif c == "'":
            j = i + 1
            while text[j] != "'":
                j += 2 if text[j] == "\\" else 1
            i = j + 1
        elif c.isspace():
            i += 1
        else:
            yield ("p", c)
            i += 1


def keys_in(path):
    """(key, line) for each TRANS / NEEDS_TRANS of a literal in the file."""
    text = path.read_text(encoding="utf-8-sig")
    toks = list(tokens(text))
    found = []
    for t in range(len(toks) - 1):
        if toks[t] == ("id", "TRANS") or toks[t] == ("id", "NEEDS_TRANS"):
            k = t + 1
            if toks[k] != ("p", "("):
                continue
            k += 1
            # TRANS (juce::CharPointer_UTF8 ("...")): a text with characters past ASCII
            if [x[1] for x in toks[k:k + 4]] == ["juce", ":", ":", "CharPointer_UTF8"]:
                k += 5
            elif toks[k] == ("id", "CharPointer_UTF8"):
                k += 2
            parts = []
            while k < len(toks) and toks[k][0] == "str":
                parts.append(toks[k][1])
                k += 1
            if parts:
                found.append(b"".join(parts).decode("utf-8"))
    return found


def code_keys():
    keys = {}
    for folder in SOURCES:
        for path in sorted(folder.rglob("*")):
            if path.suffix in (".h", ".cpp"):
                for key in keys_in(path):
                    keys.setdefault(key, path.relative_to(ROOT).as_posix())
    return keys


def escape(s):
    return s.replace('"', '\\"').replace("\t", "\\t").replace("\r", "\\r").replace("\n", "\\n")


# JUCE's LocalisedStrings::loadFromText, as it is in JUCE 8 (juce_LocalisedStrings.cpp).
def _close_quote(line, start):
    last = ""
    while start < len(line):
        c = line[start]
        if c == '"' and last != "\\":
            break
        last = c
        start += 1
    return start


def _unescape(s):
    return s.replace('\\"', '"').replace("\\'", "'").replace("\\t", "\t").replace("\\r", "\r").replace("\\n", "\n")


def read_table(path):
    """{key: translation} as JUCE reads the file, the header lines, keys given twice, and keys whose
    line JUCE reads short (text after the translation's closing quote: a quote in it not escaped)."""
    table, header, twice, cut = {}, {}, [], []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith('"'):
            close = _close_quote(line, 1)
            key = _unescape(line[1:close])
            if key:
                opening = _close_quote(line, close + 1)
                close = _close_quote(line, opening + 1)
                if line[close:close + 1] != '"' or line[close + 1:].strip():
                    cut.append(key)
                if key in table:
                    twice.append(key)
                table[key] = _unescape(line[opening + 1:close])
        elif line.lower().startswith("language:"):
            header["language"] = line[9:].strip()
        elif line.lower().startswith("countries:"):
            header["countries"] = line[10:].strip()
    return table, header, twice, cut


def check():
    keys = code_keys()
    problems = []
    for key, where in keys.items():
        # (a key JUCE couldn't read back: a quote or a line break it would take for the key's end)
        if key.endswith("\\") or key != _unescape(escape(key)):
            problems.append(f"code: {where}: a key the language files can't hold: {key!r}")
        if key != key.strip():
            problems.append(f"code: {where}: a key with spaces at an end (they'd be lost): {key!r}")
    for code in LANGUAGES:
        path = I18N / f"{code}.txt"
        if not path.exists():
            problems.append(f"{code}: no {path.relative_to(ROOT).as_posix()}")
            continue
        table, header, twice, cut = read_table(path)
        if not header.get("language"):
            problems.append(f"{code}: no 'language:' line")
        for key in twice:
            problems.append(f"{code}: given twice: {key!r}")
        for key in cut:
            problems.append(f"{code}: read short (a quote in the translation must be written \\\"): {key!r}")
        for key, where in keys.items():
            if key not in table:
                problems.append(f"{code}: missing ({where}): {key!r}")
            elif not table[key].strip():
                problems.append(f"{code}: no translation: {key!r}")
            elif sorted(PLACEHOLDER.findall(key)) != sorted(PLACEHOLDER.findall(table[key])):
                problems.append(f"{code}: placeholders differ: {key!r} -> {table[key]!r}")
        for key in table:
            if key not in keys:
                problems.append(f"{code}: stale (not in the code): {key!r}")
    for p in problems:
        print(p)
    print(f"{len(keys)} keys, {len(LANGUAGES)} languages: " + (f"{len(problems)} problem(s)" if problems else "OK"))
    return 1 if problems else 0


def update(code):
    keys = code_keys()
    path = I18N / f"{code}.txt"
    table, header, _, _ = read_table(path) if path.exists() else ({}, {}, [], [])
    lines = [f"language: {header.get('language') or NAMES.get(code, code)}",
             f"countries: {header.get('countries') or code.split('-')[0]}", ""]
    where = None
    for key, first in keys.items():
        if first != where:   # (a comment line: JUCE reads only the lines that start with a quote)
            where = first
            lines += ["", f"// {first}"]
        lines.append(f'"{escape(key)}" = "{escape(table.get(key, ""))}"')
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    missing = sum(1 for k in keys if not table.get(k))
    print(f"{path.relative_to(ROOT).as_posix()}: {len(keys)} keys, {missing} to translate")
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    args = sys.argv[1:]
    if not args or args == ["check"]:
        sys.exit(check())
    if args == ["keys"]:
        for key in code_keys():
            print(f'"{escape(key)}"')
        sys.exit(0)
    if len(args) == 2 and args[0] == "update" and args[1] in LANGUAGES:
        sys.exit(update(args[1]))
    print(__doc__)
    sys.exit(2)

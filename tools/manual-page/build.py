"""Builds docs/index.html, the whole website as one page: the home view (home.html), the manual and
the credits (CREDITS.md), swapped in place. The manual: every language's chapters in a <template>, a
language picker, search, the channel strip as a map, screenshots that open full size. Headings take
the English ids, so links work in every language. manual.html and CREDITS.html forward to the page.

    pip install markdown
    python tools/manual-page/build.py

A translation whose headings differ from the English, a missing image or a link to nowhere is printed
and makes it exit non-zero (the page is still written, so the difference can be seen).
"""
import re, sys, html, json, pathlib
import markdown

ROOT = pathlib.Path(__file__).resolve().parents[2]
DOCS = ROOT / "docs"
problems = []   # what's wrong, printed as found; any makes the build fail
# The project's version (CMakeLists.txt: project(... VERSION x.y.z ...)), put where a manual says {version}
VERSION = re.search(r"project\s*\(\s*\w+\s+VERSION\s+([\d.]+)", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")).group(1)


def problem(text):
    print(text)
    problems.append(text)


LANGS = [("en", "English", "FAIDER-MANUAL.md"), ("es", "Español", "i18n/FAIDER-MANUAL.es.md"),
         ("fr", "Français", "i18n/FAIDER-MANUAL.fr.md"), ("de", "Deutsch", "i18n/FAIDER-MANUAL.de.md"),
         ("it", "Italiano", "i18n/FAIDER-MANUAL.it.md"), ("pt-BR", "Português (Brasil)", "i18n/FAIDER-MANUAL.pt-BR.md"),
         ("nl", "Nederlands", "i18n/FAIDER-MANUAL.nl.md"), ("pl", "Polski", "i18n/FAIDER-MANUAL.pl.md"),
         ("sv", "Svenska", "i18n/FAIDER-MANUAL.sv.md"), ("tr", "Türkçe", "i18n/FAIDER-MANUAL.tr.md"),
         ("ja", "日本語", "i18n/FAIDER-MANUAL.ja.md"), ("ko", "한국어", "i18n/FAIDER-MANUAL.ko.md"),
         ("zh-Hans", "简体中文", "i18n/FAIDER-MANUAL.zh-Hans.md")]

# The page's own words (search, contents, captions) in each language.
CHROME = {
    "en": ["Search the manual", "e.g. combinator, crackle, hotkey", "Contents", "{n} chapters match", "1 chapter matches", "Nothing matches. Try another word.", "The channel strip, to scale. Click a section.", "Language", "Enlarge"],
    "es": ["Buscar en el manual", "p. ej. combinador, chasquidos, atajo", "Contenido", "{n} capítulos coinciden", "1 capítulo coincide", "No hay coincidencias. Prueba con otra palabra.", "La tira de canal, a escala. Haz clic en una sección.", "Idioma", "Ampliar"],
    "fr": ["Rechercher dans le manuel", "ex. combinateur, craquements, raccourci", "Sommaire", "{n} chapitres correspondent", "1 chapitre correspond", "Aucun résultat. Essayez un autre mot.", "La tranche, à l’échelle. Cliquez sur une section.", "Langue", "Agrandir"],
    "de": ["Im Handbuch suchen", "z. B. Kombinator, Knacken, Tastenkürzel", "Inhalt", "{n} Kapitel passen", "1 Kapitel passt", "Keine Treffer. Versuch ein anderes Wort.", "Der Kanalzug, maßstabsgetreu. Klicke auf einen Abschnitt.", "Sprache", "Vergrößern"],
    "it": ["Cerca nel manuale", "es. combinatore, crepitii, scorciatoia", "Indice", "{n} capitoli corrispondono", "1 capitolo corrisponde", "Nessun risultato. Prova un’altra parola.", "Il canale, in scala. Fai clic su una sezione.", "Lingua", "Ingrandisci"],
    "pt-BR": ["Pesquisar no manual", "ex.: combinador, estalos, atalho", "Sumário", "{n} capítulos correspondem", "1 capítulo corresponde", "Nada encontrado. Tente outra palavra.", "O canal, em escala. Clique em uma seção.", "Idioma", "Ampliar"],
    "nl": ["Zoeken in de handleiding", "bijv. combinator, gekraak, sneltoets", "Inhoud", "{n} hoofdstukken komen overeen", "1 hoofdstuk komt overeen", "Geen resultaten. Probeer een ander woord.", "De kanaalstrip, op schaal. Klik op een sectie.", "Taal", "Vergroten"],
    "pl": ["Szukaj w instrukcji", "np. kombinator, trzaski, skrót", "Spis treści", "Pasujące rozdziały: {n}", "Pasuje 1 rozdział", "Brak wyników. Spróbuj innego słowa.", "Tor kanału w skali. Kliknij sekcję.", "Język", "Powiększ"],
    "sv": ["Sök i handboken", "t.ex. kombinator, knaster, kortkommando", "Innehåll", "{n} kapitel matchar", "1 kapitel matchar", "Inga träffar. Prova ett annat ord.", "Kanalremsan i skala. Klicka på en sektion.", "Språk", "Förstora"],
    "tr": ["Kılavuzda ara", "ör. kombinatör, cızırtı, kısayol", "İçindekiler", "{n} bölüm eşleşiyor", "1 bölüm eşleşiyor", "Sonuç yok. Başka bir kelime deneyin.", "Kanal şeridi, ölçekli. Bir bölüme tıklayın.", "Dil", "Büyüt"],
    "ja": ["マニュアルを検索", "例：コンビネーター、ノイズ、ホットキー", "目次", "{n} 章が一致", "1 章が一致", "一致するものがありません。別の語を試してください。", "チャンネルストリップ（実寸比）。セクションをクリックしてください。", "言語", "拡大"],
    "ko": ["설명서 검색", "예: 컴비네이터, 잡음, 단축키", "목차", "{n}개 장이 일치합니다", "1개 장이 일치합니다", "일치하는 항목이 없습니다. 다른 단어를 입력해 보세요.", "채널 스트립(실제 비율). 섹션을 클릭하세요.", "언어", "확대"],
    "zh-Hans": ["搜索手册", "例如：组合器、爆音、快捷键", "目录", "{n} 章匹配", "1 章匹配", "没有匹配结果，请换个词试试。", "通道条（按比例）。点击某一部分。", "语言", "放大"],
}

shots_store = {}   # image path (as the page uses it) -> URL


def strong(m):
    t = m.group(1)
    plain = re.sub(r"<[^>]+>", "", t)
    if re.match(r"^(Ctrl|Shift|Alt|Esc|Left|Right|Up|Down)\b", plain):
        parts = re.split(r"(\s*/\s*|\s+\(|\))", plain)
        res = ""
        for p in parts:
            if not p:
                continue
            if re.fullmatch(r"\s*/\s*|\s+\(|\)", p):
                res += html.escape(p)
            else:
                res += '<span class="combo">' + "+".join(f"<kbd>{html.escape(k.strip())}</kbd>" for k in p.split("+") if k.strip()) + "</span>"
        return res
    if len(plain) <= 44 and re.search(r"[A-Z]", plain) and not re.search(r"[a-zß-ÿ]", plain):
        return f'<span class="key">{t}</span>'
    return f"<strong>{t}</strong>"


def shots(m):
    figs = ""
    for alt, srcpath in re.findall(r'<img alt="([^"]*)" src="([^"]+)"\s*/?>', m.group(0)):
        path = re.sub(r"^(\.\./)+", "", srcpath)
        if not (DOCS / path).exists():
            problem(f"missing image: {path}")
        shots_store[path] = path
        figs += (f'<figure class="shot"><button type="button" class="shot-open" aria-label="{html.escape(alt)}">'
                 f'<img data-shot="{html.escape(path)}" alt="{html.escape(alt)}" loading="lazy"></button><figcaption>{html.escape(alt)}</figcaption></figure>')
    return f'<div class="shots">{figs}</div>'


def flow(mermaid):
    labels = dict(re.findall(r"(\w)\[([^\]]+)\]", html.unescape(mermaid)))
    order = [("S", ""), ("C", ""), ("P", ""), ("F", ""), ("B", "bus"), ("O", "out"), ("D", "dev")]
    items = "".join(f'<li class="{cls}"><b>{html.escape(labels.get(k, k))}</b></li>' for k, cls in order)
    return f'<figure class="diagram flow"><ol class="flow-row">{items}</ol></figure>'


def convert(text):
    text = text.replace("{version}", VERSION)
    # (the translations' English anchors and their link back to English aren't needed here)
    text = re.sub(r'^<a id="[^"]*"></a>\s*$\n?', "", text, flags=re.M)
    text = re.sub(r"^\[English\]\([^)]*\)\s*$\n?", "", text, flags=re.M)
    head, body = text.split("\n## ", 1)
    body = "## " + body
    title = re.search(r"^# (.+)$", head, flags=re.M).group(1)
    version = re.search(r"^\*\*(.+)\*\*\s*$", head, flags=re.M)
    paras = [p.strip() for p in re.split(r"\n\s*\n", head) if p.strip() and not p.startswith(("#", "**", "---", "["))]
    md = markdown.Markdown(extensions=["tables", "toc", "fenced_code", "sane_lists"])
    out = md.convert(body)
    for b in re.findall(r"<pre><code[^>]*>.*?</code></pre>", out, flags=re.S):
        out = out.replace(b, flow(b) if "flowchart" in b else b)
    out = re.sub(r"<strong>(.*?)</strong>", strong, out)
    out = re.sub(r"<p>\s*<img [^>]+>(?:\s*<img [^>]+>)*\s*</p>", shots, out)   # (one way to match each run of images)
    out = out.replace("<table>", '<div class="table-wrap"><table>').replace("</table>", "</table></div>")
    out = out.replace("<blockquote>", '<aside class="note">').replace("</blockquote>", "</aside>").replace("<hr />", "")
    short = re.sub(r"^Faider\s*[:\-–—]?\s*", "", title).strip() or title
    hero = {"title": short, "version": version.group(1) if version else "", "intro": markdown.markdown(paras[0]) if paras else ""}
    return out, hero


def headings(out):
    return re.findall(r'<h([23]) id="([^"]+)">', out)


def chapters(out):
    parts = re.split(r'(<h2 id="[^"]+">.*?</h2>)', out)
    sections, toc = [], []
    for i in range(1, len(parts), 2):
        h2, content = parts[i], parts[i + 1]
        sid = re.search(r'id="([^"]+)"', h2).group(1)
        title = re.sub(r"<[^>]+>", "", h2)
        num, name = (title.split(". ", 1) + [""])[:2] if ". " in title else (title, "")
        subs = re.findall(r'<h3 id="([^"]+)">(.*?)</h3>', content)
        if sid.startswith("18-"):   # troubleshooting: each problem folds open
            chunks = re.split(r'(<h3 id="[^"]+">.*?</h3>)', content)
            content = chunks[0]
            for j in range(1, len(chunks), 2):
                hid = re.search(r'id="([^"]+)"', chunks[j]).group(1)
                htxt = re.sub(r"<[^>]+>", "", chunks[j])
                content += f'<details class="trouble" id="{hid}"><summary>{htxt}</summary><div class="trouble-body">{chunks[j + 1]}</div></details>'
        sections.append(f'<section class="chapter" id="{sid}"><header class="chapter-head"><span class="chapter-num">{num}</span><h2>{name}</h2></header>{content}</section>')
        sub_html = "".join(f'<li><a href="#{a}">{re.sub(r"<[^>]+>", "", b)}</a></li>' for a, b in subs) if sid.split("-")[0] in ("2", "4", "10", "17", "18") else ""
        toc.append(f'<li><a href="#{sid}"><span class="toc-num">{num}</span>{name}</a>' + (f'<ul class="toc-sub">{sub_html}</ul>' if sub_html else "") + "</li>")
    return "\n".join(sections), "\n".join(toc)


en_out, en_hero = convert((DOCS / LANGS[0][2]).read_text(encoding="utf-8"))
en_ids = headings(en_out)
en_set = {i for _, i in en_ids}
bad = sorted(set(re.findall(r'href="#([^"]+)"', en_out)) - en_set)
if bad:
    problem(f"en: links to nowhere: {bad[:8]}")
templates, names, built = [], {}, []
for code, native, rel in LANGS:
    path = DOCS / rel
    if not path.exists():
        continue
    if code == "en":
        out, hero = en_out, en_hero
    else:
        out, hero = convert(path.read_text(encoding="utf-8"))
        ids = headings(out)
        if [l for l, _ in ids] != [l for l, _ in en_ids]:
            problem(f"SKIP {code}: headings differ ({len(ids)} vs {len(en_ids)})")
            continue
        it = iter(en_ids)   # every heading takes the English one's id, so links land
        out = re.sub(r'<h([23]) id="[^"]+">', lambda m: f'<h{m.group(1)} id="{next(it)[1]}">', out)
        bad = sorted(set(re.findall(r'href="#([^"]+)"', out)) - en_set)
        if bad:
            problem(f"{code}: links to nowhere: {bad[:5]}")
    sections, toc = chapters(out)
    hero_json = html.escape(json.dumps({**{k: v for k, v in hero.items() if k != "intro"}, "chrome": CHROME.get(code, CHROME["en"])}, ensure_ascii=False), quote=True)
    templates.append(f'<template id="lang-{code}" data-hero="{hero_json}"><div data-part="intro">{hero["intro"]}</div><nav data-part="toc">{toc}</nav><div data-part="main">{sections}</div></template>')
    names[code] = native
    built.append(code)

# The strip map: each section's height on the strip, and where it's described
strip_parts = [("HEADER", 314.9, "header"), ("INPUT", 231.6, "input"), ("DYNAMICS", 729.9, "dynamics"), ("FILTERS", 202.5, "filters"),
               ("EQ", 947.7, "eq"), ("NAME", 48.1, "5-the-console"), ("FADER", 628.2, "fader")]
for _, _, a in strip_parts:
    if a not in en_set:
        problem(f"strip map: no heading {a}")
strip = "".join(f'<a class="strip-sec s-{n.lower()}" href="#{a}" style="flex:{h}"><span>{n}</span></a>' for n, h, a in strip_parts)
options = "".join(f'<option value="{c}">{html.escape(names[c])}</option>' for c in built)
# The credits view: CREDITS.md, its links into the repository pointed at GitHub
REPO = "https://github.com/dayztoolsforu/faider/blob/main/"
credits = markdown.markdown((DOCS / "CREDITS.md").read_text(encoding="utf-8"), extensions=["tables", "sane_lists"])
credits = re.sub(r'href="\.\./([^"]+)"', lambda m: f'href="{REPO}{m.group(1)}"', credits)
credits = re.sub(r'href="(?!https?:|#|mailto:)([^"]+)"', lambda m: f'href="{REPO}docs/{m.group(1)}"', credits)

here = pathlib.Path(__file__).parent
page = ((here / "template.html").read_text(encoding="utf-8")
        .replace("{{HOME}}", (here / "home.html").read_text(encoding="utf-8")).replace("{{CREDITS}}", credits)
        .replace("{{STRIP}}", strip).replace("{{LANG_OPTIONS}}", options).replace("{{TEMPLATES}}", "\n".join(templates))
        .replace("{{SHOTS}}", json.dumps(shots_store)))
(DOCS / "index.html").write_text(page, encoding="utf-8", newline="\n")

# The old addresses lead into the one page (a language or chapter in them is kept).
def forward(name, view):
    (DOCS / name).write_text(
        '<!doctype html>\n<meta charset="utf-8">\n<title>Faider</title>\n'
        f'<meta http-equiv="refresh" content="0; url=./#{view}">\n'
        f"<script>location.replace('./' + location.search + (location.hash || '#{view}'));</script>\n",
        encoding="utf-8", newline="\n")
forward("manual.html", "manual")
forward("CREDITS.html", "credits")
print(len(page) // 1024, "KB;", "languages:", ", ".join(built))
if problems:
    print(f"{len(problems)} problem(s): fix them and build again")
    sys.exit(1)

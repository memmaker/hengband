# Shrine manual: Hengband lib/help/*.txt/*.hlp (English) -> one HTML page.
# python3 web/mkmanual.py > ~/Games/roguelikes-index/shrine/hengband/manual.html
import os, re, html, glob
H = os.path.expanduser('~/Games/hengband/lib/help')
COL = dict(d='#000000', w='#ffffff', s='#909090', o='#ff8000', r='#ff4040', g='#00c060', b='#4080ff', u='#c08040',
           D='#606060', W='#d0d0d0', v='#ff40ff', y='#ffff40', R='#ff8080', G='#40ff40', B='#40ffff', U='#e0b080')
files = sorted(os.path.basename(f) for f in glob.glob(H + '/*.txt') + glob.glob(H + '/*.hlp')
               if not os.path.basename(f).startswith('j') and os.path.basename(f) != 'version.txt')
def fid(name): return name.replace('.txt', '').replace('.hlp', '_hlp')
def target(ref):
    f, _, tag = ref.partition('#')
    if f == 'version.txt': return 'changelog.txt'
    if f not in files: return None
    if tag and f'***** <{tag}>' not in open(f'{H}/{f}', encoding='utf-8').read(): tag = ''  # upstream dangling tag: game shows file top
    return '#' + fid(f) + ('-' + tag if tag else '')
def line_html(s):
    out, color, i, in_tag = [], None, 0, ''
    while i < len(s):
        j = s.find(in_tag, i) if in_tag else s.find('[[[[', i)
        if j < 0: j = len(s)
        seg = html.escape(s[i:j])
        out.append(f'<span style="color:{color}">{seg}</span>' if color and seg else seg)
        i = j
        if i >= len(s): break
        if in_tag: i += 1; in_tag = ''; color = None; continue
        c = s[i+4:i+5]
        if c not in COL or i + 5 >= len(s): out.append('[[[['); i += 4; continue
        color = COL[c]; in_tag = s[i+5]; i += 6
    return ''.join(out)
order, seen = [], set()
def visit(f):
    if f in seen or f not in files: return
    seen.add(f); order.append(f)
    for m in re.finditer(r'^\*\*\*\*\* \[.\] (\S+)', open(f'{H}/{f}', encoding='utf-8').read(), re.M):
        visit(m.group(1).partition('#')[0])
visit('help.hlp')
order += [f for f in files if f not in seen]
toc, body, dangling = [], [], []
for f in order:
    lines = open(f'{H}/{f}', encoding='utf-8').read().split('\n')
    links = {m.group(1): m.group(2) for m in (re.match(r'\*\*\*\*\* \[(.)\] (\S+)', l) for l in lines) if m}
    first = next((l for l in lines if l.strip()), f)
    title = re.sub(r'^\s*=+\s*|\s*=+\s*$', '', re.sub(r'\[\[\[\[.(.)', '', first)).rstrip('.|') or f
    toc.append(f'<a href="#{fid(f)}">{html.escape(title)}</a>')
    out = []
    for l in lines:
        m = re.match(r'\*\*\*\*\* <(.+)>', l)
        if m: out.append(f'<a id="{fid(f)}-{html.escape(m.group(1))}"></a>'); continue
        if l.startswith('***** '): continue
        h = line_html(l)
        def rep(m):
            t = links.get(m.group(2))
            if t is None: return m.group(0)
            href = target(t)
            if href is None: dangling.append(f'{f}: {t}'); return m.group(0)
            return f'<a href="{href}">{m.group(0)}</a>'
        h = re.sub(r'(\[|\()(.)(\]|\))', lambda m: rep(m) if (m.group(1) + m.group(3)) in ('[]', '()') else m.group(0), h)
        out.append(h)
    body.append(f'<section id="{fid(f)}"><h2>{html.escape(title)} <small><code>{f}</code></small></h2><pre>' + '\n'.join(out).strip('\n') + '</pre></section>')
print(f'''<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Hengband · Manual</title>
<link href="https://fonts.googleapis.com/css2?family=Cinzel:wght@600;800&family=IBM+Plex+Mono:wght@400;600&display=swap" rel="stylesheet">
<link rel="stylesheet" href="../shrine.css">
<style>pre {{ white-space:pre-wrap; overflow-wrap:anywhere; font-size:13px; background:var(--stone); border:1px solid var(--line); border-radius:6px; padding:12px 14px; overflow-x:auto; }} h2 small {{ font-size:12px; }}</style>
</head><body><div class="wrap">
<nav><a href="../hengband.html">← Hengband shrine</a> · <a href="../../hengband/">Play Hengband</a></nav>
<header><h1>Hengband manual</h1><p class="epi">The English in-game help files (<code>lib/help/*.txt</code> and <code>*.hlp</code>, the <kbd>?</kbd> command) of Hengband 3.0.2.4-Beta, colour markup kept. Copied under the Hengband licence (<a href="license.txt">notice</a>).</p></header>
<nav class="toc">{''.join(toc)}</nav>
{chr(10).join(body)}
</div></body></html>''')
import sys; print('\n'.join(sorted(set(dangling))), file=sys.stderr)

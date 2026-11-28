#!/usr/bin/env python3
"""Fail-closed checks before a cloze catalog is considered publishable."""
import re, sys, json
from pathlib import Path

text = Path('game_proto/main/cloze_text.h').read_text()
pat = re.compile(r'\{"([^"]*)",\s*"([^"]*)",\s*"((?:\\.|[^"])*)",\s*(\d+),\s*(\d+),\s*\{\s*(.*?)\n\s*\}\}', re.S)
entries=[]
for m in pat.finditer(text):
    title,author,orig,poetry,alt,body=m.groups()
    try: orig=json.loads('"'+orig+'"')
    except Exception: pass
    lv=[]
    for q in re.finditer(r'"((?:\\.|[^"])*)"',body):
        try: lv.append(json.loads('"'+q.group(1)+'"'))
        except Exception: lv.append(q.group(1))
    entries.append((title,author,orig,int(poetry),int(alt),lv))

# glyph coverage
font = Path('game_proto/main/chushi_font20.h').read_text()
glyphs=set(int(x,16) for x in re.findall(r'\{0x([0-9A-Fa-f]{4,6}),',font))

errors=[]; warnings=[]; titles={}; alts=0
for title,author,orig,poetry,alt,lv in entries:
    if not title or not author or author=='—': errors.append(f'{title}: 标题/作者缺失')
    if not orig: errors.append(f'{title}: 正文为空')
    if len(lv)!=5: errors.append(f'{title}: 级别数 {len(lv)} != 5')
    if title in titles: warnings.append(f'重复标题: {title}')
    titles[title]=1
    if alt: alts+=1
    if not all(len(x)==len(orig) for x in lv):
        errors.append(f'{title}: 遮罩长度与正文不一致')
        continue
    for li,mask in enumerate(lv,1):
        for a,b in zip(orig,mask):
            if b=='○' and not ('\u4e00' <= a <= '\u9fff'):
                errors.append(f'{title}: L{li} 遮罩了非汉字 {a!r}')
                break
            if b not in (a,'○'):
                errors.append(f'{title}: L{li} 遮罩改写了原文 {a!r}->{b!r}')
                break
    # unmasked round-trip
    if any(''.join(a if b!='○' else '' for a,b in zip(orig,m)) != ''.join(a for a,b in zip(orig,m) if b!='○') for m in lv):
        errors.append(f'{title}: 遮罩无法无损还原')
    for ch in set(orig):
        if ord(ch)>=0x3400 and ord(ch) not in glyphs and ch!='○':
            errors.append(f'{title}: 字库缺字 {ch!r} U+{ord(ch):04X}')

print(f'entries={len(entries)} alt={alts} errors={len(errors)} warnings={len(warnings)}')
for e in errors[:50]: print('ERROR',e)
for w in warnings[:20]: print('WARN',w)
Path('docs/flashcards/catalog-validation.txt').write_text(
    f'entries={len(entries)}\nalt={alts}\nerrors={len(errors)}\nwarnings={len(warnings)}\n\n' +
    '\n'.join('ERROR '+e for e in errors)+'\n'+'\n'.join('WARN '+w for w in warnings)+'\n')
sys.exit(1 if errors else 0)

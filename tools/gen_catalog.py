#!/usr/bin/env python3
import json, glob, re, sys, random
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from textbook_catalog import PRIMARY_LOW, PRIMARY_HIGH, PRIMARY_PROSE, MIDDLE_POEMS, MIDDLE_PROSE, HIGH_PROSE, HIGH_POEMS
from alt_texts import ALT

# Current verified/full texts, so existing curated texts win over dataset variants.
def parse_current():
    s=Path('game_proto/main/cloze_text.h').read_text(); out={}
    pat=re.compile(r'\{"([^"]+)",\s*"([^"]+)",\s*"((?:\\.|[^"])*)",\s*\{',re.S)
    for m in pat.finditer(s):
        try: txt=json.loads('"'+m.group(3)+'"')
        except Exception: continue
        out[m.group(1)]=txt
    return out
CUR={
"司马光":"群儿戏于庭，一儿登瓮，足跌没水中，众皆弃去，光持石击瓮破之，水迸，儿得活。",
"守株待兔":"宋人有耕者。田中有株。兔走触株，折颈而死。因释其耒而守株，冀复得兔。兔不可复得，而身为宋国笑。",
}


# dataset
D={}
for f in glob.glob('/tmp/chinese-gushiwen/guwen/*.json'):
    for line in open(f):
        try: d=json.loads(line)
        except: continue
        t=d.get('title','').strip(); c=d.get('content','').strip()
        if t and c and (t not in D or len(c)>len(D[t].get('content',''))): D[t]=d
def norm(x):
    x=x.replace('《','').replace('》','').replace(' ','')
    x=re.sub(r'[（(].*?[）)]','',x)
    return x.strip()
DN={norm(k):v for k,v in D.items()}
ALIAS={
 '早发白帝城':'早发白帝城 / 白帝下江陵','饮湖上初晴后雨':'饮湖上初晴后雨二首·其二',
 '惠崇春江晚景':'惠崇春江晚景二首 / 惠崇春江晓景二首','雪梅':'雪梅·其一',
 '别董大':'别董大二首','四时田园杂兴':'四时田园杂兴·其二',
 '秋夜将晓出篱门迎凉有感':'秋夜将晓出篱门迎凉有感二首','回乡偶书':'回乡偶书二首·其一',
 '早春呈水部张十八员外':'早春呈水部张十八员外 / 初春小雨',
 '伯牙鼓琴':'伯牙绝弦','铁杵成针':'铁杵磨针','两小儿辩日':'两小儿辩日 / 两小儿辩斗',
 '木兰辞':None,'送杜少府之任蜀州':'送杜少府之任蜀州 / 送杜少府之任蜀川',
 '黄鹤楼':'黄鹤楼 / 登黄鹤楼','北冥有鱼':None,'虽有嘉肴':None,'出师表':'出师表 / 前出师表',
 '与谢中书书':'答谢中书书','马说':'杂说四·马说','记承天寺夜游':'记承天寺夜游 / 记承天夜游',
 '归园田居（其一）':'归园田居·其一','拟行路难（其四）':'拟行路难·其四',
 '梦游天姥吟留别':'梦游天姥吟留别 / 梦游天姥山别东鲁诸公','望海潮':'望海潮·东南形胜',
 '书愤':'书愤五首·其一','扬州慢':'扬州慢·淮左名都','赤壁赋':'前赤壁赋',
 '报任安书（节选）':'报任少卿书 / 报任安书','归去来兮辞':'归去来兮辞·并序',
 '答司马谏议书':None,'子路、曾皙、冉有、公西华侍坐':None,'礼运（节选）':None,'长亭送别（节选）':None,
}
def find(title):
    if title in ALT: return ALT[title], '—', True
    if title in CUR: return CUR[title], '—', False
    if title in D: return D[title]['content'], D[title].get('writer',''), False
    a=ALIAS.get(title)
    if a and a in D: return D[a]['content'], D[a].get('writer',''), False
    if norm(title) in DN:
        d=DN[norm(title)]; return d['content'], d.get('writer',''), False
    return '', '', False

STOP=set('的了和在有把被并且于以之为其而则也矣焉哉乎兮所与因若虽然故乃遂')
PUNCT=set('，。；：！？、\n “”《》')
def cjk(c): return '\u4e00'<=c<='\u9fff'
def punct(c): return c in PUNCT or c.isspace()
def masks(text):
    import jieba
    spans=[]; pos=0
    for w in jieba.lcut(text): spans.append((w,pos,pos+len(w))); pos+=len(w)
    out=[]
    for lv,pct in enumerate([.20,.40,.60,.80,1.0],1):
        mask=[False]*len(text)
        if lv==5:
            for i,c in enumerate(text):
                if cjk(c): mask[i]=True
        else:
            sents=[]; cur=[]
            for ti,(w,a,b) in enumerate(spans):
                if all(punct(c) for c in w):
                    if cur: sents.append(cur); cur=[]
                else: cur.append(ti)
            if cur: sents.append(cur)
            target=int(sum(1 for c in text if cjk(c))*pct); rng=random.Random(lv*7919+len(text))
            anchors=set(); cands=[]
            for sent in sents:
                anc=None
                for ti in sent:
                    w,a,b=spans[ti]
                    if any(cjk(c) for c in w): anc=ti; break
                if anc is not None: anchors.add(anc)
                for ti in sent:
                    if lv<3 and ti==anc: continue
                    w,a,b=spans[ti]; cw=[c for c in w if cjk(c)]
                    if not cw: continue
                    if lv<3 and len(cw)>4: continue
                    if lv<3 and all(c in STOP for c in cw): continue
                    cands.append(ti)
            rng.shuffle(cands); seen=set(); cnt=0
            for ti in cands:
                if lv<3 and ((ti-1) in seen or (ti+1) in seen): continue
                w,a,b=spans[ti]; n=sum(1 for i in range(a,b) if cjk(text[i]))
                seen.add(ti); cnt+=n
                if cnt>=target: break
            for ti in seen:
                w,a,b=spans[ti]
                for i in range(a,b):
                    if cjk(text[i]): mask[i]=True
        out.append(''.join('○' if m and cjk(c) else c for c,m in zip(text,mask)))
    return out

STAGES=[
 ('小学低年级',[('诗词',t) for t in PRIMARY_LOW]),
 ('小学中高年级',[('诗词',t) for t in PRIMARY_HIGH]+[('文言文',t) for t in PRIMARY_PROSE]),
 ('初中',[('诗词曲',t) for t in MIDDLE_POEMS]+[('文言文',t) for t in MIDDLE_PROSE]),
 ('高中',[('文言文',t) for t in HIGH_PROSE]+[('诗词曲',t) for t in HIGH_POEMS]),
]
# Build generated files
card_names=['CLOZE_LOW_CARDS','CLOZE_MIDHIGH_CARDS','CLOZE_MIDDLE_CARDS','CLOZE_HIGH_CARDS']
text_names=['CLOZE_LOW','CLOZE_MIDHIGH','CLOZE_MIDDLE','CLOZE_HIGH']
cards_hdr='''#pragma once
#include <stdint.h>
struct PoemCard { const char *title; const char *author; const char *lines[4]; const char *note; uint8_t mask; };
'''
texts_hdr='''#pragma once
#include <stdint.h>
struct ClozeText { const char *title; const char *author; const char *original; uint8_t poetry; uint8_t alt; const char *levels[5]; };
'''
counts=[]; missing=[]; matched=0
for si,(stage,items) in enumerate(STAGES):
    cards=[]; texts=[]; cnt=0
    for typ,title in items:
        print(f'[{si}] {title}', flush=True)
        orig,author,is_alt=find(title)
        poetry = 1 if typ!='文言文' else 0
        if not orig:
            missing.append((stage,title)); orig=''; author=author or '—'
        else:
            matched+=1
            # add punctuation to short 4-line poems only if absent; full dataset usually has punctuation
            if poetry and not any(ch in orig for ch in '，。！？；：'):
                pass
        lv=masks(orig) if orig else ['','','','','']
        cards.append({'title':title,'author':author or '—','poetry':poetry})
        texts.append({'title':title,'author':author or '—','orig':orig,'poetry':poetry,'alt':1 if is_alt else 0,'levels':lv})
        cnt+=1
    cards_hdr += f'static const PoemCard {card_names[si]}[] = {{\n'
    for c in cards:
        cards_hdr += '    {'+json.dumps(c['title'],ensure_ascii=False)+', '+json.dumps(c['author'],ensure_ascii=False)+', {"","","",""}, "", 2},\n'
    cards_hdr += '};\n\n'
    texts_hdr += f'static const ClozeText {text_names[si]}[] = {{\n'
    for t in texts:
        texts_hdr += '    {'+json.dumps(t['title'],ensure_ascii=False)+', '+json.dumps(t['author'],ensure_ascii=False)+', '+json.dumps(t['orig'],ensure_ascii=False)+f", {t['poetry']}, {t['alt']}, {{\n"
        texts_hdr += '        '+',\n        '.join(json.dumps(x,ensure_ascii=False) for x in t['levels'])+'\n    }},\n'
    texts_hdr += '};\n\n'
    counts.append((stage,cnt))
Path('game_proto/main/poem_cards.h').write_text(cards_hdr)
Path('game_proto/main/cloze_text.h').write_text(texts_hdr)
groups='''#pragma once
#include "poem_cards.h"
#include "cloze_text.h"
struct ClozeGroup { const char *name; const char *sub; const PoemCard *items; int count; };
static const ClozeGroup CLOZE_GROUPS[] = {
'''
subs=[f'{c:02d} 条' for _,c in counts]
for i,(stage,cnt) in enumerate(counts):
    groups += f'    {{{json.dumps(stage,ensure_ascii=False)}, {json.dumps(subs[i],ensure_ascii=False)}, {card_names[i]}, {cnt}}},\n'
groups += '};\nstatic const int CLOZE_GROUP_COUNT = 4;\n'
Path('game_proto/main/cloze_content.h').write_text(groups)
print('counts',counts,'matched',matched,'missing',len(missing))
for x in missing: print('MISSING',x)

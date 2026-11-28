#!/usr/bin/env python3
"""Turn cloze_events.jsonl into fragile-point candidates."""
import json, sys, collections
from pathlib import Path

path = Path(sys.argv[1] if len(sys.argv) > 1 else 'cloze_events.jsonl')
items = {}
sessions = collections.defaultdict(lambda: {'events': 0, 'reveals': 0, 'early': 0, 'denied': 0, 'level_downs': 0})
for line in path.open():
    try: e = json.loads(line)
    except Exception: continue
    if e.get('type') != 'event': continue
    key = (e.get('group',''), e.get('title',''))
    it = items.setdefault(key, {
        'enters':0,'reveals':0,'early':0,'denied':0,'level_downs':0,
        'pages':set(),'levels':[],'last_ms':None,'dwell_ms':0,'reveal_after_down':0,
    })
    ev=e.get('event','')
    if ev=='enter': it['enters']+=1
    if ev=='reveal_on': it['reveals']+=1
    if ev=='reveal_early': it['early']+=1
    if ev=='reveal_denied': it['denied']+=1
    if ev=='level_down': it['level_downs']+=1
    if ev=='page_next': it['pages'].add(e.get('page'))
    if ev.startswith('level_'): it['levels'].append(e.get('level',0))
    if ev=='reveal_on' and it['level_downs']>0: it['reveal_after_down']+=1
    t=e.get('uptime_ms')
    if t is not None:
        if it['last_ms'] is not None: it['dwell_ms'] += max(0,t-it['last_ms'])
        it['last_ms']=t
print('title,enters,reveals,early,denied,level_downs,reveal_after_down,flags')
for (g,t),it in items.items():
    flags=[]
    if it['enters']>0 and it['reveals']/it['enters'] >= 1.0: flags.append('high_reveal')
    if it['early']: flags.append('reveal_too_early')
    if it['reveal_after_down']: flags.append('hint_after_level_down')
    if it['level_downs'] >= 2: flags.append('frequent_level_down')
    if it['enters'] >= 3 and not it['levels']: flags.append('open_no_level')
    print(f'{t},{it["enters"]},{it["reveals"]},{it["early"]},{it["denied"]},{it["level_downs"]},{it["reveal_after_down"]},{"|".join(flags)}')

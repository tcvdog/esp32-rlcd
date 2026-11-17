import csv, json, pathlib, requests, yaml, subprocess, concurrent.futures, time, sys
ROOT=pathlib.Path('/mnt/documents/桌面/web游戏/esp32-rlcd')
OUT=ROOT/'tts_build'; OUT.mkdir(exist_ok=True)
key=yaml.safe_load((pathlib.Path.home()/'.dsh/.credentials.yaml').read_text())['refs']['DEEPSEEK_API_KEY']
rows=[]
with (ROOT/'data/ket_pet_words.tsv').open(encoding='utf-8') as f:
 for r in csv.DictReader(f,delimiter='\t'):
  if r['level'] in ('KET','PET'): rows.append(r)
counts={'KET':0,'PET':0}; tasks=[]
for r in rows:
 counts[r['level']]+=1
 idx=counts[r['level']]
 name=f"{r['level'].lower()}_{idx:03d}"
 tasks.append((name,r))
print('tasks',len(tasks),counts,flush=True)

def one(item):
 name,r=item
 wav=OUT/(name+'.wav'); mp3=OUT/(name+'.mp3')
 if mp3.exists() and mp3.stat().st_size>1000: return name,'cached'
 for attempt in range(4):
  try:
   resp=requests.post('https://api.deepseek.com/v1/tools/tts',
     headers={'Authorization':'Bearer '+key,'Content-Type':'application/json'},
     json={'text':r['sentence'],'voice_id':'stella','format':'wav'},timeout=90)
   if resp.status_code==200 and len(resp.content)>1000:
    wav.write_bytes(resp.content)
    cp=subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-i',str(wav),'-codec:a','libmp3lame','-b:a','48k','-ar','24000','-ac','1',str(mp3)],capture_output=True)
    if mp3.exists() and mp3.stat().st_size>1000: return name,'ok'
    raise RuntimeError(cp.stderr.decode()[:200])
   if resp.status_code==429: time.sleep(2*(attempt+1)); continue
   raise RuntimeError(f'http {resp.status_code} {resp.text[:120]}')
  except Exception as e:
   if attempt==3: return name,'ERR '+repr(e)
   time.sleep(1.5*(attempt+1))
 return name,'ERR'

with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
 for i,(name,status) in enumerate(ex.map(one,tasks),1):
  if status.startswith('ERR'): print(name,status,flush=True)
  if i%20==0: print('progress',i,'/',len(tasks),flush=True)
print('done files',len(list(OUT.glob('*.mp3'))),flush=True)

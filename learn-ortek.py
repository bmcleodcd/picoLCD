#!/usr/bin/env python3
"""Learn LCD command -> USB HID key sequence while dongle remains connected.
Run as root on Linux with panel.service and the Ortek USB receiver.
Writes a reviewable map; never installs it.
"""
import argparse,glob,json,os,re,selectors,struct,subprocess,time
from collections import deque
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',default='learned-ortek.map',help='review map output path (default: %(default)s)')
args=parser.parse_args()
keys={1:'Escape',14:'BackSpace',15:'Tab',28:'Return',57:'space',
 71:'KP_7',72:'KP_8',73:'KP_9',74:'KP_Subtract',75:'KP_4',76:'KP_5',77:'KP_6',78:'KP_Add',79:'KP_1',80:'KP_2',81:'KP_3',82:'KP_0',83:'KP_Decimal',96:'KP_Enter',98:'KP_Divide',55:'KP_Multiply',
 102:'Home',103:'Up',104:'Prior',105:'Left',106:'Right',107:'End',108:'Down',109:'Next',110:'Insert',111:'Delete',
 113:'XF86AudioMute',114:'XF86AudioLowerVolume',115:'XF86AudioRaiseVolume',116:'XF86PowerOff',142:'XF86Sleep',
 128:'XF86AudioStop',163:'XF86AudioNext',164:'XF86AudioPlay',165:'XF86AudioPrev',166:'XF86AudioStop',172:'XF86HomePage',
 207:'XF86AudioPlay',208:'XF86AudioForward',168:'XF86AudioRewind',119:'Pause'}
for codes,chars in [(range(2,12),'1234567890'),(range(16,26),'qwertyuiop'),(range(30,39),'asdfghjkl'),(range(44,51),'zxcvbnm')]: keys.update(zip(codes,chars))
keys.update({59+i:'F'+str(i+1) for i in range(10)})
mods={29:'ctrl',97:'ctrl',42:'shift',54:'shift',56:'alt',100:'alt',125:'super',126:'super'}
sel=selectors.DefaultSelector(); handles=[]; held=set(); events=deque(); pending=[]; learned={}; conflicts=set()
for block in open('/proc/bus/input/devices').read().split('\n\n'):
 if 'Vendor=05a4 Product=9881' not in block:continue
 match=re.search(r'Handlers=.*?\b(event\d+)\b',block)
 if match:
  f=open('/dev/input/'+match[1],'rb',buffering=0); handles.append(f); sel.register(f,selectors.EVENT_READ,'input')
assert handles,'USB Ortek receiver is required during learning'
proc=subprocess.Popen(['journalctl','-u','panel.service','-f','-n','0','-o','json'],stdout=subprocess.PIPE,bufsize=0)
sel.register(proc.stdout,selectors.EVENT_READ,'journal')
event=struct.Struct('@llHHi'); started=time.monotonic(); print('LEARNING_READY',flush=True)
try:
 while time.monotonic()-started<180:
  for item,_ in sel.select(.1):
   if item.data=='journal':
    line=item.fileobj.readline()
    if not line:continue
    record=json.loads(line); m=re.search(r'Ortek IR command=(\d+)',record.get('MESSAGE',''))
    if m:print("IR",int(record["__REALTIME_TIMESTAMP"])/1e6,int(m[1]),flush=True); pending.append((int(record["__REALTIME_TIMESTAMP"])/1e6,int(m[1])))
   else:
    raw=os.read(item.fileobj.fileno(),event.size*64)
    for offset in range(0,len(raw),event.size):
     sec,usec,typ,code,value=event.unpack_from(raw,offset)
     if typ!=1:continue
     if code in mods:
      if value:held.add(code)
      else:held.discard(code)
     elif value==1:
      name=keys.get(code)
      if code in (272,273):name='mouse:'+str(1 if code==272 else 3)
      if name:
       prefix='+'.join(dict.fromkeys(mods[c] for c in sorted(held)))
       seq=(prefix+'+' if prefix else '')+name
       events.append((sec+usec/1e6,seq,code))
      print("USB",sec+usec/1e6,seq if name else "unknown",code,flush=True)
  now=time.time()
  while pending and now-pending[0][0]>.35:
   stamp,command=pending.pop(0)
   candidates={(seq,code) for t,seq,code in events if -.12 < t-stamp < .04}
   if len(candidates)==1:
    seq,code=candidates.pop()
    repeat=int(code in (103,108,105,106,104,109,114,115))
    if command in learned and learned[command]!=(repeat,seq):conflicts.add(command)
    learned[command]=(repeat,seq)
    print('LEARNED',command,repeat,seq,flush=True)
   else:print('UNRESOLVED',stamp,command,sorted(candidates),flush=True)
  while events and now-events[0][0]>2:events.popleft()
finally:
 proc.terminate(); proc.wait()
 for f in handles:f.close()
 with open(args.output,'w') as f:
  f.write('# Review before installing as /etc/picolcd-ortek.map\n')
  for command,(repeat,seq) in sorted(learned.items()):
   if command not in conflicts:f.write(f'{command} {repeat} {seq}\n')
 print('FINISHED',len(learned),'commands; conflicts:',sorted(conflicts),flush=True)

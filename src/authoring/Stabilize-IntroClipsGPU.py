"""Offline time-aware curve smoothing; retain exact audio timestamps and IDs.

This eases the custom curve envelope; it is not a live vanilla/A2F pose blend.
Audio, runtime clock, clip schema and model inference remain unchanged.
"""
import json,struct,hashlib,re
from pathlib import Path
import numpy as np
import torch
root=Path(__file__).resolve().parents[2];out=root/'output/intro-a2f'
study=root/'research/intro-a2f/correction-0.2.5';prior=study/'baseline-0.2.4'
assert torch.cuda.is_available()
names=json.loads((out/'channels.json').read_text());scene=json.loads((out/'scene-manifest.json').read_text())
report=[];hashes=[];audit=json.loads((prior/'clip-audit.json').read_text())
def fnv(b):
 h=14695981039346656037
 for x in b:h=((h^x)*1099511628211)&0xffffffffffffffff
 return h
def smoothstep(x):
 x=x.clamp(0,1);return x*x*(3-2*x)
for line in scene['Lines']:
 path=prior/'clips'/line['Clip'];blob=path.read_bytes();header=struct.unpack_from('<6IQ',blob)
 _,_,channels,count,rate,samples,_=header;assert channels==38 and rate==16000
 frames=np.frombuffer(blob[32:],dtype=np.dtype([('t','<u4'),('w','<f4',(channels,))]))
 time=torch.tensor(frames['t'].copy(),device='cuda',dtype=torch.float32)/rate
 old=torch.tensor(frames['w'].copy(),device='cuda');duration=samples/rate
 oldfade=torch.minimum((time/.04).clamp(0,1),((duration-time)/.06).clamp(0,1))
 base=old[:,:37]/oldfade.clamp_min(.0001)[:,None]
 base[0]=base[1];base[-1]=base[-2]
 assert torch.isfinite(base).all() and float(base.max())<1.001
 new=torch.empty_like(base)
 # Symmetric temporal kernels avoid a deliberate phase delay. Wider treatment
 # of slower expression channels leaves fast speech articulation more intact.
 temporal_distance=time[:,None]-time[None,:]
 for sigma,indices in [(.016,[i for i,n in enumerate(names[:37]) if n.startswith(('jaw','mouth'))]),
                       (.040,[i for i,n in enumerate(names[:37]) if not n.startswith(('jaw','mouth'))])]:
  kernel=torch.exp(-.5*(temporal_distance/sigma).square());kernel[temporal_distance.abs()>3*sigma]=0
  kernel/=kernel.sum(1,keepdim=True);new[:,indices]=kernel@base[:,indices]
 start=smoothstep(time/.09);end=smoothstep((duration-time)/.10)
 new*=torch.minimum(start,end)[:,None];new[0]=0;new[-1]=0;new=new.clamp(0,1)
 # Retain schema-compatible product although the legacy seal morph is empty.
 seal=new[:,names.index('jawOpen')]*new[:,names.index('mouthClose')]
 new=torch.cat([new,seal[:,None]],1)
 assert torch.isfinite(new).all() and new[0].abs().max()==0 and new[-1].abs().max()==0
 values=new.cpu().numpy().astype('<f4')
 data=blob[:32]+b''.join(struct.pack('<I',int(t))+w.tobytes() for t,w in zip(frames['t'],values))
 (out/'clips'/line['Clip']).write_bytes(data);h=fnv(data);hashes.append(h)
 dt=time[1:]-time[:-1];assert (dt>0).all()
 oldv=(old[1:,:37]-old[:-1,:37])/dt[:,None];newv=(new[1:,:37]-new[:-1,:37])/dt[:,None]
 startmask=time[1:]<=.1
 row={'StringRef':line['StringRef'],'Actor':line['Actor'],'OldOnsetPeakWeightSpeed':float(oldv[startmask].abs().max()),'NewOnsetPeakWeightSpeed':float(newv[startmask].abs().max()),'OldMeanSquaredAcceleration':float(((oldv[1:]-oldv[:-1])/(.5*(dt[1:]+dt[:-1]))[:,None]).square().mean()),'NewMeanSquaredAcceleration':float(((newv[1:]-newv[:-1])/(.5*(dt[1:]+dt[:-1]))[:,None]).square().mean()),'TimestampsUnchanged':True,'Frames':count,'SHA256':hashlib.sha256(data).hexdigest()}
 report.append(row)
 entry=next(x for x in audit['Clips'] if x['StringRef']==line['StringRef'])
 entry.update(SHA256=row['SHA256'],FNV64=f'{h:016x}',MaxWeight=float(new.max()),NonzeroChannels=int((new.max(0).values>.001).sum()))
 assert hashlib.sha256((out/line['Audio']).read_bytes()).hexdigest()==entry['AudioSHA256']
source=root/'src/asi/single-line-bridge/GeneratedScene.hpp'
text=(prior/'source/GeneratedScene.hpp').read_text()
replacement='inline constexpr std::uint64_t SceneClipHashes[] = {\n'+''.join(f'    0x{h:016x}ull,\n' for h in hashes)+'};'
text,n=re.subn(r'inline constexpr std::uint64_t SceneClipHashes\[\] = \{.*?\};',lambda _:replacement,text,flags=re.S);assert n==1
source.write_text(text)
audit['TemporalProcessing']={'Device':'CUDA','From':'preserved0.2.4 solved curves with prior endpoint fade removed','SymmetricSigmaSeconds':{'Speech':.016,'Expression':.040},'SmoothstepEntrySeconds':.09,'SmoothstepExitSeconds':.10,'AudioOrTimestampsChanged':False,'LiveVanillaPoseCrossfade':False}
(root/'research/intro-a2f/clip-audit.json').write_text(json.dumps(audit,indent=2))
summary={'GPU':torch.cuda.get_device_name(),'Clips':len(report),'OldMeanOnsetPeakSpeed':float(np.mean([x['OldOnsetPeakWeightSpeed'] for x in report])),'NewMeanOnsetPeakSpeed':float(np.mean([x['NewOnsetPeakWeightSpeed'] for x in report])),'OldMeanSquaredAcceleration':float(np.mean([x['OldMeanSquaredAcceleration'] for x in report])),'NewMeanSquaredAcceleration':float(np.mean([x['NewMeanSquaredAcceleration'] for x in report])),'Records':report,'RuntimeValidated':False,'Limitation':'smooths custom animation; immediate change from vanilla ownership can still produce a boundary discontinuity'}
assert summary['NewMeanOnsetPeakSpeed']<summary['OldMeanOnsetPeakSpeed']
assert summary['NewMeanSquaredAcceleration']<summary['OldMeanSquaredAcceleration']
(study/'temporal-audit.json').write_text(json.dumps(summary,indent=2))
print(json.dumps({k:v for k,v in summary.items() if k!='Records'},indent=2))

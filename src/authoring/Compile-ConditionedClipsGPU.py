"""Compile audio-relative conditioned curves; runtime owns the boundary blend."""
import csv,json,struct,hashlib,re,argparse
from pathlib import Path
import numpy as np,torch
root=Path(__file__).resolve().parents[2];out=root/'output/intro-a2f';study=root/'research/intro-a2f/correction-0.2.6'
p=argparse.ArgumentParser();p.add_argument('--raw',type=Path,required=True);p.add_argument('--emotion',default='zero baseline');p.add_argument('--promote',action='store_true');p.add_argument('--preview-only',action='store_true');args=p.parse_args()
assert not (args.promote and args.preview_only)
assert torch.cuda.is_available()
names=json.loads((out/'channels.json').read_text());scene=json.loads((out/'scene-manifest.json').read_text());dest=study/'candidate-clips';dest.mkdir(exist_ok=True)
audit=json.loads((study/'baseline-0.2.5/clip-audit.json').read_text());hashes=[];records=[]
def fnv(b):
 h=14695981039346656037
 for x in b:h=((h^x)*1099511628211)&0xffffffffffffffff
 return h
for line in scene['Lines']:
 if args.preview_only and line['StringRef'] not in [102169,102170,65217]:continue
 rawpath=args.raw/f"VO_{line['StringRef']}_M.csv"
 rows=list(csv.reader(rawpath.open()));rn=rows[0][1:];raw=np.array(rows[1:],dtype=np.float64)
 assert raw.shape[1]==53 and np.isfinite(raw).all()
 old=(study/'baseline-0.2.5/clips'/line['Clip']).read_bytes();header=struct.unpack_from('<6IQ',old)
 _,_,channels,count,rate,samples,schema=header
 frames=np.frombuffer(old[32:],dtype=np.dtype([('t','<u4'),('w','<f4',(channels,))]))
 rt=raw[:,0];assert np.all(np.diff(rt)>0)
 values=raw[:,[1+rn.index(n) for n in names[:37]]]
 assert values.min()>-.0001 and values.max()<1.0001
 times=frames['t'].copy();time=torch.tensor(times.astype('f4'),device='cuda')/rate
 w=torch.tensor(np.stack([np.interp(times,rt,values[:,i]) for i in range(37)],1),device='cuda',dtype=torch.float32).clamp(0,1)
 smooth=torch.empty_like(w);dist=time[:,None]-time[None,:]
 for sigma,indices in [(.016,[i for i,n in enumerate(names[:37]) if n.startswith(('jaw','mouth'))]),(.04,[i for i,n in enumerate(names[:37]) if not n.startswith(('jaw','mouth'))])]:
  kernel=torch.exp(-.5*(dist/sigma).square());kernel[dist.abs()>3*sigma]=0;kernel/=kernel.sum(1,keepdim=True)
  smooth[:,indices]=kernel@w[:,indices]
 # Endpoint neutrality is retained for the portable clip format. No baked fade:
 # the runtime uses the SAME audio-relative alpha for vanilla and custom layers.
 smooth[0]=0;smooth[-1]=0
 seal=smooth[:,names.index('jawOpen')]*smooth[:,names.index('mouthClose')]
 new=torch.cat([smooth,seal[:,None]],1).cpu().numpy().astype('<f4')
 assert np.isfinite(new).all() and new.min()>=0 and new.max()<=1
 data=old[:32]+b''.join(struct.pack('<I',int(t))+w.tobytes() for t,w in zip(times,new))
 (dest/line['Clip']).write_bytes(data);h=fnv(data);hashes.append(h)
 entry=next(x for x in audit['Clips'] if x['StringRef']==line['StringRef'])
 entry.update(SHA256=hashlib.sha256(data).hexdigest(),FNV64=f'{h:016x}',MaxWeight=float(new.max()),NonzeroChannels=int((new.max(0)>.001).sum()),SourceCSV_SHA256=hashlib.sha256(rawpath.read_bytes()).hexdigest())
 assert hashlib.sha256((out/line['Audio']).read_bytes()).hexdigest()==entry['AudioSHA256']
 records.append({'StringRef':line['StringRef'],'AudioAndTimestampsUnchanged':True,'SHA256':entry['SHA256']})
audit['EmotionInput']=args.emotion
audit['TemporalProcessing']={'Device':'CUDA','SpeechSigma':.016,'ExpressionSigma':.04,'BakedEntryExitFade':False,'RuntimePoseAndMorphEnvelopeSeconds':.16,'LiveVanillaPoseCrossfade':True,'RuntimeValidated':False}
(study/'candidate-clip-audit.json').write_text(json.dumps(audit,indent=2))
(study/'temporal-audit.json').write_text(json.dumps({'GPU':torch.cuda.get_device_name(),'Records':records,'SameEnvelopeForNativeFacialOutputAndCustomMorphs':True,'ActualGameValidationPending':True},indent=2))
if args.promote:
 import shutil
 for line in scene['Lines']:shutil.copy2(dest/line['Clip'],out/'clips'/line['Clip'])
 source=root/'src/asi/single-line-bridge/GeneratedScene.hpp';text=source.read_text()
 replacement='inline constexpr std::uint64_t SceneClipHashes[] = {\n'+''.join(f'    0x{h:016x}ull,\n' for h in hashes)+'};'
 text,n=re.subn(r'inline constexpr std::uint64_t SceneClipHashes\[\] = \{.*?\};',lambda _:replacement,text,flags=re.S);assert n==1;source.write_text(text)
 (root/'research/intro-a2f/clip-audit.json').write_text(json.dumps(audit,indent=2))
print('Compiled',len(records),'CUDA-conditioned clips; promoted:',args.promote)

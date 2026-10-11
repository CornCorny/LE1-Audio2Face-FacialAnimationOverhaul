"""Measure actual NVIDIA geometry versus solved source shapes and LE controls."""
from pathlib import Path
import json,struct,csv,numpy as np,torch
root=Path(__file__).resolve().parents[2];study=root/'research/intro-a2f/correction-0.2.6';out=root/'output/intro-a2f'
assert torch.cuda.is_available()
def t(a):return torch.tensor(a,device='cuda',dtype=torch.float32)
source=np.load(root/'models/A2F-v3.0/bs_skin_Mark.npz');neutral=t(source['neutral'])
channels=json.loads((out/'channels.json').read_text())[:-1]
report=[]
for actor,id in [('Kaidan',102169),('Joker',102170),('Shepard',65217)]:
 data=(study/f'source-geometry/VO_{id}_M-neutral.bin').read_bytes();offset=0;times=[];frames=[]
 while offset<len(data):
  tm,n=struct.unpack_from('<qI',data,offset);offset+=12
  assert n==72006
  frames.append(np.frombuffer(data[offset:offset+n*4],dtype='<f4').copy().reshape(-1,3));times.append(tm);offset+=n*4
 raw=t(np.stack(frames));times=np.array(times);assert np.all(np.diff(times)>0) and torch.isfinite(raw).all()
 rows=list(csv.reader((study/f'baseline-0.2.5/raw/VO_{id}_M.csv').open()));names=rows[0][1:];values=np.array(rows[1:],dtype=np.float64)
 assert np.array_equal(times,values[:,0])
 basis=t(np.stack([source[n] for n in names]));weights=t(values[:,1:]);full=neutral+(weights@basis.reshape(52,-1)).reshape(-1,24002,3)
 idx=[names.index(n) for n in channels];selected=neutral+(weights[:,idx]@basis[idx].reshape(37,-1)).reshape(-1,24002,3)
 groups={'mouth':[n for n in names if n.startswith(('mouth','jaw'))],'brow':[n for n in names if n.startswith('brow')]}
 regions={}
 for name,poses in groups.items():
  energy=torch.linalg.vector_norm(t(np.stack([source[n] for n in poses])),dim=-1).max(0).values
  mask=energy>.05
  movement=torch.linalg.vector_norm(raw[:,mask]-neutral[mask],dim=-1)
  err=torch.linalg.vector_norm(full[:,mask]-raw[:,mask],dim=-1)
  restricted=torch.linalg.vector_norm(selected[:,mask]-raw[:,mask],dim=-1)
  regions[name]={'Vertices':int(mask.sum()),'SourceMotionRMS':float(movement.square().mean().sqrt()),'Full52SolveRMSError':float(err.square().mean().sqrt()),'Played37SolveRMSError':float(restricted.square().mean().sqrt()),'Full52P95Error':float(torch.quantile(err.flatten(),.95))}
 report.append({'Actor':actor,'StringRef':id,'Frames':len(times),'RegionsInSourceUnits':regions})
 np.savez_compressed(study/f'source-geometry/VO_{id}_M-comparison.npz',times=times,raw=raw.cpu().numpy(),solved=full.cpu().numpy())
result={'GPU':torch.cuda.get_device_name(),'ActualGeometryExecutorOutput':True,'EmotionInput':'zero baseline, matching preserved raw weights','Records':report,'Interpretation':'Source-space approximation errors, not a realism score or a direct LE correspondence measurement. No rig modifications justified by a metric alone.'}
(study/'source-geometry-audit.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))

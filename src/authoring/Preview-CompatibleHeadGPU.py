"""Prepare sparse serialized rig samples on CUDA for an OptiX preview."""
import json,struct
from pathlib import Path
import numpy as np,torch
root=Path(__file__).resolve().parents[2];study=root/'research/intro-a2f/correction-0.2.8'
mesh=json.loads((study/'HumanMale2266.json').read_text(encoding='utf-8-sig'))
rig=json.loads((study/'rig/HumanMale2266-rig.json').read_text())
t=lambda a:torch.tensor(a,device='cuda',dtype=torch.float32)
pos=t([v['Position'] for v in mesh['LODs'][0]['Vertices']]);basis=torch.zeros((48,len(pos),3),device='cuda')
for c,shape in enumerate(rig['Targets']):
    for row in shape['LODs'][0]['Vertices']:basis[c,int(row[0])]=t(row[1:])
frames=[];meta=[]
for cue in ['VO_102169_M','VO_148775_M']:
    b=(root/f'output/intro-a2f/clips/{cue}.mefc').read_bytes();_,_,n,count,rate,samples,_=struct.unpack_from('<6IQ',b)
    data=np.frombuffer(b[32:],dtype=np.dtype([('t','<u4'),('w','<f4',(n,))]))
    for tm in np.linspace(0,samples/rate,12):
        w=t([np.interp(tm*rate,data['t'],data['w'][:,c]) for c in range(48)])
        frames.append((pos+(w@basis.reshape(48,-1)).reshape(-1,3)).cpu().numpy());meta.append({'Cue':cue,'Time':float(tm)})
np.savez_compressed(study/'preview.npz',neutral=pos.cpu().numpy(),candidate=np.stack(frames),indices=np.array(mesh['Indices']).reshape(-1,3))
(study/'preview-inputs.json').write_text(json.dumps({'GPU':torch.cuda.get_device_name(),'Frames':meta,'RuntimeValidated':False},indent=2))
print('Prepared 24 original-mesh frames on',torch.cuda.get_device_name())

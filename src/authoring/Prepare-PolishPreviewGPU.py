"""Isolated GPU emotion/rig comparisons, plus final release motion inputs."""
import argparse,csv,json,struct
from pathlib import Path
import numpy as np,torch
root=Path(__file__).resolve().parents[2];study=root/'research/intro-a2f/correction-0.2.7';prior=study/'baseline-0.2.6';out=root/'output/intro-a2f'
p=argparse.ArgumentParser();p.add_argument('--final',action='store_true');a=p.parse_args()
names=json.loads((prior/'channels.json').read_text());records=[]
def t(v):return torch.tensor(v,device='cuda',dtype=torch.float32)
def smoothing(tm,w,sigma):
    d=tm[:,None]-tm[None,:];k=torch.exp(-.5*(d/sigma).square());k[d.abs()>sigma*3]=0;k/=k.sum(1,keepdim=True);return k@w
for actor,id,head in [('Kaidan',102169,'Average'),('Joker',102170,'Average'),('Shepard',65217,'Shepard')]:
    mesh=json.loads((prior/f'meshes/{head}.json').read_text(encoding='utf-8-sig'));pos=t([v['Position'] for v in mesh['LODs'][0]['Vertices']])
    old=(prior/f'clips/VO_{id}_M.mefc').read_bytes();ch,count=struct.unpack_from('<II',old,8)
    frames=np.frombuffer(old[32:],dtype=np.dtype([('t','<u4'),('w','<f4',(ch,))]));time=frames['t']/16000;duration=time[-1]
    oldbasis=t(np.load(prior/f'meshes/{head}-lod0-deltas.npy'))
    variants={'old':(frames['w'].copy(),oldbasis)}
    if a.final:
        data=(out/f'clips/VO_{id}_M.mefc').read_bytes();c,n=struct.unpack_from('<II',data,8)
        f=np.frombuffer(data[32:],dtype=np.dtype([('t','<u4'),('w','<f4',(c,))]));assert np.array_equal(f['t'],frames['t'])
        basis=np.zeros_like(np.load(out/f'meshes/{head}-lod0-deltas.npy'))
        rig=json.loads((out/f'meshes/{head}-rig.json').read_text())
        for c,shape in enumerate(rig['Targets']):
            for idx,x,y,z in shape['LODs'][0]['Vertices']:basis[c,idx]=[x,y,z]
        variants['candidate']=(f['w'].copy(),t(basis))
        times=np.arange(0,duration,1/12)
    else:
        for variant in ['moderate','stronger']:
            rows=list(csv.reader((study/f'{variant}/raw/VO_{id}_M.csv').open()));cols=rows[0][1:];raw=np.array(rows[1:],dtype=np.float64)
            w=t(np.stack([np.interp(frames['t'],raw[:,0],raw[:,1+cols.index(n)]) for n in names[:37]],1));tm=t(time.astype('f4'))
            speech=[i for i,n in enumerate(names[:37]) if n.startswith(('mouth','jaw'))];expression=[i for i in range(37) if i not in speech]
            w[:,speech]=smoothing(tm,w[:,speech],.016);w[:,expression]=smoothing(tm,w[:,expression],.04)
            w=torch.cat([w,(w[:,names.index('jawOpen')]*w[:,names.index('mouthClose')])[:,None]],1);w[0]=0;w[-1]=0
            variants[variant]=(w.cpu().numpy(),oldbasis)
        details=np.load(study/f'preview-detail/VO_{id}_M.npz')
        variants['enriched']=(details['weights'],t(np.load(study/f'preview-detail/{head}-lod0-deltas.npy')))
        w=variants['moderate'][0]
        peaks=[time[w[:,names.index(n)].argmax()] for n in ['jawOpen','mouthPucker','browInnerUp']]
        times=np.array([.08,.20,*peaks,duration*.65])
    poses={}
    for label,(weights,basis) in variants.items():
        w=t(np.stack([np.interp(times,time,weights[:,i]) for i in range(weights.shape[1])],1))
        tm=t(times.astype('f4'))
        def envelope(span):
            x=(tm/span).clamp(0,1);y=((duration-tm)/span).clamp(0,1);return torch.minimum(x*x*(3-2*x),y*y*(3-2*y))
        if a.final and label=='candidate':
            channels=json.loads((out/'channels.json').read_text())
            for i,n in enumerate(channels):w[:,i]*=envelope(.24 if n.startswith(('brow','detailBrow')) else .16)
        else:w*=envelope(.16)[:,None]
        poses[label]=(pos[None]+(w@basis.flatten(1)).reshape(len(times),-1,3)).cpu().numpy()
    np.savez_compressed(study/f'{actor}-{"final-motion" if a.final else "variants"}.npz',times=times,**poses)
    records.append({'Actor':actor,'HeadMesh':head,'StringRef':id,'Times':times.tolist(),'Labels':list(variants)})
(study/('final-motion-inputs.json' if a.final else 'variant-inputs.json')).write_text(json.dumps({'Device':'CUDA','GPU':torch.cuda.get_device_name(),'Frames':records},indent=2))
print('Prepared',sum(len(r['Times'])*len(r['Labels']) for r in records),'local GPU preview frames',flush=True)

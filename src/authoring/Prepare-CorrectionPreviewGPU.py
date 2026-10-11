"""CUDA A/B deformation on identical audio times; do not regenerate game audio."""
import csv,json,numpy as np,torch,sys,struct,argparse
from pathlib import Path
root=Path(__file__).resolve().parents[2];o=root/'output/intro-a2f'
parser=argparse.ArgumentParser();parser.add_argument('--revision',default='0.2.3');parser.add_argument('--previous',default='0.2.2')
args,_=parser.parse_known_args()
p=root/f'research/intro-a2f/correction-{args.revision}';old=p/f'baseline-{args.previous}'
assert torch.cuda.is_available()
names=json.loads((o/'channels.json').read_text());base=names[:-1];report=[]
motion='--motion' in sys.argv
for actor,id,head in [('Kaidan',102169,'Average'),('Joker',102170,'Average'),('Shepard',65217,'Shepard')]:
    mesh=json.loads((o/f'meshes/{head}.json').read_text(encoding='utf-8-sig'))
    pos=torch.tensor([v['Position'] for v in mesh['LODs'][0]['Vertices']],device='cuda')
    bases=[torch.tensor(np.load(d/f'meshes/{head}-lod0-deltas.npy'),device='cuda') for d in [old,o]]
    rows=[]
    candidate=p/f'candidate-clips/VO_{id}_M.mefc' if args.revision=='0.2.6' else o/f'clips/VO_{id}_M.mefc'
    for f in [o/f'release/MEFaces-IntroA2F-{args.previous}/Game/ME1/Binaries/Win64/ASI/MEFIntroClips/VO_{id}_M.mefc',candidate]:
        data=f.read_bytes();ch,ct=struct.unpack_from('<II',data,8)
        v=np.frombuffer(data[32:],dtype=np.dtype([('t','<u4'),('w','<f4',(ch,))]))
        rows.append((v['t']/16000,v['w']))
    duration=min(r[0][-1] for r in rows)
    times=np.arange(0,duration,1/12) if motion else np.linspace(.15,.85,5)*duration
    arrays={}
    for label,basis,(t,w) in zip(['old','candidate'],bases,rows):
        sampled=np.stack([np.interp(times,t,w[:,i]) for i in range(w.shape[1])],1).astype('f4')
        gpu=torch.tensor(sampled,device='cuda')
        if label=='candidate' and args.revision=='0.2.6':
            tm=torch.tensor(times,device='cuda',dtype=torch.float32)
            entry=(tm/.16).clamp(0,1);exit=((duration-tm)/.16).clamp(0,1)
            envelope=torch.minimum(entry*entry*(3-2*entry),exit*exit*(3-2*exit))
            gpu*=envelope[:,None]
        arrays[label]=(pos[None]+(gpu@basis.reshape(len(gpu[0]),-1)).reshape(len(times),-1,3)).cpu().numpy()
    np.savez_compressed(p/f'{actor}-{"motion" if motion else "preview"}.npz',**arrays,times=times)
    report.append({'Actor':actor,'HeadMesh':head,'StringRef':id,'Times':times.tolist()})
(p/('motion-inputs.json' if motion else 'preview-inputs.json')).write_text(json.dumps({'GPU':torch.cuda.get_device_name(),'Device':'cuda','Input':'actual precomputed release clips, including smoothing/fades/corrective','Versions':[args.previous,args.revision],'Frames':report},indent=2))
print('CUDA A/B preview inputs ready')

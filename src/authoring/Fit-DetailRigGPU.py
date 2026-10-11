"""Shared residual facial controls fitted to actual NVIDIA geometry, on CUDA.

The source and LE topologies are never nearest-neighbour matched. The same
21 semantic control cage transfers source displacement onto each original
head's own skin-influence fields. Eyes, lids, skeleton, neutral shape and jaw
arc remain unchanged. PCA is uncentred: no hidden neutral-pose displacement.
"""
import argparse,csv,hashlib,json,struct
from pathlib import Path
import numpy as np
import torch
root=Path(__file__).resolve().parents[2];out=root/'output/intro-a2f'
study=root/'research/intro-a2f/correction-0.2.7';prior=study/'baseline-0.2.6'
parser=argparse.ArgumentParser();parser.add_argument('--full',action='store_true');args=parser.parse_args()
dest=study/('full-detail' if args.full else 'preview-detail');dest.mkdir(exist_ok=True)
assert torch.cuda.is_available()
def t(a):return torch.tensor(a,device='cuda',dtype=torch.float32)
names=json.loads((prior/'channels.json').read_text());assert len(names)==38
semantic=json.loads((root/'research/intro-a2f/correction-0.2.5/semantic-rig.json').read_text())
source=np.load(root/'models/A2F-v3.0/bs_skin_Mark.npz');sp=t(source['neutral'])
neutral=[];response=[];ids=[];control_weights=[]
for anchor,indices in zip(semantic['SourceControlPositions'],semantic['SourceControlIndices']):
    # Reuse the same twelve source samples, weighting about their saved
    # centroid; this is not an exact reconstruction of the original query.
    # There is no cross-topology matching.
    idx=torch.tensor(indices,device='cuda');q=t(anchor)
    weights=1/((sp[idx]-q).square().sum(-1)+.02);weights/=weights.sum()
    ids.append(idx);control_weights.append(weights)
    neutral.append((sp[idx]*weights[:,None]).sum(0))
    response.append((t(np.stack([source[n] for n in names[:37]]))[:,idx]*weights[None,:,None]).sum(1))
neutral=torch.stack(neutral);response=torch.stack(response,1)

def read_csv(path):
    rows=list(csv.reader(path.open()));return rows[0][1:],np.array(rows[1:],dtype=np.float64)
def geometry(path):
    b=path.read_bytes();offset=0;frames=[];times=[]
    while offset<len(b):
        tm,n=struct.unpack_from('<qI',b,offset);offset+=12;assert n==72006
        frame=t(np.frombuffer(b[offset:offset+4*n],dtype='<f4').copy().reshape(-1,3));offset+=4*n
        frames.append(torch.stack([(frame[idx]*w[:,None]).sum(0) for idx,w in zip(ids,control_weights)]));times.append(tm)
    return np.array(times),torch.stack(frames)-neutral
records=[];all_residual=[]
for f in sorted((study/'moderate/geometry').glob('*.bin')):
    id=int(f.stem.split('_')[1]);times,motion=geometry(f)
    cols,raw=read_csv(study/f'moderate/raw/VO_{id}_M.csv');assert np.array_equal(times,raw[:,0])
    weights=t(raw[:,[1+cols.index(n) for n in names[:37]]])
    solved=torch.einsum('fc,ckd->fkd',weights,response)
    residual=motion-solved
    assert torch.isfinite(residual).all()
    all_residual.append(residual)
    records.append({'ID':id,'Times':times,'Raw':raw,'RawNames':cols,'Residual':residual})
assert len(records)==(30 if args.full else 3),len(records)
data=torch.cat(all_residual);keys=semantic['Heads'][0]['ControlKeys']
groups=[('Brow',[i for i,k in enumerate(keys) if k.startswith(('brow_','outbrow_'))],2),
        ('Mouth',[i for i,k in enumerate(keys) if not k.startswith(('brow_','outbrow_'))],3)]
modes=[];coefficients=[];newnames=[];metrics=[]
for group,indices,rank in groups:
    matrix=data[:,indices].flatten(1)
    # Small source-control SVD runs on CUDA; output modes are shared across lines.
    _,singular,vh=torch.linalg.svd(matrix,full_matrices=False)
    basis=vh[:rank];scores=matrix@basis.T
    bounds=scores.abs().max(0).values*1.01;assert (bounds>1e-6).all()
    reconstruction=scores@basis
    before=float(matrix.square().mean().sqrt());after=float((matrix-reconstruction).square().mean().sqrt())
    assert after<before*.9,(group,before,after)
    metrics.append({'Region':group,'Modes':rank,'ControlResidualRMSBefore':before,'After':after,
                    'EnergyRetained':float(singular[:rank].square().sum()/singular.square().sum())})
    for j in range(rank):
        mode=torch.zeros_like(neutral);mode[indices]=(basis[j]*bounds[j]).reshape(-1,3)
        for sign,label in [(1.,'Positive'),(-1.,'Negative')]:
            modes.append(mode*sign)
            coefficients.append((scores[:,j]*sign/bounds[j]).clamp(0,1))
            newnames.append(f'detail{group}{j+1}{label}')
modes=torch.stack(modes);scores=torch.stack(coefficients,1)
assert len(newnames)==10 and scores.min()>=0 and scores.max()<=1
torch.save({'Modes':modes.cpu(),'Names':newnames,'Keys':keys,'Neutral':neutral.cpu()},dest/'source-control-modes.pt')
offset=0
for rec in records:
    count=len(rec['Times']);rec['Detail']=scores[offset:offset+count];offset+=count

heads=[]
for actor in ['Shepard','Joker','Kaidan','Average']:
    mesh=json.loads((prior/f'meshes/{actor}.json').read_text(encoding='utf-8-sig'))
    vv=mesh['LODs'][0]['Vertices'];pos=t([v['Position'] for v in vv])
    protected=torch.tensor([any(w>0 and any(s in n.lower() for s in ['eye','lowlid','blink','neck','chest']) for n,w in v['Weights'].items()) for v in vv],device='cuda')
    fields=t([[{k.lower():w for k,w in v['Weights'].items()}.get(key,0) for key in keys] for v in vv]);fields[protected]=0
    measure=fields.T.pow(4);measure/=measure.sum(1,keepdim=True)
    gram=measure@fields
    scale=t(next(h for h in semantic['Heads'] if h['Actor']==actor)['SourceScaleDepthWidthHeight'])
    desired=torch.stack([modes[...,2],-modes[...,0],modes[...,1]],-1)*scale*.75
    coeff=torch.linalg.solve(gram.T@gram+torch.eye(len(keys),device='cuda')*.08,
                             torch.einsum('jk,ckd->cjd',gram.T,desired))
    extra=torch.einsum('vk,ckd->cvd',fields,coeff)
    # Bound every new shape uniformly, keeping its anatomical direction.
    factor=torch.clamp(.32/torch.linalg.vector_norm(extra,dim=-1).max(1).values.clamp_min(1e-8),max=1)
    extra*=factor[:,None,None]
    assert extra[:,protected].abs().max()==0 and torch.isfinite(extra).all()
    arrays=[]
    for level,lod in enumerate(mesh['LODs']):
        base=t(np.load(prior/f'meshes/{actor}-lod{level}-deltas.npy'))
        if level==0:delta=extra
        else:
            pp=t([v['Position'] for v in lod['Vertices']]);distance,near=torch.topk(torch.cdist(pp,pos),3,largest=False)
            blend=1/(distance.square()+1e-7);blend/=blend.sum(1,keepdim=True)
            delta=(extra[:,near]*blend[None,:,:,None]).sum(2)*torch.exp(-distance[:,0].square()/2)[None,:,None]
            mask=torch.tensor([any(w>0 and any(s in n.lower() for s in ['eye','lowlid','blink','neck','chest']) for n,w in v['Weights'].items()) for v in lod['Vertices']],device='cuda')|protected[near[:,0]]
            delta[:,mask]=0
        combined=torch.cat([base,delta],0);assert torch.equal(combined[:38],base)
        arrays.append(combined.cpu().numpy().astype('<f4'))
        np.save(dest/f'{actor}-lod{level}-deltas.npy',arrays[-1])
    rig=json.loads((prior/f'meshes/{actor}-rig.json').read_text())
    rig['Channels']=names+newnames
    for c,name in enumerate(newnames,38):
        rig['Targets'].append({'Name':'MEF_A2F_'+name,'Channel':name,'LODs':[
          {'NumVertices':len(a[c]),'Vertices':[[int(i),*map(float,a[c,i])] for i in np.flatnonzero(np.linalg.norm(a[c],axis=1)>.002)]} for a in arrays]})
    (dest/f'{actor}-rig.json').write_text(json.dumps(rig,separators=(',',':')))
    heads.append({'Actor':actor,'NewShapeMaxDelta':float(torch.linalg.vector_norm(extra,dim=-1).max()),'Factors':factor.cpu().tolist(),'ProtectedMaxDelta':0,'Prior38ShapesUnchanged':True})

def smooth(time,weights,sigma):
    distance=time[:,None]-time[None,:];kernel=torch.exp(-.5*(distance/sigma).square());kernel[distance.abs()>sigma*3]=0;kernel/=kernel.sum(1,keepdim=True)
    return kernel@weights
clip_records=[]
for rec in records:
    id=rec['ID'];old=(prior/f'clips/VO_{id}_M.mefc').read_bytes()
    ch,count=struct.unpack_from('<II',old,8)
    frames=np.frombuffer(old[32:],dtype=np.dtype([('t','<u4'),('w','<f4',(ch,))]));times=frames['t'].copy();tm=t(times.astype('f4'))/16000
    values=rec['Raw'][:,[1+rec['RawNames'].index(n) for n in names[:37]]]
    weights=t(np.stack([np.interp(times,rec['Times'],values[:,i]) for i in range(37)],1))
    mouth=[i for i,n in enumerate(names[:37]) if n.startswith(('jaw','mouth'))]
    expression=[i for i in range(37) if i not in mouth]
    weights[:,mouth]=smooth(tm,weights[:,mouth],.016);weights[:,expression]=smooth(tm,weights[:,expression],.04)
    detail=rec['Detail'].cpu().numpy();detail=t(np.stack([np.interp(times,rec['Times'],detail[:,i]) for i in range(10)],1))
    detail[:,:4]=smooth(tm,detail[:,:4],.04);detail[:,4:]=smooth(tm,detail[:,4:],.016)
    seal=weights[:,names.index('jawOpen')]*weights[:,names.index('mouthClose')]
    final=torch.cat([weights,seal[:,None],detail],1).clamp(0,1);final[0]=0;final[-1]=0
    np.savez_compressed(dest/f'VO_{id}_M.npz',times=times,weights=final.cpu().numpy())
    clip_records.append({'StringRef':id,'Frames':count,'MaxDetailWeight':float(final[:,38:].max())})
(dest/'channels.json').write_text(json.dumps(names+newnames,indent=2))
(dest/'detail-rig-audit.json').write_text(json.dumps({'GPU':torch.cuda.get_device_name(),'Channels':48,'TrainingLines':len(records),
  'UncentredPCA':True,'ResidualGain':.75,'MaximumNewShapeDelta':.32,'Regions':metrics,'Heads':heads,'Clips':clip_records,
  'NeutralMeshSkeletonSkinMaterialsUnchanged':True,'DirectTopologyTransfer':False,'RuntimeValidated':False,
  'Limitations':['21 anatomical measurements do not capture the full surface','source-space reconstruction error is not a realism metric','linear morphs do not provide collision/contact solving']},indent=2))
print(json.dumps({'Channels':48,'Lines':len(records),'Regions':metrics,'Heads':heads},indent=2),flush=True)

"""Transfer an accepted facial basis to another head using skin semantics.

Authoring only: no character names, game writes, AI inference, or neutral mesh
changes. Correspondences stay within compatible skin influences; protected
vertices stay exactly fixed. A new structural asset still needs visual testing.
"""
import argparse, json, struct, hashlib
from pathlib import Path
import numpy as np
import torch
root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--mesh',type=Path,required=True)
parser.add_argument('--reference',default='Average')
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();dest=args.output;dest.mkdir(exist_ok=True,parents=True)
assert torch.cuda.is_available(),'Local CUDA required for rig transfer'
source=json.loads((root/f'output/intro-a2f/meshes/{args.reference}.json').read_text(encoding='utf-8-sig'))
target=json.loads(args.mesh.read_text(encoding='utf-8-sig'))
channels=json.loads((root/'output/intro-a2f/channels.json').read_text());assert len(channels)==48
t=lambda a:torch.tensor(a,device='cuda',dtype=torch.float32)
sp=t([v['Position'] for v in source['LODs'][0]['Vertices']])
keys=sorted({k.lower() for v in source['LODs'][0]['Vertices'] for k in v['Weights']})
def fields(vertices):
    return t([[{k.lower():w for k,w in v['Weights'].items()}.get(key,0) for key in keys] for v in vertices])
sf=fields(source['LODs'][0]['Vertices']);sf=sf/sf.norm(dim=1,keepdim=True).clamp_min(1e-8)
basis=t(np.load(root/f'output/intro-a2f/meshes/{args.reference}-lod0-deltas.npy'))
assert basis.shape==(48,len(sp),3)
reports=[];arrays=[]
for level,lod in enumerate(target['LODs']):
    vv=lod['Vertices'];pos=t([v['Position'] for v in vv]);f=fields(vv)
    known=f.sum(1)>.98
    assert bool(known.all()),'Target has unmapped skin influences; do not silently discard them'
    f=f/f.norm(dim=1,keepdim=True).clamp_min(1e-8)
    protect=torch.tensor([any(w>0 and any(s in n.lower() for s in ['eye','lowlid','blink','neck','chest']) for n,w in v['Weights'].items()) for v in vv],device='cuda')
    distance=torch.cdist(pos,sp);similarity=(f@sf.T).clamp(0,1)
    cost=distance.square()+8*(1-similarity).square()
    cost[similarity<.65]=float('inf')
    score,near=torch.topk(cost,4,largest=False)
    assert bool(torch.isfinite(score[:,0]).all())
    weights=torch.where(torch.isfinite(score),1/(score+.02),0);weights/=weights.sum(1,keepdim=True)
    result=(basis[:,near]*weights[None,:,:,None]).sum(2)
    result[:,protect]=0
    # Lip-shape vertical separation retains the accepted rig's calibration,
    # scaled by the new head's jaw response. This is a proxy, not contact solve.
    def rim(vertices,positions):
        inds=[]
        for group in ['upperlip','lowerlip']:
            w=t([sum(w for n,w in v['Weights'].items() if n.lower() in [group+'_left',group+'_right']) for v in vertices])
            c=torch.where((w>.9)&(positions[:,1].abs()<.8))[0]
            assert len(c)>=3,(group,len(c))
            inds.append(c[torch.topk(positions[c,0],min(4,len(c))).indices])
        return inds
    su,sl=rim(source['LODs'][0]['Vertices'],sp);tu,tl=rim(vv,pos)
    response=basis[:,su,2].mean(1)-basis[:,sl,2].mean(1)
    current=result[:,tu,2].mean(1)-result[:,tl,2].mean(1)
    scale=current[channels.index('jawOpen')]/response[channels.index('jawOpen')]
    assert .5<float(scale)<1.5,float(scale)
    up=t([sum(w for n,w in v['Weights'].items() if 'upperlip' in n.lower()) for v in vv])
    low=t([sum(w for n,w in v['Weights'].items() if 'lowerlip' in n.lower() or 'lowlip' in n.lower()) for v in vv])
    field=.5*(up-low);field[protect]=0;field/=field[tu].mean()-field[tl].mean()
    for name in ['mouthClose','mouthRollLower','mouthRollUpper','mouthShrugLower','mouthShrugUpper','mouthPressLeft','mouthPressRight']:
        c=channels.index(name);result[c,:,2]+=(response[c]*scale-current[c])*field
    assert torch.isfinite(result).all() and result[:,protect].abs().max()==0
    assert float(result.norm(dim=-1).max())<1.6
    # Validate the sparse payload that will actually be serialized to PCC.
    result[result.norm(dim=-1)<=.002]=0
    array=result.cpu().numpy().astype('<f4');arrays.append(array)
    np.save(dest/f"{target['Actor']}-lod{level}-deltas.npy",array)
    worst=0;clips=[]
    for path in sorted((root/'output/intro-a2f/clips').glob('*.mefc')):
        b=path.read_bytes();_,_,n,count,_,_,_=struct.unpack_from('<6IQ',b)
        assert n==48
        data=np.frombuffer(b[32:],dtype=np.dtype([('t','<u4'),('w','<f4',(n,))]))
        weights=t(data['w'].copy());disp=(weights@result.reshape(n,-1)).reshape(count,len(pos),3)
        maximum=float(disp.norm(dim=-1).max());assert maximum<3 and disp[:,protect].abs().max()==0
        worst=max(worst,maximum);clips.append({'Clip':path.name,'Maximum':maximum})
    reports.append({'LOD':level,'Vertices':len(pos),'MaximumShapeDisplacement':float(result.norm(dim=-1).max()),
        'MaximumCombinedDisplacement':worst,'ProtectedVertices':int(protect.sum()),'ProtectedDisplacement':0,
        'NearestSemanticDistanceMaximum':float(distance.gather(1,near[:,:1]).max()),'LipApertureScale':float(scale),'Clips':clips})
targets=[{'Name':'MEF_A2F_'+name,'Channel':name,'LODs':[
    {'NumVertices':len(a[c]),'Vertices':[[int(i),*map(float,a[c,i])] for i in np.flatnonzero(np.linalg.norm(a[c],axis=1)>.002)]} for a in arrays]} for c,name in enumerate(channels)]
rig={'Actor':target['Actor'],'Mesh':target['Mesh'],'Channels':channels,'Targets':targets,'SourceExportSHA256':target['SourceExportSHA256']}
(dest/f"{target['Actor']}-rig.json").write_text(json.dumps(rig,separators=(',',':')))
audit={'GPU':torch.cuda.get_device_name(),'Reference':args.reference,'TargetMesh':target['Mesh'],
       'Method':'GPU spatial correspondence constrained by semantic skin influences; accepted 48-channel rig and relative lip-aperture calibration',
       'NeutralMeshSkeletonSkinMaterialsUnchanged':True,'ClipsUnchanged':True,'RuntimeValidated':False,'LODs':reports,
       'Limitations':['Not a fresh fit to neural geometry','Linear interpolation may soften detail; live recording required','No full inner-lip contact/collision solver','Does not establish arbitrary human/asari compatibility']}
(dest/'transfer-audit.json').write_text(json.dumps(audit,indent=2))
print(json.dumps({k:v for k,v in audit.items() if k!='LODs'},indent=2));print([(r['Vertices'],r['MaximumCombinedDisplacement']) for r in reports],flush=True)

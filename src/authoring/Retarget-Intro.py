"""GPU surface deformation transfer; original LE1 render IDs/topology retained.

NVIDIA NPZ non-neutral arrays are DELTAS, not absolute poses. A regularized
landmark warp aligns the source surface to each head; its deformation Jacobian
is evaluated by finite differences and transferred with local surface weights.
No game bones, eye/eyelid vertices or protected transforms are changed.
"""
import json, numpy as np, torch
from pathlib import Path
root=Path(__file__).resolve().parents[2]; out=root/'output/intro-a2f'
assert torch.cuda.is_available(), 'CUDA GPU required for numerical retargeting'
device='cuda'; source=np.load(root/'models/A2F-v2.3-Mark/bs_skin.npz')
pose_names=[x.decode('utf-8') if isinstance(x,bytes) else str(x) for x in source['poseNames']]
names=[x for x in pose_names if x!='neutral' and not x.startswith('eye') and x!='tongueOut']
assert len(names)==37, len(names)
neutral=torch.tensor(source['neutral'],device=device)
indices=torch.tensor(source['frontalMask'].astype('int64'),device=device)
neutral=neutral[indices]
deltas=torch.tensor(np.stack([source[k] for k in names]),device=device)[:,indices]
# Pair source deformation landmarks with original game skin influence centres.
pairs=[('mouthSmileLeft',['lipcorner_left']),('mouthSmileRight',['lipcorner_right']),
       ('mouthUpperUpLeft',['upperlip_left','innerupperlip_left']),('mouthUpperUpRight',['upperlip_right','innerupperlip_right']),
       ('mouthLowerDownLeft',['lowerlip_left','innerlowlip_left']),('mouthLowerDownRight',['lowerlip_right','innerlowlip_right']),
       ('eyeBlinkLeft',['eyeblink_left','lowlid_left']),('eyeBlinkRight',['eyeblink_right','lowlid_right']),
       ('browOuterUpLeft',['outbrow_left','brow_left']),('browOuterUpRight',['outbrow_right','brow_right']),
       ('noseSneerLeft',['sneer']),('noseSneerRight',['sneer'])]
src=[]
for pose,_ in pairs:
    w=torch.tensor(np.linalg.norm(source[pose],axis=1)**4,device=device)
    src.append((torch.tensor(source['neutral'],device=device)*w[:,None]).sum(0)/w.sum())
src=torch.stack(src)
# Nose-sneer landmarks share the central nose target; retain their source
# lateral offset rather than collapsing two source anchors onto one point.
reports=[]
for actor in ['Shepard','Joker','Kaidan','Average']:
    mesh=json.loads((out/f'meshes/{actor}.json').read_text(encoding='utf-8-sig'))
    verts=mesh['LODs'][0]['Vertices']; pos=torch.tensor([v['Position'] for v in verts],device=device)
    def centroid(keys):
        w=torch.tensor([sum(w for k,w in v['Weights'].items() if k.lower() in keys) for v in verts],device=device)
        assert w.sum()>0,keys
        return (pos*w[:,None]).sum(0)/w.sum()
    dst=torch.stack([centroid(keys) for _,keys in pairs])
    side=torch.linalg.vector_norm(dst[0]-dst[1])/torch.linalg.vector_norm(src[0]-src[1])
    height=((dst[6,2]+dst[7,2])-(dst[0,2]+dst[1,2]))/((src[6,1]+src[7,1])-(src[0,1]+src[1,1]))
    scale=torch.stack([side,side,height])
    def orient(x):return torch.stack([x[...,2],-x[...,0],x[...,1]],dim=-1)*scale
    offset=(dst[:2].mean(0)-orient(src[:2]).mean(0))
    anchors=orient(src)+offset
    dst[-2:,1]=anchors[-2:,1]
    # Gaussian RBF warp, regularized to avoid unstable extrapolation.
    sigma=3.0
    kernel=torch.exp(-torch.cdist(anchors,anchors).square()/(2*sigma*sigma))
    coeff=torch.linalg.solve(kernel+torch.eye(len(src),device=device)*.15,dst-anchors)
    def warp(x):
        x=orient(x)+offset
        return x+torch.exp(-torch.cdist(x.reshape(-1,3),anchors).square()/(2*sigma*sigma)).matmul(coeff).reshape(x.shape)
    rest=warp(neutral)
    transformed=warp(neutral[None]+deltas)-rest[None]
    # Keep the first A2F pass conservative while vanilla FaceFX is still active.
    # This is an additive facial detail layer; no claim of full replacement.
    gains=torch.tensor([.35 if k.startswith('jaw') or k=='mouthClose' else .65 for k in names],device=device)
    transformed*=gains[:,None,None]
    lods=[];audit=[]
    for level,lod in enumerate(mesh['LODs']):
        vv=lod['Vertices'];vpos=torch.tensor([v['Position'] for v in vv],device=device)
        allowed=[];protected=[]
        for v in vv:
            weights={k.lower():w for k,w in v['Weights'].items()}
            allowed.append(sum(w for k,w in weights.items() if any(t in k for t in ['lip','cheek','brow','sneer','jawbone'])))
            protected.append(sum(w for k,w in weights.items() if any(t in k for t in ['eye','lowlid','blink','tongue','neck','chest'])))
        mask=torch.tensor(allowed,device=device).clamp(0,1)
        protected=torch.tensor(protected,device=device)>0
        mask[protected]=0
        mask[(vpos[:,0]<4)|(vpos[:,2]<157)|(vpos[:,2]>177)]=0
        distances,near=torch.topk(torch.cdist(vpos,rest),8,largest=False)
        weights=1/(distances.square()+.03);weights/=weights.sum(1,keepdim=True)
        transferred=(transformed[:,near]*weights[None,:,:,None]).sum(2)*mask[None,:,None]
        # Smooth attenuation for source surface regions without correspondence.
        transferred*=torch.exp(-distances[:,0].square()/8)[None,:,None]
        length=torch.linalg.vector_norm(transferred,dim=-1)
        transferred*=torch.clamp(1.6/(length+1e-8),max=1)[...,None]
        assert torch.isfinite(transferred).all()
        assert transferred[:,protected].abs().max()==0
        shape=transferred.cpu().numpy().astype('<f4')
        np.save(out/f'meshes/{actor}-lod{level}-deltas.npy',shape)
        lods.append({'LOD':level,'NumVertices':len(vv),'DeltaFile':f'{actor}-lod{level}-deltas.npy'})
        audit.append({'LOD':level,'Vertices':len(vv),'ProtectedVertices':int(protected.sum()),'ProtectedMaxDelta':0,'MaxDelta':float(length.max()),'ChangedVertices':int((torch.linalg.vector_norm(transferred,dim=-1).max(0).values>.002).sum())})
    # Sparse JSON feeds LegendaryExplorerCore; threshold below a pixel at this scale.
    targets=[]
    arrays=[np.load(out/'meshes'/x['DeltaFile']) for x in lods]
    for c,name in enumerate(names):
        targets.append({'Name':'MEF_A2F_'+name,'Channel':name,'LODs':[{'NumVertices':a.shape[1],'Vertices':[[int(i),*map(float,a[c,i])] for i in np.flatnonzero(np.linalg.norm(a[c],axis=1)>.002)]} for a in arrays]})
    rig={'Actor':actor,'Mesh':mesh['Mesh'],'Channels':names,'Targets':targets,'SourceExportSHA256':mesh['SourceExportSHA256']}
    (out/f'meshes/{actor}-rig.json').write_text(json.dumps(rig,separators=(',',':')))
    reports.append({'Actor':actor,'AnchorRMS':float(torch.linalg.vector_norm(warp(src)-dst,dim=1).square().mean().sqrt()),'Scale':scale.cpu().tolist(),'LODs':audit})
report={'GPU':torch.cuda.get_device_name(0),'NumericalDevice':'cuda','Method':'regularized landmark surface warp + eight-neighbour deformation transfer','Channels':names,'ChannelCount':len(names),'Source':'Mark v2.3 delta basis','NoEyeOrEyelidVerticesChanged':True,'VanillaFaceFX':'retained; conservative additive first pass','Heads':reports,'RuntimeValidated':False}
(root/'research/intro-a2f/retarget-report.json').write_text(json.dumps(report,indent=2))
(out/'channels.json').write_text(json.dumps(names))
print(json.dumps(report,indent=2))

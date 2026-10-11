"""Experimental shared anatomical control cage, baked to ordinary LE1 morphs.

No neutral mesh, skeleton, skin weight, material or runtime bone writes. Named
skin influences define a continuous control cage on each original head. Source
landmarks are sampled within Mark only, never nearest-transferred to LE skin.
"""
import json, hashlib
from pathlib import Path
import numpy as np
import torch

root=Path(__file__).resolve().parents[2]; out=root/'output/intro-a2f'
study=root/'research/intro-a2f/correction-0.2.5'; prior=study/'baseline-0.2.4/meshes'
assert torch.cuda.is_available(); device='cuda'
def t(x): return torch.tensor(x,dtype=torch.float32,device=device)
names=json.loads((out/'channels.json').read_text()); assert len(names)==38
source_path=root/'output/sdk-validation/_data/generated/audio2face-sdk/samples/data/multi-diffusion/bs_skin_Mark.npz'
src=np.load(source_path); sp=t(src['neutral']); sb=t(np.stack([src[n] for n in names[:-1]]))
# Anatomical points on the neutral Mark surface: X lateral, Y up, Z forward.
# Same source annotations and algorithm for every target head. InnerUpperLip
# game influences cover philtrum/upper muzzle; they are not the inner lip rim.
definitions=[
 ('lipcorner', (2.55,152.42,5.55)),
 ('upperlip', (1.05,152.93,6.45)),
 ('lowerlip', (1.05,152.12,6.40)),
 ('outerupperlip', (2.35,152.75,5.55)),
 ('innerupperlip', (1.40,154.20,5.95)),
 ('innerlowlip', (1.20,150.90,5.60)),
 ('lowercheek', (3.50,151.30,3.95)),
 ('cheek', (4.60,155.10,3.60)),
 ('brow', (2.55,161.10,5.65)),
 ('outbrow', (4.35,160.90,4.65)),
]
keys=[]; anchors=[]; source_indices=[]; source_responses=[]
for name,point in definitions:
 for suffix,sign in [('left',1),('right',-1)]:
  q=t([point[0]*sign,point[1],point[2]])
  dist,idx=torch.topk(torch.linalg.vector_norm(sp-q,dim=1),12,largest=False)
  w=1/(dist.square()+.02);w/=w.sum()
  keys.append(name+'_'+suffix);anchors.append((sp[idx]*w[:,None]).sum(0))
  source_indices.append(idx.cpu().tolist());source_responses.append((sb[:,idx]*w[None,:,None]).sum(1))
keys.append('sneer');q=t([0,155.8,7.4])
dist,idx=torch.topk(torch.linalg.vector_norm(sp-q,dim=1),12,largest=False)
w=1/(dist.square()+.02);w/=w.sum();anchors.append((sp[idx]*w[:,None]).sum(0))
source_indices.append(idx.cpu().tolist());source_responses.append((sb[:,idx]*w[None,:,None]).sum(1))
anchors=torch.stack(anchors);sr=torch.stack(source_responses,1)
assert len(keys)==21 and all(anchors[i,0]>0 for i in range(0,20,2))
aperture=json.loads((root/'research/intro-a2f/correction-0.2.4/aperture-fit.json').read_text())
preserved=[i for i,n in enumerate(names) if n.startswith('jaw') or n.startswith('cheekSquint') or n.startswith('corrective')]
reports=[]
for actor in ['Shepard','Joker','Kaidan','Average']:
 mesh=json.loads((prior/f'{actor}.json').read_text(encoding='utf-8-sig'));vv=mesh['LODs'][0]['Vertices']
 pos=t([v['Position'] for v in vv]);old=t(np.load(prior/f'{actor}-lod0-deltas.npy'))
 protect=torch.tensor([any(w>0 and any(s in n.lower() for s in ['eye','lowlid','blink','neck','chest']) for n,w in v['Weights'].items()) for v in vv],device=device)
 weights=t([[{k.lower():v for k,v in vert['Weights'].items()}.get(key,0) for key in keys] for vert in vv])
 weights[protect]=0
 assert weights.sum(0).min()>.1,(actor,'missing control')
 # Measurement rows use dominant skin influence, avoiding broad centroid bias.
 measure=weights.T.pow(4);measure/=measure.sum(1,keepdim=True)
 target_anchors=measure@pos
 width=torch.linalg.vector_norm(target_anchors[0]-target_anchors[1])/torch.linalg.vector_norm(anchors[0]-anchors[1])
 ar=next(x for x in aperture['Heads'] if x['Actor']==actor)
 vertical=ar['SourceToGameJawSeparationScale'];depth=(float(width)*vertical)**.5
 scale=t([depth,float(width),vertical])
 desired=torch.stack([sr[...,2],-sr[...,0],sr[...,1]],-1)*scale
 measured=torch.einsum('kv,cvd->ckd',measure,old[:-1])
 # Regularized least squares changes semantic control positions, retaining a
 # modest original-shape prior. It does not replace the character's anatomy.
 gram=measure@weights;regularization=.08
 lhs=gram.T@gram+torch.eye(len(keys),device=device)*regularization
 rhs=torch.einsum('jk,ckd->cjd',gram.T,desired-measured)
 coeff=torch.linalg.solve(lhs,rhs)
 correction=torch.einsum('vk,ckd->cvd',weights,coeff)*.75
 correction=torch.cat([correction,torch.zeros_like(correction[:1])],0)
 correction[preserved]=0
 # Preserve the successful0.2.4 central vertical closing calibration exactly.
 gu=torch.tensor(ar['UpperLandmarks'],device=device);gl=torch.tensor(ar['LowerLandmarks'],device=device)
 up=t([sum(w for n,w in v['Weights'].items() if 'upperlip' in n.lower()) for v in vv])
 low=t([sum(w for n,w in v['Weights'].items() if 'lowerlip' in n.lower() or 'lowlip' in n.lower()) for v in vv])
 field=.5*(up-low);field[protect]=0;field/=field[gu].mean()-field[gl].mean()
 for n in aperture['ChangedChannels']:
  c=names.index(n);response=correction[c,gu,2].mean()-correction[c,gl,2].mean()
  correction[c,:,2]-=response*field
 # Bound additions uniformly per channel, preserving each control's direction.
 maximum=torch.linalg.vector_norm(correction,dim=-1).max(1).values
 factor=torch.clamp(.65/(maximum+1e-8),max=1)
 for _ in range(32):
  candidate=old+correction*factor[:,None,None]
  too_large=torch.linalg.vector_norm(candidate,dim=-1).max(1).values>1.58
  if not too_large.any():break
  factor[too_large]*=.9
 new=old+correction*factor[:,None,None];difference=new-old
 assert torch.equal(new[preserved],old[preserved]) and new[:,protect].abs().max()==0
 assert torch.linalg.vector_norm(new,dim=-1).max()<1.6 and torch.isfinite(new).all()
 for n in aperture['ChangedChannels']:
  c=names.index(n)
  assert abs(float(difference[c,gu,2].mean()-difference[c,gl,2].mean()))<1e-5
 changed=[i for i in range(37) if i not in preserved]
 before=(measured[changed]-desired[changed]).square().mean().sqrt()
 after=(torch.einsum('kv,cvd->ckd',measure,new[:-1])[changed]-desired[changed]).square().mean().sqrt()
 assert after<before,(actor,float(before),float(after))
 arrays=[new.cpu().numpy().astype('<f4')];lod_reports=[]
 for level,lod in enumerate(mesh['LODs'][1:],1):
  v=lod['Vertices'];p=t([x['Position'] for x in v]);dist,near=torch.topk(torch.cdist(p,pos),3,largest=False)
  blend=1/(dist.square()+1e-7);blend/=blend.sum(1,keepdim=True)
  extra=(difference[:,near]*blend[None,:,:,None]).sum(2)*torch.exp(-dist[:,0].square()/2)[None,:,None]
  protected=torch.tensor([any(w>0 and any(s in n.lower() for s in ['eye','lowlid','blink','neck','chest']) for n,w in x['Weights'].items()) for x in v],device=device)|protect[near[:,0]]
  extra[:,protected]=0;base=t(np.load(prior/f'{actor}-lod{level}-deltas.npy'));delta=base+extra
  assert delta[:,protected].abs().max()==0 and torch.linalg.vector_norm(delta,dim=-1).max()<1.6
  arrays.append(delta.cpu().numpy().astype('<f4'))
 for level,a in enumerate(arrays):
  np.save(out/f'meshes/{actor}-lod{level}-deltas.npy',a)
  lod_reports.append({'LOD':level,'Vertices':a.shape[1],'MaximumDelta':float(np.linalg.norm(a,axis=-1).max()),'ProtectedMaxDelta':0})
 rig=json.loads((prior/f'{actor}-rig.json').read_text())
 for c,target in enumerate(rig['Targets']):
  target['LODs']=[{'NumVertices':len(a[c]),'Vertices':[[int(i),*map(float,a[c,i])] for i in np.flatnonzero(np.linalg.norm(a[c],axis=1)>.002)]} for a in arrays]
 (out/f'meshes/{actor}-rig.json').write_text(json.dumps(rig,separators=(',',':')))
 reports.append({'Actor':actor,'ControlKeys':keys,'TargetControlPositions':target_anchors.cpu().tolist(),'SourceScaleDepthWidthHeight':scale.cpu().tolist(),'OldControlRMSE':float(before),'NewControlRMSE':float(after),'MaximumAddedDisplacement':float(torch.linalg.vector_norm(difference,dim=-1).max()),'ChannelCorrectionScales':factor.cpu().tolist(),'LODs':lod_reports})
report={'GPU':torch.cuda.get_device_name(),'NumericalDevice':'cuda','Method':'21 semantic skin-influence control points; regularized3D NVIDIA displacement fit with original-shape prior','SourceSHA256':hashlib.sha256(source_path.read_bytes()).hexdigest(),'SourceControlPositions':anchors.cpu().tolist(),'SourceControlIndices':source_indices,'Channels':names,'ChannelCount':len(names),'PreservedChannels':[names[i] for i in preserved],'PreservedApertureChannels':aperture['ChangedChannels'],'NeutralGeometryChanged':False,'SkeletonOrSkinWeightsChanged':False,'ProtectedVerticesMaxDelta':0,'Heads':reports,'RuntimeValidated':False,'Limitations':['annotated source controls and existing target influence semantics require visual validation','control-point residual is not a realism metric','no full lip contact or collision solver','broader human/asari compatibility not validated','shared Average rig remains current NPC runtime selection']}
(study/'semantic-rig.json').write_text(json.dumps(report,indent=2));(root/'research/intro-a2f/retarget-report.json').write_text(json.dumps(report,indent=2))
print(json.dumps([{k:x[k] for k in ['Actor','OldControlRMSE','NewControlRMSE','MaximumAddedDisplacement']} for x in reports],indent=2))

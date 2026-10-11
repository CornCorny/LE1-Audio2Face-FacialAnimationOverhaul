"""Bake a game-calibrated morph basis with CUDA skinning.

Audio2Face still supplies the 37 time-series controls. The stock facial poses
are anatomical reference shapes, NOT a regenerated FaceFX animation track.
Bake relative transforms onto each original head; original topology, skeleton,
skin weights and materials remain untouched. Eye/blink vertices stay zero.
"""
import json,numpy as np,torch
from pathlib import Path
root=Path(__file__).resolve().parents[2];out=root/'output/intro-a2f'
assert torch.cuda.is_available();device='cuda'
allnames=json.loads((out/'channels.json').read_text())
names=[n for n in allnames if not n.startswith('corrective')]
assert len(names)==37 and allnames==names+['correctiveJawLipSeal']
# One shared semantic mapping for all heads. Strengths are test calibration,
# not a claim that ARKit and LE1 controls have identical anatomical meaning.
mapping={
'jawOpen':{'jawOpen':.72},'jawForward':{'jawForward':.35},'jawLeft':{'jawSideLeft':.35},'jawRight':{'jawSideRight':.35},
'mouthClose':{'lowerLipUpLeft':.65,'lowerLipUpRight':.65,'upperLipDownLeft':.55,'upperLipDownRight':.55},
'mouthFunnel':{'O_mouth':.55},'mouthPucker':{'pucker':.5},
'mouthLeft':{'mouthDownLeft':.45},'mouthRight':{'mouthDownRight':.45},
'mouthSmileLeft':{'smileLeft':.5},'mouthSmileRight':{'smileRight':.5},
'mouthFrownLeft':{'frownLeft':.3},'mouthFrownRight':{'frownRight':.3},
'mouthDimpleLeft':{'smileLeft':.2,'mouthDownLeft':.2},'mouthDimpleRight':{'smileRight':.2,'mouthDownRight':.2},
'mouthStretchLeft':{'smileLeft':.35},'mouthStretchRight':{'smileRight':.35},
'mouthRollLower':{'lowerLipCurlIn':.55},'mouthRollUpper':{'upperLipCurlIn':.55},
'mouthShrugLower':{'lowerLipUpLeft':.3,'lowerLipUpRight':.3,'lowerLipCurlOut':.2},
'mouthShrugUpper':{'upperLipCurlOut':.35},
'mouthPressLeft':{'lowerLipUpLeft':.3,'upperLipDownLeft':.3},'mouthPressRight':{'lowerLipUpRight':.3,'upperLipDownRight':.3},
'mouthLowerDownLeft':{'lowerLipDownLeft':.65},'mouthLowerDownRight':{'lowerLipDownRight':.65},
'mouthUpperUpLeft':{'sneerLeft':.45},'mouthUpperUpRight':{'sneerRight':.45},
'browDownLeft':{'innerBrowLeftDown':.55,'outBrowLeftDown':.35},
'browDownRight':{'innerBrowRightDown':.55,'outBrowRightDown':.35},
'browInnerUp':{'innerBrowLeftUp':.6,'innerBrowRightUp':.6},
'browOuterUpLeft':{'outBrowLeftUp':.65},'browOuterUpRight':{'outBrowRightUp':.65},
'cheekPuff':{'cheekPuff':.45},
'cheekSquintLeft':{'cheekLeft':.45},'cheekSquintRight':{'cheekRight':.45},
'noseSneerLeft':{'sneerLeft':.45,'noseUp':.15},'noseSneerRight':{'sneerRight':.45,'noseUp':.15}}
assert set(mapping)==set(names),(set(names)-set(mapping),set(mapping)-set(names))
def tensor(a):return torch.tensor(a,dtype=torch.float32,device=device)
def quatmat(q):
    q=q/torch.linalg.vector_norm(q,dim=-1,keepdim=True);x,y,z,w=q.unbind(-1)
    return torch.stack([1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)],-1).reshape(*q.shape[:-1],3,3)
def globals_(local,skel):
    result=[]
    for i,b in enumerate(skel):result.append(result[b['ParentIndex']]@local[:,i] if i else local[:,i])
    return torch.stack(result,1)
def owned(n):return n.lower() in ['mouthbase','jawbone','sneer'] or any(s in n.lower() for s in ['lip','cheek','brow'])
reports=[]
for actor in ['Shepard','Joker','Kaidan','Average']:
    mesh=json.loads((out/f'meshes/{actor}.json').read_text(encoding='utf-8-sig'))
    graph=json.loads((root/f'research/intro-a2f/ownership/{actor}-facegraph.json').read_text(encoding='utf-8-sig'))
    skel=graph['Skeleton'];byname={b['Name']['Name'].lower():i for i,b in enumerate(skel)}
    bynode={n['Name']:n['Index'] for n in graph['Nodes']}
    assert all(n in bynode for x in mapping.values() for n in x)
    rest=torch.eye(4,device=device).repeat(len(skel),1,1)
    rest[:,:3,:3]=quatmat(tensor([[b['Orientation'][k] for k in 'XYZW'] for b in skel]))
    rest[:,:3,3]=tensor([[b['Position'][k] for k in 'XYZ'] for b in skel])
    local=rest[None].repeat(len(names),1,1,1)
    for bone in graph['Bones']:
        key=bone['Name'].lower()
        if key not in byname or not owned(key):continue
        i=byname[key];ref=bone['Data']['unkFloats']
        # FaceFX asset uses position Y and quaternion Y opposite mesh storage;
        # confirmed against all reference rotations and LEx PSK export.
        rq=quatmat(tensor([ref[1],-ref[2],ref[3],ref[0]]))
        for c,channel in enumerate(names):
            rd=torch.zeros(3,device=device);dq=torch.zeros(4,device=device);total=0.
            for child in bone['Data']['Children']:
                node=next((n for n in mapping[channel] if bynode[n]==child['CombinerIndex']),None)
                if node is None:continue
                w=mapping[channel][node];v=child['unkFloats'];total+=w
                rd+=tensor([v[0],-v[1],v[2]])*w
                q=tensor([v[4],-v[5],v[6],v[3]])
                refq=tensor([ref[1],-ref[2],ref[3],ref[0]])
                if torch.dot(q,refq)<0:q=-q
                dq+=q*w
            if not total:continue
            refq=tensor([ref[1],-ref[2],ref[3],ref[0]])
            dq+=refq*max(0.,1-total)
            rotation=quatmat(dq)@rq.transpose(0,1)
            local[c,i,:3,:3]=rotation@rest[i,:3,:3]
            local[c,i,:3,3]+=rd
    base=globals_(rest[None],skel)
    globalpose=globals_(local,skel)
    deform=globalpose@torch.linalg.inv(base)
    assert torch.isfinite(deform).all()
    # Neutral inverse-bind round-trip and head location sanity checks prevent
    # quaternion-convention errors from silently creating a plausible export.
    bind_error=float(torch.max(torch.abs(base@torch.linalg.inv(base)-torch.eye(4,device=device))))
    assert bind_error<.001,bind_error
    head=base[0,byname['head'],:3,3];assert 160<head[2]<180 and abs(head[0])<15
    arrays=[];lodreports=[];source_pos=None;source_protected=None
    for lod in mesh['LODs']:
        vv=lod['Vertices'];pos=tensor([v['Position'] for v in vv]);weights=torch.zeros((len(vv),len(skel)),device=device)
        protected=[]
        for vno,v in enumerate(vv):
            for n,w in v['Weights'].items():weights[vno,byname[n.lower()]]=w
            protected.append(any(w>0 and any(s in n.lower() for s in ['eye','lowlid','blink','neck','chest']) for n,w in v['Weights'].items()))
        assert torch.max(torch.abs(weights.sum(1)-1))<.005
        homogeneous=torch.cat([pos,torch.ones((len(vv),1),device=device)],1)
        moved=torch.einsum('vb,cbij,vj->cvi',weights,deform[:,:,:3,:],homogeneous)
        delta=moved-pos[None]
        protected=torch.tensor(protected,device=device)
        if source_pos is not None:
            # Reduced LODs can collapse all facial skinning onto Head. Transfer
            # THIS head's fitted LOD0 basis, not the unrelated NVIDIA surface.
            distances,near=torch.topk(torch.cdist(pos,source_pos),3,largest=False)
            blend=1/(distances.square()+1e-7);blend/=blend.sum(1,keepdim=True)
            delta=(tensor(arrays[0][:-1])[:,near]*blend[None,:,:,None]).sum(2)
            delta*=torch.exp(-distances[:,0].square()/2)[None,:,None]
            protected|=source_protected[near[:,0]]
        delta[:,protected]=0
        if source_pos is None:
            # Stretch widens the lips without smile's upward cheek/corner pull.
            # Preserve the game-calibrated contour, separate its semantics.
            for side in ['Left','Right']:
                delta[names.index('mouthStretch'+side),:,[0,2]]=0
            # Opposed horizontal mouth translation uses the original skin's
            # mouth/lip influence rather than the unrelated mouthDown pose.
            lip=torch.zeros(len(vv),device=device)
            for bno,b in enumerate(skel):
                n=b['Name']['Name'].lower()
                if 'lip' in n or n=='mouthbase':lip+=weights[:,bno]
            lip=lip.clamp(0,1);lip[protected]=0
            # Actual LipCorner_Left centroid is negative meshY (not positive).
            for side,sign in [('Left',-1),('Right',1)]:
                c=names.index('mouth'+side);delta[c]=0;delta[c,:,1]=sign*.45*lip
        # Conservative finite bounds; includes jaw-skinned teeth/tongue so they
        # follow the authored jaw arc instead of the nearest external skin point.
        size=torch.linalg.vector_norm(delta,dim=-1)
        # A uniform scale per channel preserves its jaw/dental arc. Clipping
        # individual vertices would flatten the shape and split dental motion.
        scale=torch.clamp(1.55/(size.max(1).values+1e-8),max=1)
        delta*=scale[:,None,None]
        assert torch.isfinite(delta).all() and delta[:,protected].abs().max()==0
        if source_pos is None:
            # A2F mouthClose describes lip sealing with the jaw still open.
            # Correct only jaw's displacement of lip skin, never the dental
            # mesh. Its clip weight is jawOpen*mouthClose, zero if either is0.
            seal=-delta[names.index('jawOpen')]*lip[:,None]
        else:
            seal=(tensor(arrays[0][-1])[near]*blend[:,:,None]).sum(1)
            seal*=torch.exp(-distances[:,0].square()/2)[:,None]
        seal[protected]=0
        delta=torch.cat([delta,seal[None]],0)
        arr=delta.cpu().numpy().astype('<f4');arrays.append(arr)
        if source_pos is None:source_pos=pos;source_protected=protected
        level=lod['LOD'] if 'LOD' in lod else len(arrays)-1
        np.save(out/f'meshes/{actor}-lod{level}-deltas.npy',arr)
        lodreports.append({'LOD':level,'Vertices':len(vv),'ProtectedVertices':int(protected.sum()),'ProtectedMaxDelta':0,'MaxDelta':float(torch.linalg.vector_norm(delta,dim=-1).max()),'ClampedVertexChannels':int((size>1.55).sum()),'ChangedVertices':int((delta.square().sum(-1).max(0).values>.002**2).sum())})
    targets=[{'Name':'MEF_A2F_'+n,'Channel':n,'LODs':[{'NumVertices':len(a[c]),'Vertices':[[int(i),*map(float,a[c,i])] for i in np.flatnonzero(np.linalg.norm(a[c],axis=1)>.002)]} for a in arrays]} for c,n in enumerate(allnames)]
    rig={'Actor':actor,'Mesh':mesh['Mesh'],'Channels':allnames,'Targets':targets,'SourceExportSHA256':mesh['SourceExportSHA256']}
    (out/f'meshes/{actor}-rig.json').write_text(json.dumps(rig,separators=(',',':')))
    reports.append({'Actor':actor,'HeadBindPosition':head.cpu().tolist(),'LODs':lodreports})
report={'GPU':torch.cuda.get_device_name(0),'NumericalDevice':'cuda','Method':'game-calibrated facial pose reference with separated stretch/translation and coupled jaw/lip seal corrective','Mapping':mapping,'Channels':allnames,'ChannelCount':len(allnames),'NoEyeOrEyelidVerticesChanged':True,'DentalMotion':'original skin weights and jaw arc; seal corrective affects lip skin only','VanillaFaceFX':'owned bone outputs masked only during active supported line; eyes/blink/head/body retained','Heads':reports,'RuntimeValidated':False,'Limitations':['semantic control mapping and gains require visual calibration','linear base morph combinations approximate nonlinear skeletal poses','no tongue-out channel; protected eye region constrains cheek poses','lip seal corrective is not a complete contact/collision solver']}
(root/'research/intro-a2f/retarget-report.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report['Heads'],indent=2))

"""Calibrate lip closing against the solved NVIDIA surface; preserve the bridge.

Outer-lip landmarks measure separation, not true inner-lip contact. All fitting
and combined-frame measurements use CUDA. No surface nearest-neighbour transfer,
timestamps, jaw/dental motion, lateral shapes or protected vertices are changed.
"""
import hashlib
import json
import struct
from pathlib import Path
import numpy as np
import torch

root = Path(__file__).resolve().parents[2]
out = root/'output/intro-a2f'
study = root/'research/intro-a2f/correction-0.2.4'
prior = study/'baseline-0.2.3/meshes'
assert torch.cuda.is_available()
device = 'cuda'
names = json.loads((out/'channels.json').read_text())
assert len(names) == 38 and names[-1] == 'correctiveJawLipSeal'
source_path = root/'output/sdk-validation/_data/generated/audio2face-sdk/samples/data/multi-diffusion/bs_skin_Mark.npz'
source = np.load(source_path)
sp = torch.tensor(source['neutral'], device=device)
# Central front vermilion, selected in the unmodified Mark source coordinates.
# Explicit masks are preserved in the report so this proxy can be challenged.
su = torch.where((sp[:,0].abs()<.4)&(sp[:,1]>152.8)&(sp[:,1]<153.2)&(sp[:,2]>6.5))[0]
sl = torch.where((sp[:,0].abs()<.4)&(sp[:,1]>151.9)&(sp[:,1]<152.35)&(sp[:,2]>6.35))[0]
assert len(su)>8 and len(sl)>8
sb = torch.tensor(np.stack([source[n] for n in names[:-1]]), device=device)
source_response = sb[:,su,1].mean(1)-sb[:,sl,1].mean(1)
assert 2<source_response[names.index('jawOpen')]<3
correct = ['mouthClose','mouthRollLower','mouthRollUpper','mouthShrugLower',
           'mouthShrugUpper','mouthPressLeft','mouthPressRight']
scene = json.loads((out/'scene-manifest.json').read_text())
records = []
for actor in ['Shepard','Joker','Kaidan','Average']:
    mesh_path = prior/f'{actor}.json'
    mesh = json.loads(mesh_path.read_text(encoding='utf-8-sig'))
    vertices = mesh['LODs'][0]['Vertices']
    pos = torch.tensor([v['Position'] for v in vertices], device=device)
    # Select from each head's own semantic skin influences; no character IDs.
    upper = torch.tensor([sum(w for n,w in v['Weights'].items() if n.lower() in
                           ['upperlip_left','upperlip_right']) for v in vertices],device=device)
    lower = torch.tensor([sum(w for n,w in v['Weights'].items() if n.lower() in
                           ['lowerlip_left','lowerlip_right']) for v in vertices],device=device)
    candidates = [torch.where((w>.9)&(pos[:,1].abs()<.8))[0] for w in [upper,lower]]
    assert all(len(x)>=3 for x in candidates)
    # Average the four foremost eligible samples, including UV splits when
    # present, rather than depending on one head's particular vertex density.
    gu,gl = [c[torch.topk(pos[c,0],min(4,len(c))).indices] for c in candidates]
    assert len(gu)>=2 and len(gl)>=2
    old = torch.tensor(np.load(prior/f'{actor}-lod0-deltas.npy'),device=device)
    response = old[:,gu,2].mean(1)-old[:,gl,2].mean(1)
    scale = response[names.index('jawOpen')]/source_response[names.index('jawOpen')]
    assert .2<float(scale)<1.2
    protected = torch.tensor([any(w>0 and any(t in n.lower() for t in
                         ['eye','lowlid','blink','neck','chest']) for n,w in v['Weights'].items())
                         for v in vertices],device=device)
    # A continuous skin-weight field changes lip separation only. Dental and
    # non-lip vertices remain exactly unchanged; full source transfer is avoided.
    up = torch.tensor([sum(w for n,w in v['Weights'].items() if 'upperlip' in n.lower())
                       for v in vertices],device=device)
    low = torch.tensor([sum(w for n,w in v['Weights'].items() if
                        'lowerlip' in n.lower() or 'lowlip' in n.lower()) for v in vertices],device=device)
    field = .5*(up-low)
    field[protected] = 0
    field /= field[gu].mean()-field[gl].mean()
    assert field.abs().max()<1
    new = old.clone()
    changes = []
    for name in correct:
        c = names.index(name)
        desired = scale*source_response[c]
        difference = desired-response[c]
        assert abs(float(difference))<.65,(actor,name,float(difference))
        new[c,:,2] += difference*field
        assert torch.linalg.vector_norm(new[c],dim=1).max()<1.6
        changes.append({'Channel':name,'OldSeparationResponse':float(response[c]),
                        'SourceSeparationResponse':float(source_response[c]),
                        'TargetSeparationResponse':float(desired),
                        'NewSeparationResponse':float(new[c,gu,2].mean()-new[c,gl,2].mean()),
                        'MaximumAddedDisplacement':float((difference*field).abs().max())})
    # The solved source is linear; this invented jawOpen*mouthClose cancellation
    # was an additional closing contribution with no counterpart in that basis.
    # Retain the38-slot schema and clip files, but make this legacy target empty.
    new[-1] = 0
    assert torch.equal(new[names.index('jawOpen')],old[names.index('jawOpen')])
    untouched = [i for i,n in enumerate(names) if n not in correct+[names[-1]]]
    assert torch.equal(new[untouched],old[untouched])
    assert new[:,protected].abs().max()==0
    nonlip = (up+low)==0
    assert torch.equal(new[:-1,nonlip],old[:-1,nonlip])
    arrays = [new.cpu().numpy().astype('<f4')]
    lods = [{'LOD':0,'ProtectedMaxDelta':0,'MaxDelta':float(torch.linalg.vector_norm(new,dim=-1).max())}]
    difference = new-old
    for level,lod in enumerate(mesh['LODs'][1:],1):
        vv=lod['Vertices']
        p=torch.tensor([v['Position'] for v in vv],device=device)
        distances,near=torch.topk(torch.cdist(p,pos),3,largest=False)
        blend=1/(distances.square()+1e-7);blend/=blend.sum(1,keepdim=True)
        extra=(difference[:,near]*blend[None,:,:,None]).sum(2)
        extra*=torch.exp(-distances[:,0].square()/2)[None,:,None]
        protect=torch.tensor([any(w>0 and any(t in n.lower() for t in
                        ['eye','lowlid','blink','neck','chest']) for n,w in v['Weights'].items())
                        for v in vv],device=device)|protected[near[:,0]]
        extra[:,protect]=0
        base=torch.tensor(np.load(prior/f'{actor}-lod{level}-deltas.npy'),device=device)
        delta=base+extra;delta[-1]=0
        assert delta[:,protect].abs().max()==0
        assert torch.linalg.vector_norm(delta,dim=-1).max()<1.6
        arrays.append(delta.cpu().numpy().astype('<f4'))
        lods.append({'LOD':level,'ProtectedMaxDelta':0,'MaxDelta':float(torch.linalg.vector_norm(delta,dim=-1).max())})
    comparisons=[]
    for line in scene['Lines']:
        if actor=='Shepard' and line['Actor']!='Shepard':continue
        if actor in ['Joker','Kaidan'] and line['Actor']!=actor:continue
        if actor=='Average' and line['Actor']=='Shepard':continue
        data=(out/'clips'/line['Clip']).read_bytes()
        count=struct.unpack_from('<I',data,8)[0]
        frame=np.frombuffer(data[32:],dtype=np.dtype([('t','<u4'),('w','<f4',(count,))]))
        w=torch.tensor(frame['w'].copy(),device=device)
        target=w[:,:37]@(scale*source_response)
        before=w@response
        after=w@(new[:,gu,2].mean(1)-new[:,gl,2].mean(1))
        comparisons.append({'StringRef':line['StringRef'],
                            'OldSeparationRMSE':float((before-target).square().mean().sqrt()),
                            'NewSeparationRMSE':float((after-target).square().mean().sqrt()),
                            'OldMeanChange':float(before.mean()),'NewMeanChange':float(after.mean()),
                            'TargetMeanChange':float(target.mean())})
    oldmean=np.mean([x['OldSeparationRMSE'] for x in comparisons])
    newmean=np.mean([x['NewSeparationRMSE'] for x in comparisons])
    assert newmean<oldmean,(actor,oldmean,newmean)
    for level,a in enumerate(arrays):np.save(out/f'meshes/{actor}-lod{level}-deltas.npy',a)
    rig=json.loads((prior/f'{actor}-rig.json').read_text())
    for c,target in enumerate(rig['Targets']):
        target['LODs']=[{'NumVertices':len(a[c]),'Vertices':[[int(i),*map(float,a[c,i])]
                            for i in np.flatnonzero(np.linalg.norm(a[c],axis=1)>.002)]} for a in arrays]
    (out/f'meshes/{actor}-rig.json').write_text(json.dumps(rig,separators=(',',':')))
    records.append({'Actor':actor,'UpperLandmarks':gu.cpu().tolist(),'LowerLandmarks':gl.cpu().tolist(),
                    'SourceToGameJawSeparationScale':float(scale),'Changes':changes,'LODs':lods,
                    'OldMeanSeparationRMSE':float(oldmean),'NewMeanSeparationRMSE':float(newmean),
                    'Lines':comparisons})
report={'GPU':torch.cuda.get_device_name(),'NumericalDevice':'cuda','SourceSHA256':hashlib.sha256(source_path.read_bytes()).hexdigest(),
        'SourceUpperLandmarks':su.cpu().tolist(),'SourceLowerLandmarks':sl.cpu().tolist(),
        'Method':'Source-calibrated vertical lip-closing responses, jaw-normalized; no full surface transfer',
        'ChangedChannels':correct,'DisabledLegacyCorrective':'correctiveJawLipSeal',
        'Channels':names,'ChannelCount':38,'Heads':records,'RuntimeValidated':False,
        'Preserved':'all curves/audio/timestamps; jaw/dental arc; non-lip vertices; lateral/depth movement; brows/eyes/blinks/body; shaders/native lifecycle',
        'Limitations':['outer-vermilion separation is an articulation proxy, not inner-lip contact or a collision solver',
                       'one-dimensional calibration does not establish full anatomical source fidelity','in-game quality requires recording']}
(study/'aperture-fit.json').write_text(json.dumps(report,indent=2))
(root/'research/intro-a2f/retarget-report.json').write_text(json.dumps(report,indent=2))
print(json.dumps([{'Actor':x['Actor'],'OldRMSE':x['OldMeanSeparationRMSE'],'NewRMSE':x['NewMeanSeparationRMSE']} for x in records],indent=2))

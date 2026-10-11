"""Release checks for shared detail transfer; uses serialized morph vertices."""
import hashlib,json,re,struct
from pathlib import Path
import numpy as np,torch
root=Path(__file__).resolve().parents[2];out=root/'output/intro-a2f'
p=root/'research/intro-a2f/correction-0.2.7';old=p/'baseline-0.2.6'
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert torch.cuda.is_available()
def t(a):return torch.tensor(a,device='cuda',dtype=torch.float32)
def frames(path):
    b=path.read_bytes();c,n=struct.unpack_from('<II',b,8)
    f=np.frombuffer(b[32:],dtype=np.dtype([('t','<u4'),('w','<f4',(c,))]));assert len(f)==n
    return f['t'].copy(),t(f['w'].copy())
scene=json.loads((out/'scene-manifest.json').read_text());names=json.loads((out/'channels.json').read_text())
assert len(names)==48 and names[:38]==json.loads((old/'channels.json').read_text())
assert sha(out/'scene-manifest.json')==sha(old/'scene-manifest.json')
prior_audio={x['StringRef']:x['AudioSHA256'] for x in json.loads((old/'clip-audit.json').read_text())['Clips']}
data={}
for line in scene['Lines']:
    assert sha(out/line['Audio'])==prior_audio[line['StringRef']]
    tm,w=frames(out/'clips'/line['Clip']);ot,ow=frames(old/'clips'/line['Clip'])
    assert np.array_equal(tm,ot) and w.shape[1]==48
    assert torch.isfinite(w).all() and w.min()>=0 and w.max()<=1 and w[[0,-1]].abs().max()==0
    assert ((w[:,38::2]*w[:,39::2])<.03).all(),'signed smoothing overlap unexpectedly large'
    data[line['StringRef']]=(w,ow)
reports=[];lip=[]
for actor in ['Shepard','Joker','Kaidan','Average']:
    assert sha(out/f'meshes/{actor}.json')==sha(old/f'meshes/{actor}.json')
    mesh=json.loads((out/f'meshes/{actor}.json').read_text(encoding='utf-8-sig'))
    rig=json.loads((out/f'meshes/{actor}-rig.json').read_text());priorrig=json.loads((old/f'meshes/{actor}-rig.json').read_text())
    assert rig['Targets'][:38]==priorrig['Targets'] and len(rig['Targets'])==48
    lines=[x for x in scene['Lines'] if (x['Actor']=='Shepard' if actor=='Shepard' else x['Actor']!='Shepard' if actor=='Average' else x['Actor']==actor)]
    for level,lod in enumerate(mesh['LODs']):
        a=np.load(out/f'meshes/{actor}-lod{level}-deltas.npy');b=np.load(old/f'meshes/{actor}-lod{level}-deltas.npy')
        assert np.array_equal(a[:38],b)
        assert np.isfinite(a).all() and np.linalg.norm(a,axis=-1).max()<1.601
        protected=np.array([any(w>0 and any(s in n.lower() for s in ['eye','lowlid','blink','neck','chest']) for n,w in v['Weights'].items()) for v in lod['Vertices']])
        assert np.max(abs(a[:,protected]))==0
        # Exactly the sparse arrays written to PCC, including 0.002 threshold.
        sparse=np.zeros_like(a)
        for c,shape in enumerate(rig['Targets']):
            for idx,x,y,z in shape['LODs'][level]['Vertices']:sparse[c,idx]=[x,y,z]
        assert np.max(abs(sparse[:,protected]))==0
        basis=t(sparse).flatten(1);worst=0;peakline=0
        for line in lines:
            w,_=data[line['StringRef']];d=(w@basis).reshape(len(w),-1,3)
            assert torch.isfinite(d).all() and d[:,protected].abs().max()==0
            maximum=float(torch.linalg.vector_norm(d,dim=-1).max());assert maximum<3.0,(actor,level,maximum)
            if maximum>worst:worst=maximum;peakline=line['StringRef']
        reports.append({'Head':actor,'LOD':level,'Lines':len(lines),'MaximumCombinedDelta':worst,'PeakStringRef':peakline,'ProtectedMaximum':0,'Original38ShapesIdentical':True})
        if level:continue
        vv=lod['Vertices'];pos=t([v['Position'] for v in vv])
        fields=[t([sum(w for n,w in v['Weights'].items() if n.lower() in keys) for v in vv]) for keys in [('upperlip_left','upperlip_right'),('lowerlip_left','lowerlip_right')]]
        candidates=[torch.where((w>.9)&(pos[:,1].abs()<.8))[0] for w in fields]
        gu,gl=[idx[torch.topk(pos[idx,0],min(4,len(idx))).indices] for idx in candidates]
        gap=float(pos[gu,2].mean()-pos[gl,2].mean())
        response=t(sparse)[:,gu,2].mean(1)-t(sparse)[:,gl,2].mean(1)
        oldresponse=t(b)[:,gu,2].mean(1)-t(b)[:,gl,2].mean(1)
        for line in lines:
            w,ow=data[line['StringRef']];newgap=gap+w@response;oldgap=gap+ow@oldresponse
            # Outer vermilion proxy, not proof of inner lip contact/collisions.
            assert float(newgap.min())>=min(0,float(oldgap.min()))-.08,(actor,line['StringRef'],float(newgap.min()))
            lip.append({'Head':actor,'StringRef':line['StringRef'],'BaselineMinimumOuterLipSeparation':float(oldgap.min()),'CandidateMinimum':float(newgap.min()),'MaximumSeparationChange':float((newgap-oldgap).abs().max())})
source=root/'src/asi/single-line-bridge';changed=[];unchanged=[]
def function(text,name):
    match=re.search(r'^[^\n]*\b'+name+r'\([^;\n]*\)\s*\{',text,re.M);assert match,name
    start=text.index('{',match.start());end=start+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[match.start():end]
preserved_functions=['Pose','Bind','Tick','OwnershipBound','RefreshOwnershipMap','PrepareOwnershipRoute','OwnershipFault','FaceRebuild','PrepareMaterials','MaterialOnly','JokerLoadOnly','RenderState','RestoreMaterialProbe']
before=(old/'source/Bridge.cpp').read_text();after=(source/'Bridge.cpp').read_text()
for name in preserved_functions:assert function(before,name)==function(after,name),name
allowed={'Bridge.cpp','GeneratedScene.hpp','FaceTransition.hpp','FaceTransitionTests.cpp'}
for f in (old/'source').iterdir():
    if not f.is_file():continue
    if sha(f)==sha(source/f.name):unchanged.append(f.name)
    else:assert f.name in allowed,f.name;changed.append(f.name)
shader=root/'output/single-line-bridge/assets/MEFFaceShaders.pcc'
assert sha(shader)==sha(old/'assets/MEFFaceShaders.pcc')
rr=json.loads((root/'research/intro-a2f/morph-roundtrip.json').read_text(encoding='utf-8-sig'))
assert len(rr)==4 and all(x['BinaryRoundTripIdentical'] and len(x['Targets'])==48 for x in rr)
fit=json.loads((p/'full-detail/detail-rig-audit.json').read_text());assert fit['TrainingLines']==30
report={'Revision':'0.2.7','GPU':torch.cuda.get_device_name(),'SharedDetailChannels':10,'TotalChannels':48,
 'OriginalAudioAndTimestampsPreserved':30,'OriginalSceneManifestPreserved':True,'OriginalMeshSkeletonSkinMaterialExportsIdentical':True,
 'PreservedRuntimeFiles':unchanged,'PreservedRuntimeFunctions':preserved_functions,'ChangedRuntimeFiles':changed,'ShaderUnchanged':True,'ShaderSHA256':sha(shader),
 'CombinedSparseMorphChecks':reports,'OuterLipProxy':lip,'SourceControlFit':fit['Regions'],
 'EmotionScale':.85,'EmotionTotalCap':.45,'SpeechHandoverSeconds':.16,'BrowHandoverSeconds':.24,
 'RuntimeValidated':False,'Limitations':['source-control fit does not measure final acting realism','outer lip proxy is not an inner lip collision solver','all30 lines authoring checked; live paths and handover still need recording']}
(p/'iteration-audit.json').write_text(json.dumps(report,indent=2))
print(json.dumps({k:v for k,v in report.items() if k not in ['OuterLipProxy','CombinedSparseMorphChecks','PreservedRuntimeFiles']},indent=2))

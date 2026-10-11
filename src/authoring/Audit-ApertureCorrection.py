"""Release gates for the0.2.4 asset-only lip correction."""
import hashlib,json
from pathlib import Path
import numpy as np
root=Path(__file__).resolve().parents[2];out=root/'output/intro-a2f'
study=root/'research/intro-a2f/correction-0.2.4';old=study/'baseline-0.2.3'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source=root/'src/asi/single-line-bridge'
before=(old/'source/Bridge.cpp').read_text();after=(source/'Bridge.cpp').read_text()
assert before.replace('L"0.2.3"','L"0.2.4"')==after
unchanged=[]
for path in source.iterdir():
    if path.is_file() and path.name!='Bridge.cpp':
        assert sha(path)==sha(old/'source'/path.name),path.name
        unchanged.append(path.name)
prior=out/'release/MEFaces-IntroA2F-0.2.3'
scene=json.loads((out/'scene-manifest.json').read_text())
audio_audit=json.loads((old/'clip-audit.json').read_text())
for line in scene['Lines']:
    assert sha(out/'clips'/line['Clip'])==sha(prior/'Game/ME1/Binaries/Win64/ASI/MEFIntroClips'/line['Clip'])
    expected=next(x['AudioSHA256'] for x in audio_audit['Clips'] if x['StringRef']==line['StringRef'])
    assert sha(out/line['Audio'])==expected
names=json.loads((out/'channels.json').read_text())
changed=json.loads((study/'aperture-fit.json').read_text())['ChangedChannels']
untouched=[i for i,n in enumerate(names) if n not in changed+[names[-1]]]
heads=[]
for actor in ['Shepard','Joker','Kaidan','Average']:
    mesh=json.loads((out/f'meshes/{actor}.json').read_text(encoding='utf-8-sig'))
    assert sha(out/f'meshes/{actor}.json')==sha(old/f'meshes/{actor}.json')
    for level,lod in enumerate(mesh['LODs']):
        a=np.load(out/f'meshes/{actor}-lod{level}-deltas.npy')
        b=np.load(old/f'meshes/{actor}-lod{level}-deltas.npy')
        protected=np.array([any(w>0 and any(t in n.lower() for t in
                          ['eye','lowlid','blink','neck','chest']) for n,w in v['Weights'].items())
                          for v in lod['Vertices']])
        assert np.array_equal(a[untouched],b[untouched]),(actor,level,'untouched')
        assert np.array_equal(a[:-1,:,:2],b[:-1,:,:2]),(actor,level,'lateral/depth')
        assert np.max(abs(a[:,protected]))==0
        assert np.max(abs(a[-1]))==0
        assert np.isfinite(a).all() and np.linalg.norm(a,axis=2).max()<1.6
    rig=json.loads((out/f'meshes/{actor}-rig.json').read_text())
    empty=[x['Channel'] for x in rig['Targets'] if not x['LODs'][0]['Vertices']]
    assert empty==['cheekSquintLeft','cheekSquintRight','correctiveJawLipSeal'],empty
    heads.append({'Actor':actor,'UntouchedShapesIdentical':len(untouched),'ProtectedMaxDelta':0,
                  'LegacySealEmpty':True,'LateralDepthPreserved':True,'LODs':len(mesh['LODs'])})
shader=root/'output/single-line-bridge/assets/MEFFaceShaders.pcc'
assert sha(shader)==sha(prior/'Game/ME1/BioGame/CookedPCConsole/MEFFaceShaders.pcc')
roundtrip=json.loads((root/'research/intro-a2f/morph-roundtrip.json').read_text(encoding='utf-8-sig'))
assert len(roundtrip)==4 and all(x['BinaryRoundTripIdentical'] and not x['BoneOffsets'] for x in roundtrip)
report={'RuntimeSourceOnlyVersionLabelChanged':True,'UnchangedRuntimeFiles':unchanged,
        'UnchangedClipsAndOriginalAudio':len(scene['Lines']),'ShaderUnchanged':True,
        'ShaderSHA256':sha(shader),'OriginalMeshExportsUnchanged':True,'Heads':heads,
        'MorphRoundTrips':4,'ChangedLipChannels':changed,'RuntimeValidated':False}
(study/'iteration-audit.json').write_text(json.dumps(report,indent=2))
print(json.dumps({k:v for k,v in report.items() if k not in ['UnchangedRuntimeFiles','Heads']},indent=2))

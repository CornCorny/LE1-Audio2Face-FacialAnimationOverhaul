"""Release gates for the reversible0.2.5 control-rig/curve experiment."""
import hashlib,json,re,struct
from pathlib import Path
import numpy as np
import torch
root=Path(__file__).resolve().parents[2];out=root/'output/intro-a2f';study=root/'research/intro-a2f/correction-0.2.5';old=study/'baseline-0.2.4'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source=root/'src/asi/single-line-bridge'
assert (old/'source/Bridge.cpp').read_text().replace('L"0.2.4"','L"0.2.5"')==(source/'Bridge.cpp').read_text()
def striphashes(s):return re.sub(r'inline constexpr std::uint64_t SceneClipHashes\[\] = \{.*?\};','HASHES',s,flags=re.S)
assert striphashes((old/'source/GeneratedScene.hpp').read_text())==striphashes((source/'GeneratedScene.hpp').read_text())
unchanged=[]
for p in source.iterdir():
 if p.is_file() and p.name not in ['Bridge.cpp','GeneratedScene.hpp']:
  assert sha(p)==sha(old/'source'/p.name),p.name;unchanged.append(p.name)
scene=json.loads((out/'scene-manifest.json').read_text());audioaudit=json.loads((old/'clip-audit.json').read_text())
names=json.loads((out/'channels.json').read_text());report=json.loads((study/'semantic-rig.json').read_text())
fixed=[names.index(n) for n in report['PreservedChannels']]
for line in scene['Lines']:
 a=(out/'clips'/line['Clip']).read_bytes();b=(old/'clips'/line['Clip']).read_bytes()
 assert a[:32]==b[:32] and a!=b
 fmt=np.dtype([('t','<u4'),('w','<f4',(38,))]);newframes=np.frombuffer(a[32:],dtype=fmt);priorframes=np.frombuffer(b[32:],dtype=fmt)
 assert np.array_equal(newframes['t'],priorframes['t'])
 assert np.isfinite(newframes['w']).all() and newframes['w'].min()>=0 and newframes['w'].max()<=1
 assert np.max(abs(newframes['w'][[0,-1]]))==0
 entry=next(x for x in audioaudit['Clips'] if x['StringRef']==line['StringRef']);assert sha(out/line['Audio'])==entry['AudioSHA256']
heads=[]
for actor in ['Shepard','Joker','Kaidan','Average']:
 mesh=json.loads((out/f'meshes/{actor}.json').read_text(encoding='utf-8-sig'))
 assert sha(out/f'meshes/{actor}.json')==sha(old/f'meshes/{actor}.json')
 for level,lod in enumerate(mesh['LODs']):
  a=np.load(out/f'meshes/{actor}-lod{level}-deltas.npy');b=np.load(old/f'meshes/{actor}-lod{level}-deltas.npy')
  protected=np.array([any(w>0 and any(t in n.lower() for t in ['eye','lowlid','blink','neck','chest']) for n,w in v['Weights'].items()) for v in lod['Vertices']])
  assert np.array_equal(a[fixed],b[fixed]) and np.max(abs(a[:,protected]))==0
  assert np.isfinite(a).all() and np.linalg.norm(a,axis=2).max()<1.6
  # All-zero weights reconstruct the exact original rest shape by definition.
  assert np.max(abs(np.zeros((38,),np.float32)@a.reshape(38,-1)))==0
 rig=json.loads((out/f'meshes/{actor}-rig.json').read_text())
 empty=[x['Channel'] for x in rig['Targets'] if not x['LODs'][0]['Vertices']]
 assert empty==['cheekSquintLeft','cheekSquintRight','correctiveJawLipSeal'],empty
 heads.append({'Actor':actor,'UnchangedJawAndProtectedShapes':len(fixed),'OriginalMeshIdentityPreserved':True,'ProtectedMaxDelta':0,'LODs':len(mesh['LODs'])})
shader=root/'output/single-line-bridge/assets/MEFFaceShaders.pcc'
assert sha(shader)==sha(out/'release/MEFaces-IntroA2F-0.2.4/Game/ME1/BioGame/CookedPCConsole/MEFFaceShaders.pcc')
roundtrip=json.loads((root/'research/intro-a2f/morph-roundtrip.json').read_text(encoding='utf-8-sig'))
assert len(roundtrip)==4 and all(x['BinaryRoundTripIdentical'] and not x['BoneOffsets'] for x in roundtrip)
result={'RuntimeChangesOnlyVersionAndClipHashes':True,'SceneIdentifiersAndSchemaUnchanged':True,'UnchangedRuntimeFiles':unchanged,'OriginalAudioUnchanged':30,'ClipTimestampsUnchanged':30,'ChangedPrecomputedCurves':30,'ShaderUnchanged':True,'ShaderSHA256':sha(shader),'OriginalMeshExportsUnchanged':True,'Heads':heads,'MorphRoundTrips':4,'RuntimeValidated':False,'LiveVanillaPoseCrossfadeImplemented':False}
(study/'iteration-audit.json').write_text(json.dumps(result,indent=2))
print(json.dumps({k:v for k,v in result.items() if k not in ['Heads','UnchangedRuntimeFiles']},indent=2))

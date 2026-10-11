"""Release evidence for the shared emotion/transition experiment."""
from pathlib import Path
import csv, hashlib, json, struct
import numpy as np

root=Path(__file__).resolve().parents[2]
p=root/'research/intro-a2f/correction-0.2.6'; old=p/'baseline-0.2.5'
out=root/'output/intro-a2f'; sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
scene=json.loads((out/'scene-manifest.json').read_text())
assert len(scene['Lines'])==30
assert {a:sum(x['Actor']==a for x in scene['Lines']) for a in ['Joker','Kaidan','Shepard']}=={'Joker':19,'Kaidan':5,'Shepard':6}
assets=[]
for f in (old/'assets').glob('*.pcc'):
    current=root/'output/single-line-bridge/assets'/f.name
    assert sha(f)==sha(current),f
    assets.append({'Name':f.name,'SHA256':sha(current)})
assert len(assets)==5
meshes=[]
for f in (old/'meshes').iterdir():
    if f.is_file():
        assert sha(f)==sha(out/'meshes'/f.name),f
        meshes.append(f.name)
assert meshes
verification=json.loads((p/'download-verification.json').read_text());assert verification['matches']
engine=json.loads((p/'a2e-model-validation.json').read_text());assert engine['EngineExitCode']==0
conditioning=json.loads((p/'emotion-conditioning-audit.json').read_text());assert len(conditioning['Records'])==30
clips=json.loads((p/'candidate-clip-audit.json').read_text());records=[]
for line in scene['Lines']:
    before=(old/'clips'/line['Clip']).read_bytes();after=(out/'clips'/line['Clip']).read_bytes()
    assert before[:32]==after[:32]
    channels=struct.unpack_from('<I',after,8)[0];assert channels==38
    dtype=np.dtype([('t','<u4'),('w','<f4',(channels,))])
    a=np.frombuffer(before[32:],dtype=dtype);b=np.frombuffer(after[32:],dtype=dtype)
    assert np.array_equal(a['t'],b['t']) and np.isfinite(b['w']).all()
    assert b['w'].min()>=0 and b['w'].max()<=1 and not b['w'][[0,-1]].any()
    audit=next(x for x in clips['Clips'] if x['StringRef']==line['StringRef'])
    assert sha(out/line['Audio'])==audit['AudioSHA256']
    assert sha(out/'clips'/line['Clip'])==audit['SHA256']
    def read_raw(f):
        rows=list(csv.reader(f.open()));return rows[0],np.array(rows[1:],dtype=np.float64)
    names,raw=read_raw(p/'raw-conditioned'/f"VO_{line['StringRef']}_M.csv")
    prior_names,prior_raw=read_raw(old/'raw'/f"VO_{line['StringRef']}_M.csv")
    assert names==prior_names and np.array_equal(raw[:,0],prior_raw[:,0])
    diff=float(np.abs(raw[:,1:]-prior_raw[:,1:]).max())
    assert diff>1e-5,'Conditioned inference unexpectedly identical'
    records.append({'StringRef':line['StringRef'],'RawConditioningMaxDifference':diff,'AudioAndClipTimingUnchanged':True,'ClipSHA256':audit['SHA256']})
preservation=json.loads((root/'research/intro-a2f/preservation-audit.json').read_text())
assert not preservation['PlayerRequiresAIStack'] and preservation['OriginalDialoguePackageAndAudioUnchanged']
gpu=json.loads((root/'research/intro-a2f/combined-gpu-audit.json').read_text());assert gpu['ProtectedVerticesMaxDelta']==0
assert '100% tests passed' in (p/'ctest.txt').read_text()
report={'Version':'0.2.6','Clips':30,'Channels':38,'EmotionModel':'Audio2Emotion-v2.2','Inference':'local RTX5070Ti CUDA, Mark identity2 constant noise','ManualEmotionOverrides':0,'AssetsUnchangedFrom025':assets,'UnchangedMeshFiles':len(meshes),'Records':records,'ProtectedMorphVerticesMaxDelta':0,'RuntimeHandover':'160ms audio-relative named facial blend; immediate existing skip cleanup','NativeTestsPassed':11,'PlayerRequiresAIStack':False,'RuntimeValidated':False,'PreviewLimitation':'Clay previews omit vanilla pose contribution, eyes and game materials; they do not validate live handover or sync.'}
(p/'iteration-audit.json').write_text(json.dumps(report,indent=2))
print('0.2.6 evidence gates passed:',len(records),'clips;',len(assets),'unchanged assets')

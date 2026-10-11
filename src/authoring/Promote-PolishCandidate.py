"""Promote the fully fitted 0.2.7 authoring candidate with exact audio times."""
import hashlib,json,re,shutil,struct,wave
from pathlib import Path
import numpy as np
root=Path(__file__).resolve().parents[2];out=root/'output/intro-a2f'
study=root/'research/intro-a2f/correction-0.2.7';prior=study/'baseline-0.2.6';stage=study/'full-detail'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def fnv(b):
    h=14695981039346656037
    for v in b:h=((h^v)*1099511628211)&0xffffffffffffffff
    return h
names=json.loads((stage/'channels.json').read_text());assert len(names)==48
schema=fnv(b''.join(n.encode()+b'\0' for n in names))
scene=json.loads((prior/'scene-manifest.json').read_text());assert len(scene['Lines'])==30
assert sha(out/'scene-manifest.json')==sha(prior/'scene-manifest.json')
clips=[];hashes=[];prepared=[]
for line in scene['Lines']:
    id=line['StringRef'];old=(prior/'clips'/line['Clip']).read_bytes()
    magic,version,c,n,rate,samples,_=struct.unpack_from('<6IQ',old)
    oldframes=np.frombuffer(old[32:],dtype=np.dtype([('t','<u4'),('w','<f4',(c,))]))
    data=np.load(stage/f'VO_{id}_M.npz');times=data['times'];weights=data['weights']
    assert np.array_equal(times,oldframes['t']) and weights.shape==(n,48)
    assert np.isfinite(weights).all() and weights.min()>=0 and weights.max()<=1
    assert np.all(weights[[0,-1]]==0) and times[0]==0 and times[-1]==samples
    with wave.open(str(out/line['Audio'])) as w:assert w.getframerate()==rate==16000 and w.getnframes()==samples
    binary=struct.pack('<6IQ',magic,version,48,n,rate,samples,schema)+b''.join(struct.pack('<I',int(t))+w.astype('<f4').tobytes() for t,w in zip(times,weights))
    h=fnv(binary);hashes.append(h);prepared.append((out/'clips'/line['Clip'],binary))
    raw=study/f'moderate/raw/VO_{id}_M.csv'
    clips.append({'Actor':line['Actor'],'StringRef':id,'Frames':n,'Channels':48,'Duration':samples/rate,
      'MaxWeight':float(weights.max()),'NonzeroChannels':int((weights.max(0)>.001).sum()),
      'SHA256':hashlib.sha256(binary).hexdigest(),'FNV64':f'{h:016x}',
      'SourceCSV_SHA256':sha(raw),'AudioSHA256':sha(out/line['Audio']),'ExactBaselineTimestamps':True})
header=root/'src/asi/single-line-bridge/GeneratedScene.hpp';text=header.read_text()
scene_block=re.search(r'inline constexpr TestLine SceneLines\[\] = \{.*?\n\};',text,re.S).group()
old_block=re.search(r'inline constexpr TestLine SceneLines\[\] = \{.*?\n\};',(prior/'source/GeneratedScene.hpp').read_text(),re.S).group()
assert scene_block==old_block
text=re.sub(r'inline constexpr const wchar_t\* FacialChannels\[\] = \{.*?\n\};',
 'inline constexpr const wchar_t* FacialChannels[] = {\n'+''.join(f'    L"MEF_A2F_{n}",\n' for n in names)+'};',text,flags=re.S)
text=re.sub(r'inline constexpr std::uint64_t FacialSchemaHash = .*?;',f'inline constexpr std::uint64_t FacialSchemaHash = 0x{schema:016x}ull;',text)
text=re.sub(r'inline constexpr std::uint64_t SceneClipHashes\[\] = \{.*?\n\};',
 'inline constexpr std::uint64_t SceneClipHashes[] = {\n'+''.join(f'    0x{h:016x}ull,\n' for h in hashes)+'};',text,flags=re.S)
for path,binary in prepared:path.write_bytes(binary)
for actor in ['Shepard','Joker','Kaidan','Average']:
    assert sha(out/f'meshes/{actor}.json')==sha(prior/f'meshes/{actor}.json')
    for f in stage.glob(f'{actor}*'):shutil.copy2(f,out/'meshes'/f.name)
for f in (study/'moderate/raw').glob('*.csv'):shutil.copy2(f,out/'raw'/f.name)
shutil.copy2(stage/'channels.json',out/'channels.json');header.write_text(text)
(root/'research/intro-a2f/clip-audit.json').write_text(json.dumps({'Model':'Mark diffusion identity2, constant noise, local RTX GPU',
 'EmotionInput':'Audio2Emotion-v2.2 vendor postprocessing, 150ms smoothing, 0.85 gain, total cap0.45',
 'Channels':names,'SchemaFNV64':f'{schema:016x}','Clips':clips,'RuntimeValidated':False},indent=2))
shutil.copy2(stage/'detail-rig-audit.json',root/'research/intro-a2f/retarget-report.json')
print(json.dumps({'Clips':len(clips),'Channels':48,'ExactBaselineAudioTimes':True,'RuntimeValidated':False},indent=2))

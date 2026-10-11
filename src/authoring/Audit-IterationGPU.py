"""Evidence gates for the candidate, including actual serialized morph support."""
import csv,json,hashlib,numpy as np,torch
from pathlib import Path
root=Path(__file__).resolve().parents[2];o=root/'output/intro-a2f'
p=root/'research/intro-a2f/correction-0.2.3';n=json.loads((o/'channels.json').read_text())
assert n[-1]=='correctiveJawLipSeal' and len(n)==38 and torch.cuda.is_available()
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
m=json.loads((o/'scene-manifest.json').read_text())
assert len(m['Lines'])==30 and len({x['StringRef'] for x in m['Lines']})==30
assert {a:sum(x['Actor']==a for x in m['Lines']) for a in ['Joker','Kaidan','Shepard']}=={'Joker':19,'Kaidan':5,'Shepard':6}
j=next(x for x in m['Lines'] if x['StringRef']==101470)
assert j['Actor']=='Joker' and j['Index']==5 and j['Cue']=='nor10_joker_N.VO_101470_M'
old=p/'baseline-0.2.2'
audio=[]
for x in m['Lines']:
    prior=next((z for z in json.loads((old/'clip-audit.json').read_text())['Clips'] if z['StringRef']==x['StringRef']),None)
    if prior:assert sha(o/x['Audio'])==prior['AudioSHA256'];audio.append(x['StringRef'])
reports=[]
for actor in ['Shepard','Joker','Kaidan','Average']:
    mesh=json.loads((o/f'meshes/{actor}.json').read_text(encoding='utf-8-sig'))
    assert mesh==json.loads((old/f'meshes/{actor}.json').read_text(encoding='utf-8-sig'))
    b=torch.tensor(np.load(o/f'meshes/{actor}-lod0-deltas.npy'),device='cuda')
    cosine=[]
    for side in ['Left','Right']:
        a,c=b[n.index('mouthSmile'+side)].flatten(),b[n.index('mouthStretch'+side)].flatten()
        v=float(torch.dot(a,c)/(a.norm()*c.norm()));assert abs(v)<.9
        cosine.append(v)
    jaw=b[n.index('jawOpen')];seal=b[-1]
    assert ((jaw*seal).sum(-1)<=1e-6).all() # opposes jaw motion on lip skin
    nonlip=torch.tensor([not any(('lip' in k.lower() or k.lower()=='mouthbase') and w>0 for k,w in v['Weights'].items()) for v in mesh['LODs'][0]['Vertices']],device='cuda')
    assert seal[nonlip].abs().max()==0 # no corrective on jaw-only dental vertices
    rig=json.loads((o/f'meshes/{actor}-rig.json').read_text())
    empty=[t['Channel'] for t in rig['Targets'] if not t['LODs'][0]['Vertices']]
    assert empty==['cheekSquintLeft','cheekSquintRight'],empty
    reports.append({'Head':actor,'SmileStretchCosines':cosine,'SealOpposesLipJawMotion':True,'NonLipCorrectiveMax':0,'EmptySerializedChannels':empty})
code=(root/'research/single-line/try2/native/.text.mem').read_bytes()
call=0x5908d1;relative=int.from_bytes(code[call-4096+1:call-4096+5],'little',signed=True)
assert code[call-4096]==0xe8 and call+5+relative==0x589ca0
native=json.loads((p/'native-guard.json').read_text())
assert sha(p/'map-rebuild.bin')==native['SHA256']
report={'GPU':torch.cuda.get_device_name(),'Device':'cuda','Clips':30,'Channels':38,'ReusedOriginalAudioVerified':len(audio),'MissingJokerLineCorrected':True,'OriginalMeshExportsUnchanged':True,'Heads':reports,'NativeRebuildCallsiteVerified':True,'RebuildFullBodySHA256':native['SHA256'],'Model':'Mark diffusion identity2, local metadata3.2, default zero emotion, constant noise','RuntimeValidated':False}
(p/'iteration-audit.json').write_text(json.dumps(report,indent=2))
print(json.dumps({k:v for k,v in report.items() if k!='Heads'},indent=2))

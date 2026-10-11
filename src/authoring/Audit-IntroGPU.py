"""Check actual combined performances on GPU, not just isolated morph bounds."""
import json,struct,numpy as np,torch,hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[2];out=root/'output/intro-a2f'
m=json.loads((out/'scene-manifest.json').read_text());reports=[]
assert torch.cuda.is_available()
for actor in ['Shepard','Joker','Kaidan','Average']:
    mesh=json.loads((out/f'meshes/{actor}.json').read_text(encoding='utf-8-sig'))
    basis=torch.tensor(np.load(out/f'meshes/{actor}-lod0-deltas.npy'),device='cuda')
    protected=torch.tensor([any(any(t in k.lower() for t in ['eye','lowlid','blink','neck','chest']) and w>0 for k,w in v['Weights'].items()) for v in mesh['LODs'][0]['Vertices']],device='cuda')
    worst=0;lineReport=[]
    for x in m['Lines']:
        if(actor=='Shepard' and x['Actor']!='Shepard')or(actor in ['Joker','Kaidan'] and x['Actor']!=actor)or(actor=='Average' and x['Actor']=='Shepard'):continue
        b=(out/'clips'/x['Clip']).read_bytes();magic,version,channels,count,rate,samples,schema=struct.unpack_from('<6IQ',b)
        data=np.frombuffer(b[32:],dtype=np.dtype([('t','<u4'),('w','<f4',(channels,))]))
        weights=torch.tensor(data['w'].copy(),device='cuda')
        displacement=(weights@basis.reshape(channels,-1)).reshape(count,-1,3)
        assert torch.isfinite(displacement).all()
        assert displacement[:,protected].abs().max()==0
        maximum=float(torch.linalg.vector_norm(displacement,dim=2).max())
        assert maximum<3.0,(actor,x['StringRef'],maximum)
        worst=max(worst,maximum);lineReport.append({'StringRef':x['StringRef'],'MaxCombinedDisplacement':maximum,'ProtectedMaxDisplacement':0})
    reports.append({'Head':actor,'MaxCombinedDisplacement':worst,'Lines':lineReport})
prior=root/'output/single-line-bridge/release/MEF-SingleLineBridge-0.1.15/Game/ME1/BioGame/CookedPCConsole/MEFFaceShaders.pcc'
current=root/'output/single-line-bridge/assets/MEFFaceShaders.pcc'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(prior)==sha(current)
report={'GPU':torch.cuda.get_device_name(),'NumericalDevice':'cuda','CombinedPerformances':reports,'ProtectedVerticesMaxDelta':0,'SuccessfulShaderPackageUnchanged':True,'ShaderPackageSHA256':sha(current),'RuntimeValidated':False}
(root/'research/intro-a2f/combined-gpu-audit.json').write_text(json.dumps(report,indent=2))
print(json.dumps({k:v for k,v in report.items() if k!='CombinedPerformances'},indent=2));print([(x['Head'],x['MaxCombinedDisplacement']) for x in reports])

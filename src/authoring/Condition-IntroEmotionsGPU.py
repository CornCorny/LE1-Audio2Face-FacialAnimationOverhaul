"""Condition GPU A2E output solely as input to offline A2F facial animation."""
from pathlib import Path
import json,csv,numpy as np,torch,hashlib
root=Path(__file__).resolve().parents[2];study=root/'research/intro-a2f/correction-0.2.6';out=root/'output/intro-a2f'
scene=json.loads((out/'scene-manifest.json').read_text());dest=study/'conditioned-emotions';dest.mkdir(exist_ok=True)
assert torch.cuda.is_available();rows=[];batches=[];preview=[];geometry=[]
for line in scene['Lines']:
 id=line['StringRef'];p=study/f'emotions/VO_{id}_M.csv';data=list(csv.reader(p.open()));names=data[0];a=np.array(data[1:],dtype=np.float64)
 assert a.shape[1]==11 and np.isfinite(a).all() and np.all(np.diff(a[:,0])>0)
 time=torch.tensor(a[:,0],device='cuda',dtype=torch.float32)/16000
 values=torch.tensor(a[:,1:],device='cuda',dtype=torch.float32)
 assert float(values.min())>=0 and float(values.max())<=1
 distance=time[:,None]-time[None,:];kernel=torch.exp(-.5*(distance/.15).square());kernel[distance.abs()>.45]=0;kernel/=kernel.sum(1,keepdim=True)
 conditioned=(kernel@values)*.65
 # Conservative total expression budget; preserve proportions and neutral lines.
 conditioned*=torch.clamp(.35/conditioned.sum(1).clamp_min(1e-6),max=1)[:,None]
 assert torch.isfinite(conditioned).all() and float(conditioned.sum(1).max())<=.35001
 v=conditioned.cpu().numpy();target=dest/p.name
 with target.open('w',newline='') as f:
  w=csv.writer(f);w.writerow(names)
  for t,weights in zip(a[:,0],v):w.writerow([int(t),*weights.tolist()])
 means=v.mean(0);rank=means.argsort()[::-1][:2]
 rows.append({'Actor':line['Actor'],'StringRef':id,'Text':line['Text'],'AnimationConditioningMean':{names[i+1]:float(means[i]) for i in rank},'RawPeak':float(values.max()),'ConditionedPeak':float(conditioned.max()),'GPUInference':True,'ManualOverride':False,'ConditioningSHA256':hashlib.sha256(target.read_bytes()).hexdigest()})
 audio=out/line['Audio'];raw=study/f'raw-conditioned/VO_{id}_M.csv'
 batches.append(f'{audio}\t{raw}\t{target}\n')
 if id in [102169,102170,65217]:
  preview.append(batches[-1]);geometry.append(f'{audio}\t{study}/source-geometry/VO_{id}_M-conditioned.bin\t{target}\n')
(study/'raw-conditioned').mkdir(exist_ok=True)
(study/'a2f-conditioned.tsv').write_text(''.join(batches));(study/'a2f-preview.tsv').write_text(''.join(preview));(study/'geometry-conditioned.tsv').write_text(''.join(geometry))
report={'GPU':torch.cuda.get_device_name(),'Purpose':'Audio2Face fictional-character animation conditioning only','VendorPostprocessingRetained':True,'AdditionalGPUScale':.65,'TotalConditioningCap':.35,'SymmetricSigmaSeconds':.15,'ManualOverrides':0,'Records':rows}
(study/'emotion-conditioning-audit.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))

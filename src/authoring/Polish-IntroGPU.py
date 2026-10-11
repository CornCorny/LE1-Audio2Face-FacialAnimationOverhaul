"""Reversible 0.2.7 authoring experiments using local NVIDIA GPU inference."""
import argparse,csv,hashlib,json,os,shutil,subprocess
from pathlib import Path
import numpy as np
import torch
root=Path(__file__).resolve().parents[2];out=root/'output/intro-a2f'
study=root/'research/intro-a2f/correction-0.2.7';prior=study/'baseline-0.2.6'
p=argparse.ArgumentParser();p.add_argument('stage',choices=['prepare','preview-inference','full-inference']);p.add_argument('--variant',default='moderate');args=p.parse_args()
study.mkdir(exist_ok=True)
scene=json.loads((out/'scene-manifest.json').read_text())
configs={'current':(.65,.35),'moderate':(.85,.45),'stronger':(1.,.55)}
preview_ids=[102169,102170,65217]
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
if args.stage=='prepare':
    assert not prior.exists(),'Baseline already captured; do not overwrite'
    prior.mkdir()
    for name,source in [('meshes',out/'meshes'),('clips',out/'clips'),('source',root/'src/asi/single-line-bridge'),('assets',root/'output/single-line-bridge/assets'),('raw',root/'research/intro-a2f/correction-0.2.6/raw-conditioned')]:
        shutil.copytree(source,prior/name)
    for name in ['channels.json','scene-manifest.json']:shutil.copy2(out/name,prior/name)
    for name in ['clip-audit.json','retarget-report.json','combined-gpu-audit.json','morph-roundtrip.json','preservation-audit.json']:
        shutil.copy2(root/'research/intro-a2f'/name,prior/name)
    records=[{'Path':str(f.relative_to(prior)),'SHA256':sha(f)} for f in prior.rglob('*') if f.is_file()]
    (prior/'SHA256.json').write_text(json.dumps(records,indent=2))
    print('Preserved 0.2.6 baseline:',len(records),'files',flush=True)
    raise SystemExit()

assert torch.cuda.is_available()
variants=configs if args.stage=='preview-inference' else {args.variant:configs[args.variant]}
env=os.environ.copy()
env['PATH']=';'.join([str(root/'output/sdk-build/audio2x-sdk/bin'),str(root/'tools/tensorrt/TensorRT-10.13.3.9/lib'),str(root/'tools/tensorrt/TensorRT-10.13.3.9/bin'),r'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.9\bin',env['PATH']])
exe=root/'output/a2f-export-build/Release/MEFA2FExport.exe'
model=root/'output/sdk-validation/_data/generated/audio2face-sdk/samples/data/multi-diffusion/model.json'
records=[]
for variant,(gain,cap) in variants.items():
    folder=study/variant
    for name in ['emotions','raw','geometry']:(folder/name).mkdir(parents=True,exist_ok=True)
    batches={'diffusion':[],'geometry':[]}
    for line in scene['Lines']:
        id=line['StringRef']
        if args.stage=='preview-inference' and id not in preview_ids:continue
        source=root/f'research/intro-a2f/correction-0.2.6/emotions/VO_{id}_M.csv'
        rows=list(csv.reader(source.open()));a=np.array(rows[1:],dtype=np.float64)
        time=torch.tensor(a[:,0],device='cuda',dtype=torch.float32)/16000
        values=torch.tensor(a[:,1:],device='cuda',dtype=torch.float32)
        distance=time[:,None]-time[None,:]
        kernel=torch.exp(-.5*(distance/.15).square());kernel[distance.abs()>.45]=0;kernel/=kernel.sum(1,keepdim=True)
        conditioned=(kernel@values)*gain
        conditioned*=torch.clamp(cap/conditioned.sum(1).clamp_min(1e-6),max=1)[:,None]
        assert torch.isfinite(conditioned).all() and float(conditioned.sum(1).max())<=cap+.00001
        target=folder/f'emotions/VO_{id}_M.csv'
        with target.open('w',newline='') as f:
            writer=csv.writer(f);writer.writerow(rows[0]);writer.writerows([[int(tm),*v] for tm,v in zip(a[:,0],conditioned.cpu().tolist())])
        audio=out/line['Audio']
        batches['diffusion'].append(f'{audio}\t{folder}/raw/VO_{id}_M.csv\t{target}\n')
        batches['geometry'].append(f'{audio}\t{folder}/geometry/VO_{id}_M.bin\t{target}\n')
        records.append({'Variant':variant,'StringRef':id,'Scale':gain,'TotalCap':cap,'ManualOverride':False,'AudioSHA256':sha(audio),'EmotionSHA256':sha(target)})
    for mode,batch in batches.items():
        path=folder/f'{mode}.tsv';path.write_text(''.join(batch))
        with (folder/f'{mode}.log').open('w') as log:
            subprocess.run([str(exe),str(model),str(path),mode],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
        print(variant,mode,'complete:',len(batch),'lines',flush=True)
(study/f'{args.stage}.json').write_text(json.dumps({'GPU':torch.cuda.get_device_name(),'Records':records,'ManualEmotionOverrides':0},indent=2))

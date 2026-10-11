from pathlib import Path
import json,subprocess,os,shutil,hashlib
root=Path(__file__).resolve().parents[1];source=root/'models/Audio2Emotion-v2.2'
dest=root/'output/sdk-validation/_data/generated/audio2emotion-sdk/samples/model';dest.mkdir(parents=True,exist_ok=True)
study=root/'research/intro-a2f/correction-0.2.6'
assert (source/'network.onnx').stat().st_size>1000000000
for n in ['model.json','model_config.json','network_info.json']:shutil.copy2(source/n,dest/n)
args=[str(root/'tools/tensorrt/TensorRT-10.13.3.9/bin/trtexec.exe'),'--onnx='+str(source/'network.onnx'),'--saveEngine='+str(dest/'network.trt'),'--minShapes=input_values:1x5000','--optShapes=input_values:1x60000','--maxShapes=input_values:1x60000','--device=0','--skipInference']
with (study/'a2e-engine.log').open('w') as f:process=subprocess.run(args,stdout=f,stderr=subprocess.STDOUT)
report={'Model':'nvidia/Audio2Emotion-v2.2','Purpose':'offline Audio2Face animation conditioning only','EngineExitCode':process.returncode,'BatchProfile':1,'SourceFiles':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in source.iterdir() if p.is_file()}}
(study/'a2e-model-validation.json').write_text(json.dumps(report,indent=2))
print('A2E TensorRT engine exit:',process.returncode);raise SystemExit(process.returncode)

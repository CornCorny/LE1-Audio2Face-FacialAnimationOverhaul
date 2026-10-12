"""Validate GPU curves, create bounded portable clips and generate runtime IDs."""
import csv,json,struct,wave,hashlib
from pathlib import Path
import numpy as np,torch
root=Path(__file__).resolve().parents[2];out=root/'output/intro-a2f'
m=json.loads((out/'scene-manifest.json').read_text(encoding='utf-8-sig'))
channels=json.loads((out/'channels.json').read_text());basechannels=[c for c in channels if not c.startswith('corrective')];countchannels=len(channels)
assert len(basechannels)==37 and channels==basechannels+['correctiveJawLipSeal'] and torch.cuda.is_available()
def fnv(b):
    h=14695981039346656037
    for v in b:h=((h^v)*1099511628211)&0xffffffffffffffff
    return h
schema=fnv(b''.join(k.encode()+b'\0' for k in channels))
clips=out/'clips';clips.mkdir(exist_ok=True);lines=[];hashes=[];report=[]
for x in m['Lines']:
    with wave.open(str(out/x['Audio'])) as wav:samples=wav.getnframes();assert wav.getframerate()==16000
    x['Samples']=samples;x['Duration']=samples/16000
    path=out/'raw'/f"VO_{x['StringRef']}_M.csv"
    raw=list(csv.reader(path.open()));names=raw[0][1:];values=np.array(raw[1:],dtype=np.float64)
    assert values.shape[1]==53 and np.isfinite(values).all()
    times=values[:,0].astype(np.int64);assert (np.diff(times)>0).all()
    weights=values[:,[1+names.index(k) for k in basechannels]]
    assert weights.min()>=-.0001 and weights.max()<=1.0001
    weights=np.clip(weights,0,1).astype(np.float32)
    # Three-tap zero-phase smoothing; audio timestamps remain untouched.
    if len(weights)>2:weights[1:-1]=.2*weights[:-2]+.6*weights[1:-1]+.2*weights[2:]
    keep=(times>=0)&(times<samples);times=times[keep];weights=weights[keep]
    assert len(times)>1
    # Brief entry/exit fade avoids a step on line changes/skips. Explicit final
    # neutral frame guarantees deterministic endpoint even at low frame rates.
    fade=np.minimum(np.clip(times/640,0,1),np.clip((samples-times)/960,0,1))
    weights*=fade[:,None]
    if times[0]>0:times=np.r_[0,times];weights=np.vstack([np.zeros((1,37),np.float32),weights])
    times=np.r_[times,samples];weights=np.vstack([weights,np.zeros((1,37),np.float32)])
    # GPU authors the nonlinear coupling into an ordinary precomputed channel.
    # The player performs only the same linear morph evaluation as before.
    gpu=torch.tensor(weights,device='cuda');seal=gpu[:,basechannels.index('jawOpen')]*gpu[:,basechannels.index('mouthClose')]
    weights=torch.cat([gpu,seal[:,None]],1).cpu().numpy()
    header=struct.pack('<6IQ',0x4346454d,1,countchannels,len(times),16000,samples,schema)
    data=header+b''.join(struct.pack('<I',int(t))+w.astype('<f4').tobytes() for t,w in zip(times,weights))
    dest=clips/x['Clip'];dest.write_bytes(data);h=fnv(data);hashes.append(h)
    pawn=x['SpeakerTag'];lines.append(f'    {{L"{x["Actor"]}", {x["Index"]}, {x["StringRef"]}, {x["ExportId"]}, L"{x["Cue"]}", L"{pawn}", {x["Duration"]:.7f}f}},')
    report.append({'Actor':x['Actor'],'StringRef':x['StringRef'],'Frames':len(times),'Channels':countchannels,'Duration':x['Duration'],'MaxWeight':float(weights.max()),'NonzeroChannels':int((weights.max(0)>.001).sum()),'SHA256':hashlib.sha256(data).hexdigest(),'FNV64':f'{h:016x}','SourceCSV_SHA256':hashlib.sha256(path.read_bytes()).hexdigest(),'AudioSHA256':hashlib.sha256((out/x['Audio']).read_bytes()).hexdigest()})
text='#pragma once\n// Generated from exact opening-scene graph and local GPU outputs.\n'
text+='inline constexpr TestLine SceneLines[] = {\n'+'\n'.join(lines)+'\n};\n'
text+='inline constexpr const wchar_t* FacialChannels[] = {\n'+''.join(f'    L"MEF_A2F_{k}",\n' for k in channels)+'};\n'
text+=f'inline constexpr std::uint64_t FacialSchemaHash = 0x{schema:016x}ull;\n'
text+='inline constexpr std::uint64_t SceneClipHashes[] = {\n'+''.join(f'    0x{h:016x}ull,\n' for h in hashes)+'};\n'
(root/'src/asi/single-line-bridge/GeneratedScene.hpp').write_text(text)
(out/'scene-manifest.json').write_text(json.dumps(m,indent=2))
(root/'research/intro-a2f/clip-audit.json').write_text(json.dumps({'Model':'Mark diffusion, identity2; local multi model network metadata version3.2','ModelDescriptor':'output/sdk-validation/_data/generated/audio2face-sdk/samples/data/multi-diffusion/model.json','ConstantNoise':True,'EmotionInput':'default zero; no fabricated emotion or A2E pass','GPUInferenceAndSolver':True,'CorrectiveDevice':torch.cuda.get_device_name(),'Channels':channels,'SchemaFNV64':f'{schema:016x}','Clips':report,'RuntimeValidated':False},indent=2))
print(json.dumps({'Clips':len(report),'Channels':countchannels,'TotalFrames':sum(x['Frames'] for x in report),'ShepardVariants':sum(x['Actor']=='Shepard' for x in report),'RuntimeValidated':False},indent=2))

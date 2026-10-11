"""Preserve the successful rendering/native guards while adding clip playback."""
from pathlib import Path
import hashlib,json,re,sys
root=Path(__file__).resolve().parents[2]
prior=root/'output/single-line-bridge/release/MEF-SingleLineBridge-0.1.15'
source=root/'src/asi/single-line-bridge';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
old=(prior/'source/Bridge.cpp').read_text();new=(source/'Bridge.cpp').read_text()
def function(s,name):
    m=re.search(r'^[^\n]*\b'+name+r'\([^;\n]*\)\s*\{',s,re.M);assert m,name
    a=s.index('{',m.start());depth=1;i=a+1
    while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
    return s[m.start():i]
protected=['PrepareMaterials','BaseMaterial','MorphUsageChain','CreateHeadTree','MatchingAudio','SupportedExe','TraceComponentUsage','TraceMaterialCopy','TraceBaseUsage','TraceInstanceUsage','DiagnosticTick','Construct','IsLiveInstance']
for name in protected:assert function(old,name)==function(new,name),name
headers=['NativeMorph.hpp','RenderTrace.hpp','RenderTraceNative.hpp','ResourceTrace.hpp','ResourceTraceNative.hpp','ResourceTraceBridge.inl','MaterialProbe.hpp']
for name in headers:assert sha(prior/'source'/name)==sha(source/name),name
for name in ['Invoke','World','LoadRig','Playing','Init','SetTree','Material','LoadFaceTemplate','MaterialParent','CopyFaceParameters','Weight']:
    assert function((prior/'source/EngineCalls.hpp').read_text(),name)==function((source/'EngineCalls.hpp').read_text(),name),name
shader=root/'output/single-line-bridge/assets/MEFFaceShaders.pcc'
assert sha(shader)==sha(prior/'Game/ME1/BioGame/CookedPCConsole/MEFFaceShaders.pcc')
manifest=json.loads((root/'output/intro-a2f/scene-manifest.json').read_text())
import os
game=Path(os.environ['LE_INSTALL_DIR'])/'Game/ME1/BioGame'
assert sha(game/'CookedPCConsole/BIOA_NOR10_01_DSG_LOC_INT.pcc').upper()==manifest['SourcePackageSHA256']
assert sha(game/'Content/Packages/ISACT/nor10_joker.isb').upper()==manifest['AudioBankSHA256']
sys.path.insert(0,str(root/'tools/native-analysis/python-libs'));import pefile
pe=pefile.PE(str(root/'output/bridge-build/Release/MEFSingleLineBridge.asi'))
imports=[e.dll.decode() for e in pe.DIRECTORY_ENTRY_IMPORT]
assert not any(any(x in dll.lower() for x in ['cuda','nvinfer','torch','python','audio2x']) for dll in imports)
model=root/'output/sdk-validation/_data/generated/audio2face-sdk/samples/data/multi-diffusion'
provenance={p.name:sha(p) for p in model.iterdir() if p.is_file()}
report={'ProtectedFunctionsUnchanged':protected,'ProtectedHeadersUnchanged':headers,'NativeAPIsPreserved':True,'ShaderPackageUnchanged':True,'ShaderPackageSHA256':sha(shader),'OriginalDialoguePackageAndAudioUnchanged':True,'PlayerASIImports':imports,'PlayerRequiresAIStack':False,'AuthoringModel':'Mark diffusion identity2; local network metadata3.2; Audio2Emotion-v2.2 with bounded GPU conditioning; constant noise; GPU solver','AuthoringModelFilesSHA256':provenance,'RuntimeValidated':False}
(root/'research/intro-a2f/preservation-audit.json').write_text(json.dumps(report,indent=2))
print(json.dumps({k:v for k,v in report.items() if k!='AuthoringModelFilesSHA256'},indent=2))

"""Check that a binding compatibility change preserves the accepted bridge."""
import json,re,hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[2];p=root/'research/intro-a2f/correction-0.2.8'
source=root/'src/asi/single-line-bridge';old=p/'baseline-0.2.7/runtime'
release=root/'output/intro-a2f/release/MEFaces-IntroA2F-0.2.7'
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
def function(text,name):
    match=re.search(r'^[^\n]*\b'+name+r'\([^;\n]*\)\s*\{',text,re.M);assert match,name
    i=match.end();depth=1
    while depth:
        if text[i]=='{':depth+=1
        elif text[i]=='}':depth-=1
        i+=1
    return text[match.start():i]
before=(old/'Bridge.cpp').read_text();after=(source/'Bridge.cpp').read_text()
preserved=['Pose','Bind','Tick','Release','OwnershipBound','RefreshOwnershipMap','PrepareOwnershipRoute',
           'OwnershipFault','FaceUpdate','FaceRebuild','CreateHeadTree','MatchingAudio','RenderState',
           'TraceComponentUsage','TraceInstanceUsage','TraceMaterialCopy','TraceGetMaterialResource']
# Some native trace symbols have different generated names; use only exact
# definitions present in the accepted source, and record every checked name.
preserved=[n for n in preserved if re.search(r'^[^\n]*\b'+n+r'\([^;\n]*\)\s*\{',before,re.M)]
for name in preserved:assert function(before,name)==function(after,name),name
allowed={'Bridge.cpp','LineBinding.hpp','GeneratedScene.hpp','CMakeLists.txt','BindingTests.cpp','FacialClipTests.cpp'}
unchanged=[];changed=[]
for f in old.iterdir():
    if sha(f)==sha(source/f.name):unchanged.append(f.name)
    else:assert f.name in allowed,f.name;changed.append(f.name)
assets=[]
for file in (release/'Game/ME1/BioGame/CookedPCConsole').glob('*.pcc'):
    assert sha(file)==sha(root/'output/single-line-bridge/assets'/file.name),file.name
    assets.append(file.name)
clips=[]
for file in (release/'Game/ME1/Binaries/Win64/ASI/MEFIntroClips').glob('*.mefc'):
    assert sha(file)==sha(root/'output/intro-a2f/clips'/file.name),file.name
    clips.append(file.name)
assert len(clips)==30 and len(assets)==5
scene=json.loads((root/'output/intro-a2f/scene-manifest.json').read_text());prior=json.loads((p/'baseline-0.2.7/scene-manifest.json').read_text())
for line in scene['Lines']:line.pop('SpeakerTag')
assert scene==prior,'Only participant metadata may change; dialogue/audio IDs and timing must stay exact'
inventory=json.loads((p/'head-inventory.json').read_text(encoding='utf-8-sig'))
average=[x for x in inventory if x['Mesh'].endswith('Average.HMM_HED_PROAverage_MDL')]
assert len(average)==3 and len({x['GeometrySHA256'] for x in average})==1
newheads=[x for x in inventory if x['Mesh'].endswith('Kaidan.HMM_HED_PROKaidan_MDL')]
assert len(newheads)==3 and len({x['GeometrySHA256'] for x in newheads})==1
assert all(x['LODVertices']==[2266] and len(x['Bones'])==42 for x in newheads)
assert 'GetFullPath().Equals(test.' not in function(after,'Update')
assert 'BaseSkelMesh != head->SkeletalMesh' in function(after,'Bind')
assert 'set->BaseSkelMesh' in function(after,'ResolveRig')
report={'Revision':'0.2.8','PreservedRuntimeFunctions':preserved,'UnchangedRuntimeFiles':unchanged,'ChangedRuntimeFiles':changed,
        'AcceptedAssetsByteIdentical':assets,'AcceptedClipsByteIdentical':clips,
        'SceneAudioIDsAndTimingUnchanged':True,'PackageIndependentParticipants':True,'RuntimeRigBasePointerRequired':True,
        'AverageGeometryIdenticalAcrossVanillaAndPatch':True,'NewHeadGeometryIdenticalAcrossThreePatchedPackages':True,
        'RuntimeValidated':False,'Limitations':['Loaded package replacement with a different geometry but identical object identity needs a future install-time compatibility scan','Live participant/routing/material behaviour still requires the user recording']}
(p/'iteration-audit.json').write_text(json.dumps(report,indent=2))
print(json.dumps({k:v for k,v in report.items() if k not in ['AcceptedClipsByteIdentical','UnchangedRuntimeFiles']},indent=2))

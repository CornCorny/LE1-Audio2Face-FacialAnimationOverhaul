"""Evidence gates specific to the Sync1 correction; no game mutation."""
import json,hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[2]
old=root/'output/intro-a2f/release/MEFaces-IntroA2F-0.2.0'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
unchanged=[]
for f in (root/'output/intro-a2f/clips').glob('*.mefc'):
    assert sha(f)==sha(old/'Game/ME1/Binaries/Win64/ASI/MEFIntroClips'/f.name)
    unchanged.append(f.name)
assert len(unchanged)==29
assert sha(root/'src/asi/single-line-bridge/GeneratedScene.hpp')==sha(old/'source/runtime/GeneratedScene.hpp')
native=root/'research/intro-a2f/ownership/native'
evidence=json.loads((native/'live-code-verification.json').read_text())
policy=json.loads((root/'research/intro-a2f/ownership/policy.json').read_text())
assert evidence['Checks'][0]['SHA256']==policy['CodeSHA256']==sha(native/'0x590670.bin')
assert all(x['MatchesHistoricalCapture'] for x in evidence['Checks'])
assert len(policy['OwnedBones'])==23
assert not any(any(s in n.lower() for s in ['eye','lid','blink','head','neck','tongue']) for n in policy['OwnedBones'])
render=json.loads((root/'research/intro-a2f/correction-0.2.1/previews/render.json').read_text())
assert render['Device']=='GPU OptiX' and any('5070' in x for x in render['GPUs'])
rig=json.loads((root/'research/intro-a2f/retarget-report.json').read_text())
assert rig['NumericalDevice']=='cuda'
assert all(x['ProtectedMaxDelta']==0 for h in rig['Heads'] for x in h['LODs'])
roundtrip=json.loads((root/'research/intro-a2f/morph-roundtrip.json').read_text(encoding='utf-8-sig'))
assert len(roundtrip)==4 and all(x['BinaryRoundTripIdentical'] and x['BoneOffsets']==0 for x in roundtrip)
report={'UnchangedClips':unchanged,'SceneIdentifiersAndClipHashesUnchanged':True,'OwnershipNativeFullBodySHA256':policy['CodeSHA256'],'LiveReadOnlyCodeVerified':True,'OwnedBoneCount':23,'ProtectedBonePolicyVerified':True,'CUDAFitAndOptiXRenderVerified':True,'MorphPackagesRoundTripVerified':True,'RuntimeValidated':False}
(root/'research/intro-a2f/correction-0.2.1/correction-audit.json').write_text(json.dumps(report,indent=2))
print(json.dumps({k:v for k,v in report.items() if k!='UnchangedClips'},indent=2))

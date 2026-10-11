"""Generate exact map gates from the inspected stock assets, never guessed indices."""
import json, hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[2]
out=root/'src/asi/single-line-bridge'
graphs=[json.loads((root/f'research/intro-a2f/ownership/{a}-facegraph.json').read_text(encoding='utf-8-sig')) for a in ['Shepard','Joker','Kaidan','Average']]
owned=lambda n: n.lower() in {'mouthbase','jawbone','sneer'} or any(x in n.lower() for x in ['lip','cheek','brow'])
bones=[b['Name'] for b in graphs[0]['Bones']]
assert len(bones)==116 and all([b['Name'] for b in g['Bones']]==bones for g in graphs)
mask=[owned(n) for n in bones]
assert not any(mask[i] for i,n in enumerate(bones) if any(x in n.lower() for x in ['eye','lid','blink','head','neck','tongue']))
text='#pragma once\n#include <array>\nnamespace FaceOwnership {\n'
text+='inline constexpr size_t BoneCount=116;\n'
text+='inline constexpr std::array<const wchar_t*,BoneCount> BoneNames={'+','.join('L"'+x+'"' for x in bones)+'};\n'
text+='inline constexpr std::array<bool,BoneCount> Owned={'+','.join('true' if x else 'false' for x in mask)+'};\n'
for g in graphs:
    names={b['Name']['Name'].lower():i for i,b in enumerate(g['Skeleton'])}
    for b in g['Skeleton']:
        if any(x in b['Name']['Name'].lower() for x in ['eye','lid','blink']):
            i=names[b['Name']['Name'].lower()]
            for _ in range(len(g['Skeleton'])):
                assert not owned(g['Skeleton'][i]['Name']['Name'])
                if i==0:break
                i=g['Skeleton'][i]['ParentIndex']
    values=[names.get(n.lower(),255) for n in bones]
    assert max((x for x in values if x!=255))<255
    text+=f'inline constexpr std::array<unsigned char,BoneCount> {g["Actor"]}Map={{'+','.join(map(str,values))+'};\n'
text+='}\n'
(out/'GeneratedFaceOwnership.hpp').write_text(text)
code=(root/'research/intro-a2f/ownership/native/0x590670.bin').read_bytes()
assert len(code)==0x591ef2-0x590670
(out/'FaceOwnershipNative.hpp').write_text('#pragma once\n#include <array>\n#include <cstring>\n#include <cstdint>\nnamespace FaceOwnershipNative {\ninline constexpr std::uintptr_t RVA=0x590670;\ninline constexpr std::array<unsigned char,'+str(len(code))+'> Bytes={'+','.join('0x%02x'%x for x in code)+'};\ninline bool Matches(const void* p){return p && !std::memcmp(p,Bytes.data(),Bytes.size());}\n}\n')
(root/'research/intro-a2f/ownership/policy.json').write_text(json.dumps({'OwnedBones':[n for n,x in zip(bones,mask) if x],'ProtectedBones':[n for n,x in zip(bones,mask) if not x],'CodeSHA256':hashlib.sha256(code).hexdigest(),'BoneMapSkipValue':255,'RuntimeValidated':False},indent=2))
print('Generated policy:',sum(mask),'owned bones;',len(code),'verified native bytes')

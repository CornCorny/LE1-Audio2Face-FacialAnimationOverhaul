#pragma once
#include <LESDK/Headers.hpp>

// The generated SDK wrappers OR function flags with ~0x400. Avoid that global
// metadata mutation: dispatch the same reflected native APIs with zeroed parms
// and leave their original flags untouched. Missing functions fail closed.
namespace EngineCalls {
template<class P> bool Invoke(UObject* object, const wchar_t* name, P& parms) {
    static UFunction* fn = nullptr; // Each P below identifies exactly one function.
    if (!fn) fn = UObject::FindObject<UFunction>(name);
    if (!object || !fn || !(fn->FunctionFlags & 0x400)) return false;
    object->ProcessEvent(fn, &parms, nullptr);
    return true;
}
inline AWorldInfo* World(UGameEngine* engine) {
    UEngine_execGetCurrentWorldInfo_Parms p{};
    Invoke(engine, L"Function Engine.Engine.GetCurrentWorldInfo", p);
    return p.ReturnValue;
}
inline UObject* LoadRig(UObject* context, const wchar_t* path) {
    UObject_execDynamicLoadObject_Parms p{};
    p.ObjectName = FString(path);
    p.ObjectClass = UMorphTargetSet::StaticClass(); p.MayFail = true;
    if (!p.ObjectClass) return nullptr;
    Invoke(context, L"Function Core.Object.DynamicLoadObject", p);
    return p.ReturnValue;
}
inline bool Playing(UAudioComponent* audio) {
    UAudioComponent_execBioIsPlaying_Parms p{};
    return Invoke(audio, L"Function Engine.AudioComponent.BioIsPlaying", p) && p.ReturnValue;
}
inline bool Init(USkeletalMeshComponent* head) {
    USkeletalMeshComponent_execInitMorphTargets_Parms p{};
    return Invoke(head, L"Function Engine.SkeletalMeshComponent.InitMorphTargets", p);
}
// Read-only normal UE3 APIs: resolve the actual evaluation component's bones,
// including body/parent skeletons that differ from the visible head skeleton.
inline bool MatchBone(USkeletalMeshComponent* component,const wchar_t* name,int& index) {
    USkeletalMeshComponent_execMatchRefBone_Parms p{};
    p.BoneName=SFXName(name,0);p.ReturnValue=-1;
    if(!Invoke(component,L"Function Engine.SkeletalMeshComponent.MatchRefBone",p))return false;
    index=p.ReturnValue;return true;
}
inline bool BoneName(USkeletalMeshComponent* component,int index,SFXName& name) {
    USkeletalMeshComponent_execGetBoneName_Parms p{};p.BoneIndex=index;
    if(!Invoke(component,L"Function Engine.SkeletalMeshComponent.GetBoneName",p))return false;
    name=p.ReturnValue;return true;
}
inline bool SetTree(USkeletalMeshComponent* head, UAnimTree* tree) {
    USkeletalMeshComponent_execSetAnimTreeTemplate_Parms p{};
    p.NewTemplate = tree;
    return Invoke(head, L"Function Engine.SkeletalMeshComponent.SetAnimTreeTemplate", p);
}
inline UMaterialInterface* Material(USkeletalMeshComponent* head, int slot) {
    UMeshComponent_execGetMaterial_Parms p{};
    p.ElementIndex = slot;
    Invoke(head, L"Function Engine.MeshComponent.GetMaterial", p);
    return p.ReturnValue;
}
inline UObject* LoadFaceTemplate(UObject* context, bool joker) {
    // Shares only DynamicLoadObject's function cache with LoadRig.
    UObject_execDynamicLoadObject_Parms p{};
    p.ObjectName = FString(joker ? L"MEFFaceShaders.JokerFaceTemplate" : L"MEFFaceShaders.NPCFaceTemplate");
    p.ObjectClass = joker ? UBioMaterialInstanceConstant::StaticClass() : UMaterialInstanceConstant::StaticClass(); p.MayFail = true;
    if (!p.ObjectClass) return nullptr;
    Invoke(context, L"Function Core.Object.DynamicLoadObject", p);
    return p.ReturnValue;
}
inline bool MaterialParent(UMaterialInstanceConstant* instance, UMaterialInterface* parent) {
    UMaterialInstanceConstant_execSetParent_Parms p{};
    p.NewParent = parent;
    return Invoke(instance, L"Function Engine.MaterialInstanceConstant.SetParent", p) && instance->Parent == parent;
}
inline bool CopyFaceParameters(UMaterialInstanceConstant* from, UMaterialInstanceConstant* to) {
    for (auto const& v : from->FontParameterValues) {
        UMaterialInstanceConstant_execSetFontParameterValue_Parms p{};
        p.ParameterName=v.ParameterName; p.FontValue=v.FontValue; p.FontPage=v.FontPage;
        if (!Invoke(to,L"Function Engine.MaterialInstanceConstant.SetFontParameterValue",p)) return false;
    }
    for (auto const& v : from->VectorParameterValues) {
        UMaterialInstanceConstant_execSetVectorParameterValue_Parms p{};
        p.ParameterName=v.ParameterName; p.Value=v.ParameterValue;
        if (!Invoke(to,L"Function Engine.MaterialInstanceConstant.SetVectorParameterValue",p)) return false;
    }
    for (auto const& v : from->ScalarParameterValues) {
        UMaterialInstanceConstant_execSetScalarParameterValue_Parms p{};
        p.ParameterName=v.ParameterName; p.Value=v.ParameterValue;
        if (!Invoke(to,L"Function Engine.MaterialInstanceConstant.SetScalarParameterValue",p)) return false;
    }
    for (auto const& v : from->TextureParameterValues) {
        UMaterialInstanceConstant_execSetTextureParameterValue_Parms p{};
        p.ParameterName=v.ParameterName; p.Value=v.ParameterValue;
        if (!Invoke(to,L"Function Engine.MaterialInstanceConstant.SetTextureParameterValue",p)) return false;
    }
    return true;
}
inline UMorphTarget* Find(UMorphTargetSet* set, const wchar_t* name=L"MEF_SL_CornerPull") {
    UMorphTargetSet_execFindMorphTarget_Parms p{};
    p.MorphTargetName = SFXName(name, 0);
    Invoke(set, L"Function Engine.MorphTargetSet.FindMorphTarget", p);
    return p.ReturnValue;
}
inline bool Add(UMorphNodeMultiPose* node, const wchar_t* name=L"MEF_SL_CornerPull") {
    UMorphNodeMultiPose_execAddMorphTarget_Parms p{};
    p.MorphTargetName = SFXName(name, 0); p.InWeight = 0;
    return Invoke(node, L"Function Engine.MorphNodeMultiPose.AddMorphTarget", p) && p.ReturnValue;
}
inline bool Weight(UMorphNodeMultiPose* node, UMorphTarget* target, float weight) {
    UMorphNodeMultiPose_execUpdateMorphTarget_Parms p{};
    p.Target = target; p.InWeight = weight;
    return Invoke(node, L"Function Engine.MorphNodeMultiPose.UpdateMorphTarget", p) && p.ReturnValue;
}
}

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>
#include <SPI.h>
#include <bcrypt.h>
#include <cstdio>
#include <cstdarg>
#include <share.h>
#include <filesystem>
#include <array>
#include <cmath>
#include <atomic>
#include <optional>
#include <intrin.h>
#include "Ramp.hpp"
#include "EngineCalls.hpp"
#include "LineBinding.hpp"
#include "GeneratedScene.hpp"
#include "FacialClip.hpp"
#include "NativeMorph.hpp"
#include "MaterialProbe.hpp"
#include "RenderTrace.hpp"
#include "RenderTraceNative.hpp"
#include "ResourceTrace.hpp"
#include "ResourceTraceNative.hpp"
#include "FaceOwnership.hpp"
#include "FaceOwnershipNative.hpp"
#include "FaceRebuildNative.hpp"
#include "FaceRouting.hpp"
#include "FaceTransition.hpp"
#include "FaceParameters.hpp"

static wchar_t pluginName[] = L"MEF Single Line Bridge";
static wchar_t pluginAuthor[] = L"MEFaces";
static wchar_t pluginVersion[] = L"0.2.8";
SPI_PLUGINSIDE_SUPPORT(pluginName, pluginAuthor, pluginVersion, SPI_GAME_LE1, SPI_VERSION_ANY);
SPI_PLUGINSIDE_POSTLOAD;
SPI_PLUGINSIDE_SEQATTACH;

namespace {
using TickFn = void(UGameEngine*, float);
using PoseFn = void(USkeletalMeshComponent*, float, int, int);
using EvaluateFn = void(UMorphNodeMultiPose*, TArray<FActiveMorph>*);
PoseFn* originalPose = nullptr;
using FaceUpdateFn=void(USkeletalMeshComponent*,TArray<FBoneAtom>*,float,int);
FaceUpdateFn* originalFaceUpdate=nullptr;
using FaceRebuildFn=void(USkeletalMeshComponent*);
FaceRebuildFn* originalFaceRebuild=nullptr;
thread_local bool insideFaceUpdate=false;
unsigned long long ownershipCalls=0,ownershipRestores=0,ownershipRefusals=0;
unsigned long long ownershipRebuilds=0;
std::uint64_t ownershipInputHash=0;
unsigned long long ownershipInputChanges=0;
unsigned long long poseWithheld=0;
unsigned long long transitionCalls=0,transitionRefusals=0;
float transitionAlpha=0;
float transitionBrowAlpha=0;
bool activationReported=false,activationFailureReported=false;
unsigned untrackedFaceSources=0;
FaceRouting::Coverage ownershipCoverage;
static_assert(offsetof(USkeletalMeshComponent,LocalAtoms)==0x2c0 &&
    offsetof(USkeletalMeshComponent,FaceFXActorInstance)==0x670 &&
    offsetof(USkeletalMeshComponent,m_aBioFFXBoneMap)==0x77c && sizeof(FBoneAtom)==32 &&
    offsetof(FBoneAtom,Rotation)==0 && offsetof(FBoneAtom,Translation)==16 && offsetof(FBoneAtom,Scale)==28);
EvaluateFn* evaluateMorph = nullptr;
std::atomic<DWORD> gameThread{0};
bool releasing = false;
unsigned long long poseCallbacks = 0, poseApplications = 0;
static_assert(sizeof(FActiveMorph) == 12 && offsetof(FActiveMorph, Weight) == 8);
using ConstructFn = UObject*(UClass*, UObject*, SFXName, EObjectFlags, UObject*, void*, UObject*, void*);
TickFn* originalTick = nullptr;
ConstructFn* constructObject = nullptr;
void** errorDevice = nullptr;
FILE* logFile = nullptr;
bool disabled = false, f8Down = false, busy = false, restorationFault = false;
unsigned long long frame = 0;

void Log(const wchar_t* format, ...) {
    if (!logFile) return;
    fwprintf(logFile, L"[%llu] ", frame);
    va_list args; va_start(args, format); vfwprintf(logFile, format, args); va_end(args);
    fputwc(L'\n', logFile); fflush(logFile);
}

struct Ref {
    UObject* pointer = nullptr;
    int index = -1;
    void Set(UObject* p) { pointer = p; index = p ? p->ObjectInternalInteger : -1; }
    bool Alive() const {
        if (!pointer || !UObject::GObjObjects || index < 0 ||
            static_cast<unsigned>(index) >= UObject::GObjObjects->Count() ||
            (*UObject::GObjObjects)(index) != pointer) return false;
        constexpr auto invalid = static_cast<uint64_t>(EObjectFlags::PendingKill) |
            static_cast<uint64_t>(EObjectFlags::Unreachable) |
            static_cast<uint64_t>(EObjectFlags::BeginDestroyed) |
            static_cast<uint64_t>(EObjectFlags::FinishDestroyed);
        return (static_cast<uint64_t>(pointer->ObjectFlags) & invalid) == 0;
    }
    template<class T> T* As() const { return Alive() ? static_cast<T*>(pointer) : nullptr; }
};
Ref headRef, treeRef, nodeRef, setRef, targetRef, audioRef;
constexpr unsigned facialChannelCount=static_cast<unsigned>(std::size(FacialChannels));
std::array<Ref,facialChannelCount> facialTargetRefs;
std::array<float,facialChannelCount> facialWeights{};
std::array<FacialClip::Clip,std::size(SceneLines)> facialClips;
const FacialClip::Clip* activeClip=nullptr;
Ref ownedTemplateRef, parentRef;
struct OwnershipRoute {
    Ref component,mesh,asset;
    std::array<unsigned char,FaceOwnership::BoneCount> skeleton{};
    std::array<unsigned char,FaceOwnership::BoneCount> expected{};
    bool prepared=false,failed=false;
    unsigned long long calls=0;
};
std::array<OwnershipRoute,2> ownershipRoutes;
struct OwnershipScope {
    USkeletalMeshComponent* component;
    TArray<FBoneAtom>* atoms;
    OwnershipRoute* route;
    std::uintptr_t instance,actor;
    FaceOwnership::Lifecycle lifecycle;
};
thread_local OwnershipScope* currentOwnershipScope=nullptr;
bool addedSet = false;
float requestedWeight = 0, lastAudioTime = -1;
bool attempted = false, previouslyEligible = false;
const TestLine* activeLine = nullptr;
bool IsLiveInstance(UObject* object);
UObject* Construct(UClass* cls, UObject* outer, const wchar_t* name);
struct PreparedFace { Ref head, source, displayed, bridge, previousParent; };
std::array<PreparedFace,2> preparedFaces;
using ComponentUsageFn=void(USkeletalMeshComponent*);
using InstanceUsageFn=int(UMaterialInterface*,int);
using BaseUsageFn=int(UMaterial*,int*,int);
using MaterialCopyFn=void(void*,const void*);
ComponentUsageFn* originalComponentUsage=nullptr;
InstanceUsageFn* originalInstanceUsage=nullptr;
BaseUsageFn* originalBaseUsage=nullptr;
MaterialCopyFn* originalMaterialCopy=nullptr;
thread_local RenderTrace::CaptureData* currentRenderTrace=nullptr;
std::array<RenderTrace::Budget,2> renderTraceBudgets;
std::uintptr_t moduleBase=0;
struct MaterialProbeState {
    Ref head, animations, treeTemplate, animationParent;
    unsigned morphSets=0;
    std::uintptr_t meshObject=0;
    bool engaged=false, applied=false;
    MaterialProbePhase phase=MaterialProbePhase::Invalid;
} materialProbe;

bool TraceRead(std::uintptr_t address,void* destination,size_t bytes) {
    const auto error=GetLastError();
    SIZE_T copied=0;
    const bool valid=address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),destination,bytes,&copied) && copied==bytes;
    SetLastError(error);return valid;
}
UObject* TraceObject(std::uintptr_t address) {
    int index=-1;
    if (!address || !UObject::GObjObjects || !TraceRead(address+offsetof(UObject,ObjectInternalInteger),&index,sizeof(index)) ||
        index<0 || static_cast<unsigned>(index)>=UObject::GObjObjects->Count()) return nullptr;
    auto object=reinterpret_cast<UObject*>(address);
    if ((*UObject::GObjObjects)(index)!=object) return nullptr;
    Ref ref;ref.Set(object);return ref.Alive() ? object : nullptr;
}
#include "ResourceTraceBridge.inl"

void TraceUsageObject(const RenderTrace::Usage& e) {
    auto object=TraceObject(e.object);
    auto instanceObject=TraceObject(e.instanceObject);
    Log(L"RENDER TRACE CHECK kind=%ls objectPtr=%p object=%ls interfacePtr=%p interface=%ls usage=%d result=%d recompile=%d callerRVA=%llx",
        e.base ? L"native-base" : L"native-instance",reinterpret_cast<void*>(e.object),object ? *object->GetFullPath() : L"unresolved",
        reinterpret_cast<void*>(e.instanceObject),instanceObject ? *instanceObject->GetFullPath() : L"none",e.usage,e.result,e.recompile,e.caller);
}
// Observe existing calls only: no extra CheckUsage/GetMaterial invocation and
// no changed return/output values. TLS is enabled solely within the selected
// NPC component's own engine update, on its established game thread.
int TraceBaseUsage(UMaterial* material,int* recompile,int usage) {
    auto trace=currentRenderTrace;
    auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-moduleBase;
    const int result=originalBaseUsage(material,recompile,usage);
    if(trace && (usage==0 || usage==14)) trace->Add({reinterpret_cast<std::uintptr_t>(material),trace->currentInterface,caller,usage,result,recompile ? *recompile : -1,true});
    return result;
}
int TraceInstanceUsage(UMaterialInterface* material,int usage) {
    auto trace=currentRenderTrace;
    const auto previous=trace ? trace->currentInterface : 0;
    auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-moduleBase;
    if(trace)trace->currentInterface=reinterpret_cast<std::uintptr_t>(material);
    const int result=originalInstanceUsage(material,usage);
    if(trace) {
        if(usage==0 || usage==14)trace->Add({reinterpret_cast<std::uintptr_t>(material),reinterpret_cast<std::uintptr_t>(material),caller,usage,result,0,false});
        trace->currentInterface=previous;
    }
    return result;
}
void TraceMaterialCopy(void* destination,const void* source) {
    auto trace=currentRenderTrace;
    auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-moduleBase;
    originalMaterialCopy(destination,source);
    if(!trace || !RenderTrace::CopySite(caller))return;
    if(trace->copyCount>=trace->copies.size()){trace->overflow=true;return;}
    auto& copy=trace->copies[trace->copyCount++];copy.caller=caller;
    RenderTrace::Capture(reinterpret_cast<std::uintptr_t>(destination),copy,TraceRead);
}
void TraceComponentUsage(USkeletalMeshComponent* head) {
    if(currentRenderTrace || GetCurrentThreadId()!=gameThread.load(std::memory_order_relaxed)) {originalComponentUsage(head);return;}
    unsigned actor=2;
    for(unsigned i=0;i<preparedFaces.size();++i)
        if(preparedFaces[i].head.pointer==head && preparedFaces[i].head.Alive()){actor=i;break;}
    if(actor==2 || !renderTraceBudgets[actor].Begin(head->ActiveMorphs.Count()>0)){originalComponentUsage(head);return;}
    ArmResourceTrace(actor,renderTraceBudgets[actor].captures,head);
    RenderTrace::CaptureData capture;
    std::uintptr_t scene=0,proxy=0,engine=0,defaultMaterial=0;
    std::uint32_t before=0,after=0;
    // These offsets are verified by the guarded native wrapper/fallback code.
    TraceRead(reinterpret_cast<std::uintptr_t>(head)+0xa8,&scene,sizeof(scene));
    if(scene)TraceRead(scene+8,&proxy,sizeof(proxy));
    if(proxy)TraceRead(proxy+0x108,&before,sizeof(before));
    TraceRead(moduleBase+RenderTraceNative::EngineGlobalRva,&engine,sizeof(engine));
    if(engine)TraceRead(engine+0x620,&defaultMaterial,sizeof(defaultMaterial));
    const auto activeCount=head->ActiveMorphs.Count();
    currentRenderTrace=&capture;
    originalComponentUsage(head);
    currentRenderTrace=nullptr;
    if(proxy)TraceRead(proxy+0x108,&after,sizeof(after));
    auto raw=head->Materials.Count()>2 ? head->Materials(2) : nullptr;
    auto displayed=head->m_aEffectsMaterialMICs.Count()>2 ? head->m_aEffectsMaterialMICs(2) : nullptr;
    Log(L"RENDER TRACE BEGIN actor=%ls capture=%u head=%p proxy=%p active=%u proxyMorphBefore=%d proxyMorphAfter=%d rawFace=%p displayedFace=%p engineDefault=%p copies=%u checks=%u overflow=%d",
        actor==0 ? L"Kaidan" : L"Joker",renderTraceBudgets[actor].captures,head,reinterpret_cast<void*>(proxy),activeCount,
        !!(before&0x10),!!(after&0x10),raw,displayed,reinterpret_cast<void*>(defaultMaterial),capture.copyCount,capture.checkCount,capture.overflow);
    for(unsigned i=0;i<capture.checkCount;++i)TraceUsageObject(capture.checks[i]);
    for(unsigned i=0;i<capture.copyCount;++i) {
        auto const& copy=capture.copies[i];
        Log(L"RENDER TRACE COPY stage=%ls callerRVA=%llx count=%u invalid=%d truncated=%d",RenderTrace::CopyStage(copy.caller),copy.caller,copy.count,copy.invalid,copy.truncated);
        for(unsigned j=0;j<copy.count;++j) {
            auto const& e=copy.entries[j];auto object=TraceObject(e.pointer);
            Log(L"RENDER TRACE MATERIAL stage=%ls lod=%u slot=%u pointer=%p object=%ls flags=%x isEngineDefault=%d isRawFace=%d isDisplayedFace=%d",
                RenderTrace::CopyStage(copy.caller),e.lod,e.slot,reinterpret_cast<void*>(e.pointer),object ? *object->GetFullPath() : L"unresolved",e.flags,
                defaultMaterial && e.pointer==defaultMaterial,e.pointer==reinterpret_cast<std::uintptr_t>(raw),e.pointer==reinterpret_cast<std::uintptr_t>(displayed));
        }
    }
    Log(L"RENDER TRACE END actor=%ls",actor==0 ? L"Kaidan" : L"Joker");
}

UMaterial* BaseMaterial(UMaterialInterface* material) {
    for (unsigned depth=0; depth<12 && material; ++depth) {
        if (auto base=material->Cast<UMaterial>()) return base;
        if (auto instance=material->Cast<UMaterialInstance>()) material=instance->Parent;
        else if (auto user=material->Cast<URvrEffectsMaterialUser>()) material=user->m_pBaseMaterial;
        else if (auto mux=material->Cast<URvrMaterialMultiplexor>()) material=mux->m_pDefaultMaterial;
        else return nullptr;
    }
    return nullptr;
}

// Mirror the captured LE1 usage traversal, rather than infer compatibility
// from the default base alone. EffectsUser checks Parent preferentially
// (RVA 89a970); Multiplexor ANDs Default and every tagged parent (89a9f0).
// Read-only guard: never force the engine's usage result or mutate stock flags.
bool MorphUsageChain(UMaterialInterface* material, unsigned depth=0) {
    if (!material || depth>=16) return false;
    if (auto base=material->Cast<UMaterial>()) {
        const bool valid=base->bUsedWithMorphTargets && base->bUsedWithSkeletalMesh;
        if (!valid) Log(L"USAGE CHAIN REFUSED material=%ls morph=%d skeletal=%d",*base->GetFullPath(),base->bUsedWithMorphTargets,base->bUsedWithSkeletalMesh);
        return valid;
    }
    if (auto instance=material->Cast<UMaterialInstance>()) return MorphUsageChain(instance->Parent,depth+1);
    if (auto user=material->Cast<URvrEffectsMaterialUser>())
        return MorphUsageChain(user->m_pParentMaterial ? user->m_pParentMaterial : user->m_pBaseMaterial,depth+1);
    if (auto mux=material->Cast<URvrMaterialMultiplexor>()) {
        if (mux->m_lstParents.Count()>64 || !MorphUsageChain(mux->m_pDefaultMaterial,depth+1)) return false;
        for (auto const& parent : mux->m_lstParents)
            if (!MorphUsageChain(parent.m_pMaterial,depth+1)) return false;
        return true;
    }
    Log(L"USAGE CHAIN REFUSED unknown interface=%ls",*material->GetFullPath());
    return false;
}

template<class Array> std::uint64_t ParameterStamp(const Array& values) {
    std::uint64_t hash=14695981039346656037ull ^ values.Count();
    for (const auto& value : values) {
        const auto bytes=reinterpret_cast<const unsigned char*>(&value);
        for (size_t i=0;i<sizeof(value);++i) hash=(hash ^ bytes[i])*1099511628211ull;
    }
    return hash;
}
auto ParameterStamps(UMaterialInstanceConstant* material) {
    return std::array<std::uint64_t,4>{ParameterStamp(material->FontParameterValues),
        ParameterStamp(material->VectorParameterValues),ParameterStamp(material->ScalarParameterValues),
        ParameterStamp(material->TextureParameterValues)};
}

// Keep GPU skinning. The private shader package preserves the NPC material's
// pixel shaders/parameters and supplies its absent GPU-morph vertex shaders.
// GetMaterial returns the transient effects MIC before the pawn-owned base
// material (verified native getter RVA 5843a0). Validate both independently.
// Preserve the effects MIC identity and overrides; give it a head-owned dynamic
// parent carrying the original pawn parameters and a private compiled template.
template<class Array, class Equal>
bool SameValues(const Array& from, const Array& to, Equal equal) {
    if (from.Count()!=to.Count()) return false;
    for (auto const& a : from) {
        bool found=false;
        for (auto const& b : to) if (a.ParameterName==b.ParameterName) { found=equal(a,b); break; }
        if (!found) return false;
    }
    return true;
}
bool SameFaceValues(UMaterialInstanceConstant* a, UMaterialInstanceConstant* b) {
    return SameValues(a->FontParameterValues,b->FontParameterValues,[](auto& x,auto& y){return x.FontValue==y.FontValue && x.FontPage==y.FontPage;}) &&
        SameValues(a->ScalarParameterValues,b->ScalarParameterValues,[](auto& x,auto& y){return x.ParameterValue==y.ParameterValue;}) &&
        SameValues(a->VectorParameterValues,b->VectorParameterValues,[](auto& x,auto& y){return memcmp(&x.ParameterValue,&y.ParameterValue,sizeof(x.ParameterValue))==0;}) &&
        SameValues(a->TextureParameterValues,b->TextureParameterValues,[](auto& x,auto& y){return x.ParameterValue==y.ParameterValue;});
}
bool CopyEffectiveFaceValues(UMaterialInstanceConstant* leaf, UMaterialInstanceConstant* bridge) {
    std::vector<UMaterialInstanceConstant*> layers;
    for(auto p=leaf;p;p=p->Parent ? p->Parent->Cast<UMaterialInstanceConstant>() : nullptr) {
        if(!IsLiveInstance(p) || layers.size()>=8 || std::find(layers.begin(),layers.end(),p)!=layers.end())return false;
        layers.push_back(p);
    }
    for(auto it=layers.rbegin();it!=layers.rend();++it)if(!EngineCalls::CopyFaceParameters(*it,bridge))return false;
    auto check=[&](auto member,auto equal) {
        using Array=std::remove_reference_t<decltype(bridge->*member)>;
        using Value=std::remove_cvref_t<decltype((bridge->*member)(0))>;
        std::vector<const Array*> values;
        for(auto p:layers)values.push_back(&(p->*member));
        return FaceParameters::Matches<Value>(values,bridge->*member,[](const auto& v){return v.ParameterName;},equal);
    };
    return check(&UMaterialInstanceConstant::FontParameterValues,[](auto& a,auto& b){return a.FontValue==b.FontValue && a.FontPage==b.FontPage;}) &&
        check(&UMaterialInstanceConstant::ScalarParameterValues,[](auto& a,auto& b){return a.ParameterValue==b.ParameterValue;}) &&
        check(&UMaterialInstanceConstant::VectorParameterValues,[](auto& a,auto& b){return !memcmp(&a.ParameterValue,&b.ParameterValue,sizeof(a.ParameterValue));}) &&
        check(&UMaterialInstanceConstant::TextureParameterValues,[](auto& a,auto& b){return a.ParameterValue==b.ParameterValue;});
}
bool PrepareMaterials(ABioPawn* speaker, USkeletalMeshComponent* head) {
    if (activeLine && *activeLine->participantTag) {
        if (head->Owner!=speaker || head->Materials.Count()!=4 || head->m_aEffectsMaterialMICs.Count()!=4) {
            Log(L"REFUSED: unexpected NPC head ownership or material/cache slots."); return false;
        }
        auto original=head->Materials(2);
        auto leaf=original ? original->Cast<UMaterialInstanceConstant>() : nullptr;
        const bool joker=leaf && leaf->bHasStaticPermutationResource; // Template follows material capability, not character identity.
        auto& saved=preparedFaces[wcscmp(activeLine->actor,L"Joker")==0 ? 1 : 0]; // Two scoped NPC diagnostic slots.
        auto material=EngineCalls::Material(head,2);
        auto displayed=material ? material->Cast<UMaterialInstanceConstant>() : nullptr;
        Log(L"FACE SOURCE actor=%ls original=%ls outer=%ls static=%d displayed=%ls cached=%d displayedStatic=%d parent=%ls",
            activeLine->actor,leaf ? *leaf->GetFullPath() : L"null",leaf && leaf->Outer ? *leaf->Outer->GetFullPath() : L"null",
            leaf ? leaf->bHasStaticPermutationResource : 0,displayed ? *displayed->GetFullPath() : L"null",
            displayed==head->m_aEffectsMaterialMICs(2),displayed ? displayed->bHasStaticPermutationResource : 0,
            displayed && displayed->Parent ? *displayed->Parent->GetFullPath() : L"null");
        if (!IsLiveInstance(leaf) || leaf->Outer!=speaker || !IsLiveInstance(leaf->Parent)) {
            Log(L"REFUSED: unexpected pawn-owned face material or static permutation."); return false;
        }
        if (!IsLiveInstance(displayed) || displayed!=head->m_aEffectsMaterialMICs(2) || displayed==leaf ||
            !displayed->Parent || displayed->bHasStaticPermutationResource) {
            Log(L"REFUSED: displayed face is not the head's non-static effects instance."); return false;
        }
        const bool enhanced=saved.head.As<USkeletalMeshComponent>()==head && saved.source.pointer==leaf &&
            saved.displayed.As<UMaterialInstanceConstant>()==displayed && saved.bridge.Alive() && displayed->Parent==saved.bridge.pointer;
        if (!enhanced) {
            auto previousBase=BaseMaterial(displayed);
            if (!previousBase || !previousBase->GetFullPath().Equals(L"BIOG_Humanoid_MASTER_MTR_R.Human.HMM_HED_PRONPC_MASTER_FACE_MAT")) {
                Log(L"REFUSED: unknown displayed NPC base material."); return false;
            }
            // Check every unaffected slot before changing any live parent.
            for (int slot=0;slot<4;++slot) if (slot!=2) {
                auto base=BaseMaterial(EngineCalls::Material(head,slot));
                if (!base || !base->bUsedWithMorphTargets) {Log(L"REFUSED: unaffected slot %d lacks morph support.",slot);return false;}
            }
            Log(L"FACE LOAD BEGIN actor=%ls object=%ls",activeLine->actor,joker ? L"MEFFaceShaders.JokerFaceTemplate" : L"MEFFaceShaders.NPCFaceTemplate");
            auto loaded=EngineCalls::LoadFaceTemplate(speaker,joker);
            Log(L"FACE LOAD END actor=%ls result=%ls class=%ls",activeLine->actor,
                loaded ? *loaded->GetFullPath() : L"null",loaded ? *loaded->Class->GetFullPath() : L"null");
            auto replacement=loaded ? loaded->Cast<UMaterialInstanceConstant>() : nullptr;
            if (!replacement) {Log(L"REFUSED: private face template missing or wrong class.");return false;}
            if (joker && !replacement->IsA(UBioMaterialInstanceConstant::StaticClass())) {
                Log(L"REFUSED: Joker template lost its native BioMaterialInstanceConstant subclass.");return false;
            }
            auto base=BaseMaterial(replacement);
            Log(L"FACE ASSET actor=%ls parent=%ls static=%d base=%ls baseClass=%ls morph=%d",
                activeLine->actor,replacement->Parent ? *replacement->Parent->GetFullPath() : L"null",
                replacement->bHasStaticPermutationResource,base ? *base->GetFullPath() : L"null",
                base ? *base->Class->GetFullPath() : L"null",base ? base->bUsedWithMorphTargets : 0);
            if (!base) {Log(L"REFUSED: private face template's base chain is unresolved.");return false;}
            if (!!replacement->bHasStaticPermutationResource!=joker) {Log(L"REFUSED: private template static permutation mismatch.");return false;}
            if (!base->GetFullPath().Equals(L"MEFFaceShaders.NPCFace")) {Log(L"REFUSED: private base material has incorrect package identity.");return false;}
            if (!base->bUsedWithMorphTargets) {Log(L"REFUSED: private base lacks morph usage.");return false;}
            if (!MorphUsageChain(replacement)) {Log(L"REFUSED: private effects chain lacks complete morph/skeletal usage.");return false;}
            Log(L"USAGE CHAIN actor=%ls private default and all tagged effects passed",activeLine->actor);
            Log(L"FACE PARAMETERS BEGIN actor=%ls",activeLine->actor);
            auto bridge=static_cast<UMaterialInstanceConstant*>(Construct(UMaterialInstanceConstant::StaticClass(),head,L"MEF_SL_FaceParameters"));
            if (!IsLiveInstance(bridge) || bridge->Outer!=head || bridge->bHasStaticPermutationResource ||
                !EngineCalls::MaterialParent(bridge,replacement) || !CopyEffectiveFaceValues(leaf,bridge)) {
                Log(L"REFUSED: head-owned parameter copy failed; displayed material untouched."); return false;
            }
            Log(L"FACE PARAMETERS END actor=%ls matched=1",activeLine->actor);
            auto originalParent=displayed->Parent;
            const auto parameters=ParameterStamps(leaf);
            const auto effectsParameters=ParameterStamps(displayed);
            Log(L"FACE REPARENT BEGIN actor=%ls",activeLine->actor);
            if (!EngineCalls::MaterialParent(displayed,bridge) || ParameterStamps(leaf)!=parameters ||
                ParameterStamps(displayed)!=effectsParameters || EngineCalls::Material(head,2)!=displayed || BaseMaterial(displayed)!=base) {
                const bool restored=EngineCalls::MaterialParent(displayed,originalParent);
                if (!restored) {disabled=true;restorationFault=true;}
                Log(L"REFUSED: native material parent change or parameter preservation failed."); return false;
            }
            saved.head.Set(head);saved.source.Set(leaf);saved.displayed.Set(displayed);saved.bridge.Set(bridge);
            saved.previousParent.Set(originalParent);
            Log(L"SHADER actor=%ls slot=2 effects parent installed; previous=%ls template=%ls original and effects parameters retained; pawn material unchanged",
                activeLine->actor,*originalParent->GetFullPath(),*replacement->GetFullPath());
        }
    }
    // Include mesh-default materials (Shepard has no component overrides).
    // Refuse morph activation rather than enter the engine's fallback material.
    for (int slot=0;slot<4;++slot) {
        auto material=EngineCalls::Material(head,slot);
        auto base=BaseMaterial(material);
        Log(L"SHADER CHECK actor=%ls slot=%d material=%ls base=%ls morph=%d",
            activeLine ? activeLine->actor : L"none",slot,
            material ? *material->GetFullPath() : L"null",base ? *base->GetFullPath() : L"null",
            base ? base->bUsedWithMorphTargets : 0);
        if (!base || !base->bUsedWithMorphTargets) return false;
        // Preserve the established Shepard path; tighten NPC-only validation.
        if (activeLine && *activeLine->participantTag && !MorphUsageChain(material)) return false;
    }
    return true;
}

// Read-only state for separating graph setup from the first rendered morph.
void RenderState(USkeletalMeshComponent* head, const wchar_t* stage) {
    if (!head) return;
    std::uintptr_t meshObject = 0;
    memcpy(&meshObject, &head->MeshObject, sizeof(meshObject));
    Log(L"RENDER stage=%ls actor=%ls meshObject=%p materials=%u active=%u lod=%d",
        stage, activeLine ? activeLine->actor : L"none", reinterpret_cast<void*>(meshObject),
        head->Materials.Count(), head->ActiveMorphs.Count(), head->PredictedLODLevel);
    if (meshObject) {
        auto table = *reinterpret_cast<std::uintptr_t**>(meshObject);
        auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        Log(L"RENDER vtableRVA=%llx updateRVA=%llx", reinterpret_cast<std::uintptr_t>(table)-module, table[5]-module);
    }
    for (unsigned i=0; i<head->Materials.Count(); ++i) {
        auto mat = head->Materials(i);
        if (!mat) continue;
        Log(L"MATERIAL stage=%ls slot=%u object=%ls class=%ls", stage, i, *mat->GetFullPath(), *mat->Class->GetFullPath());
        for (unsigned depth=0; depth<8 && mat; ++depth) {
            if (auto base=mat->Cast<UMaterial>()) {
                Log(L"MATERIAL BASE stage=%ls object=%ls skinned=%d morph=%d", stage, *base->GetFullPath(), base->bUsedWithSkeletalMesh, base->bUsedWithMorphTargets);
                break;
            }
            if (auto instance=mat->Cast<UMaterialInstance>()) mat=instance->Parent;
            else if (auto user=mat->Cast<URvrEffectsMaterialUser>()) mat=user->m_pBaseMaterial;
            else if (auto mux=mat->Cast<URvrMaterialMultiplexor>()) mat=mux->m_pDefaultMaterial;
            else break;
        }
    }
}

template<class T> void ErasePointer(TArray<T*>& array, T* p) {
    for (unsigned i = array.Count(); i > 0; --i) if (array(i-1) == p) array.Erase(i-1);
}

void RestoreMaterialProbe(const wchar_t* reason) {
    if (!materialProbe.engaged) return;
    auto& saved=preparedFaces[0];
    auto head=materialProbe.head.As<USkeletalMeshComponent>();
    auto displayed=saved.displayed.As<UMaterialInstanceConstant>();
    auto original=saved.previousParent.As<UMaterialInterface>();
    if (materialProbe.applied && head && displayed && original) {
        const bool owned=saved.head.pointer==head && head->m_aEffectsMaterialMICs.Count()>2 &&
            head->m_aEffectsMaterialMICs(2)==displayed && displayed->Parent==saved.bridge.pointer;
        const bool restored=displayed->Parent==original || (owned && EngineCalls::MaterialParent(displayed,original));
        std::uintptr_t meshObject=0;memcpy(&meshObject,&head->MeshObject,sizeof(meshObject));
        const bool graphUnchanged=head->Animations==materialProbe.animations.pointer &&
            head->AnimTreeTemplate==materialProbe.treeTemplate.pointer && head->MorphSets.Count()==materialProbe.morphSets &&
            head->ParentAnimComponent==materialProbe.animationParent.pointer && meshObject==materialProbe.meshObject;
        Log(L"MATERIAL ONLY RESTORE reason=%ls restored=%d graphAndMeshUnchanged=%d material=%ls parent=%ls",
            reason,restored,graphUnchanged,*displayed->GetFullPath(),displayed->Parent ? *displayed->Parent->GetFullPath() : L"null");
        if (!restored) {
            disabled=true;restorationFault=true;
            Log(L"CRITICAL: material-only probe could not restore its owned parent; intervention disabled. Restart game.");
        }
    } else if (materialProbe.applied) {
        Log(L"MATERIAL ONLY RESTORE reason=%ls selected objects expired; no stale writes",reason);
    }
    materialProbe.applied=false;
    saved={};
}

void MaterialOnly(ABioPawn* speaker, USkeletalMeshComponent* head, UAudioComponent* audio) {
    const auto t=audio->PlaybackTime;
    const auto phase=MaterialProbeAt(t);
    if (!materialProbe.engaged) {
        materialProbe.engaged=true;materialProbe.head.Set(head);
        materialProbe.animations.Set(head->Animations);materialProbe.treeTemplate.Set(head->AnimTreeTemplate);
        materialProbe.animationParent.Set(head->ParentAnimComponent);materialProbe.morphSets=head->MorphSets.Count();
        memcpy(&materialProbe.meshObject,&head->MeshObject,sizeof(materialProbe.meshObject));
        Log(L"MATERIAL ONLY START actor=Kaidan no rig/node/weight; baseline <0.5s, private 0.5..1.6s, original >=1.6s");
    }
    if (phase!=materialProbe.phase) {
        materialProbe.phase=phase;
        Log(L"MATERIAL ONLY PHASE actor=Kaidan audio=%.6f phase=%d activeMorphs=%u curveMorphs=%u",t,
            static_cast<int>(phase),head->ActiveMorphs.Count(),head->ActiveCurveMorphs.Count());
    }
    if (phase!=MaterialProbePhase::PrivateMaterial) {
        RestoreMaterialProbe(phase==MaterialProbePhase::Restored ? L"audio-window-ended" : L"baseline-or-invalid-clock");
        return;
    }
    if (attempted) return;
    attempted=true;
    // An existing custom morph graph would invalidate this control experiment.
    if (head->Animations || head->AnimTreeTemplate || head->ActiveMorphs.Count() || head->ActiveCurveMorphs.Count()) {
        Log(L"REFUSED: material-only control requires a head without active morphs or a head graph.");return;
    }
    const bool prepared=PrepareMaterials(speaker,head);
    // Track a successful parent installation even if a later check refuses.
    materialProbe.applied=preparedFaces[0].head.pointer==head && preparedFaces[0].displayed.Alive() &&
        preparedFaces[0].bridge.Alive();
    Log(L"MATERIAL ONLY APPLY audio=%.6f prepared=%d applied=%d customMorphsInstalled=0",t,prepared,materialProbe.applied);
    if (!prepared) RestoreMaterialProbe(L"preparation-refused");
}

void JokerLoadOnly(ABioPawn* speaker) {
    auto before=UObject::FindObject<UObject>(L"BioMaterialInstanceConstant MEFFaceShaders.JokerFaceTemplate");
    auto type=UBioMaterialInstanceConstant::StaticClass();
    Log(L"JOKER LOAD ONLY BEGIN existing=%ls requestedClass=%ls no material or morph edits",
        before ? *before->GetFullPath() : L"null",type ? *type->GetFullPath() : L"null");
    auto loaded=EngineCalls::LoadFaceTemplate(speaker,true);
    auto found=UObject::FindObject<UObject>(L"BioMaterialInstanceConstant MEFFaceShaders.JokerFaceTemplate");
    auto instance=loaded ? loaded->Cast<UBioMaterialInstanceConstant>() : nullptr;
    auto base=instance ? BaseMaterial(instance) : nullptr;
    Log(L"JOKER LOAD ONLY END loaded=%ls found=%ls class=%ls expectedSubclass=%d static=%d base=%ls morph=%d no material or morph edits",
        loaded ? *loaded->GetFullPath() : L"null",found ? *found->GetFullPath() : L"null",
        loaded ? *loaded->Class->GetFullPath() : L"null",instance!=nullptr,
        instance ? instance->bHasStaticPermutationResource : 0,base ? *base->GetFullPath() : L"null",base ? base->bUsedWithMorphTargets : 0);
}

void Release() {
    releasing = true;
    RestoreMaterialProbe(L"line-exit-skip-disable-or-head-change");
    materialProbe={};
    auto node = nodeRef.As<UMorphNodeMultiPose>();
    auto tree = treeRef.As<UAnimTree>();
    auto head = headRef.As<USkeletalMeshComponent>();
    auto set = setRef.As<UMorphTargetSet>();
    if (head) RenderState(head, L"before-release");
    for(auto& ref:facialTargetRefs)if(auto owned=ref.As<UMorphTarget>()){
        if(node)EngineCalls::Weight(node,owned,0.f);
        if(head)NativeMorph::RemoveOwned(head->ActiveMorphs,owned);
    }
    if (tree && node) ErasePointer(tree->RootMorphNodes, static_cast<UMorphNodeBase*>(node));
    if (head && ownedTemplateRef.Alive() && head->AnimTreeTemplate == ownedTemplateRef.pointer) {
        // Only this route starts from both Animations and AnimTreeTemplate null.
        // Do not overwrite an unrelated graph installed by the engine meanwhile.
        const bool reset = EngineCalls::SetTree(head, nullptr);
        Log(L"TREE RESTORE: api=%d animationsNull=%d templateNull=%d parentUnchanged=%d",
            reset, head->Animations == nullptr, head->AnimTreeTemplate == nullptr,
            head->ParentAnimComponent == parentRef.pointer);
        if (!reset || head->Animations || head->AnimTreeTemplate || head->ParentAnimComponent != parentRef.pointer) {
            disabled = true;
            restorationFault = true;
            Log(L"CRITICAL: temporary tree did not restore expected state; intervention disabled. Restart game before further testing.");
        }
    }
    if (head && set && addedSet) { ErasePointer(head->MorphSets, set); EngineCalls::Init(head); }
    // GPU morph VFs can remain allocated after the last nonzero weight.
    // Keep the equivalent private shader parent on this selected live NPC MIC
    // until normal actor destruction, so zero weight cannot re-enter fallback.
    // No renderer teardown/rebuild; positions return to vanilla at weight zero.
    if (head) RenderState(head, L"after-release");
    if (headRef.pointer) Log(L"RELEASE: result=%ls nativePoseCalls=%llu nativeApplications=%llu withheld=%llu ownershipCalls=%llu restores=%llu refusals=%llu rebuilds=%llu headSourceCalls=%llu parentSourceCalls=%llu inputOwnedPoseChanges=%llu mapState=%ls; boundary-only validated facial pose blend.",activationReported?L"A2F_OBSERVED":L"NO_A2F_OBSERVED",poseCallbacks,poseApplications,poseWithheld,ownershipCalls,ownershipRestores,ownershipRefusals,ownershipRebuilds,ownershipRoutes[0].calls,ownershipRoutes[1].calls,ownershipInputChanges,restorationFault?L"RESTORATION_FAULT":L"SCOPES_RESTORED");
    if(headRef.pointer)Log(L"HANDOVER SUMMARY blendedEvaluations=%llu refused=%llu entryMs=160 exitMs=160 browEntryMs=240 browExitMs=240; named facial outputs only",transitionCalls,transitionRefusals);
    transitionCalls=transitionRefusals=0;transitionAlpha=transitionBrowAlpha=0;
    headRef = {}; treeRef = {}; nodeRef = {}; setRef = {}; targetRef = {}; audioRef = {};
    ownedTemplateRef = {}; parentRef = {}; addedSet = false;
    requestedWeight = 0; lastAudioTime = -1;
    facialTargetRefs={};facialWeights={};activeClip=nullptr;
    poseCallbacks = 0; poseApplications = 0; releasing = false;
    ownershipCalls=ownershipRestores=ownershipRefusals=0;
    ownershipRebuilds=0;
    ownershipInputHash=0;ownershipInputChanges=0;
    ownershipRoutes={};ownershipCoverage={};poseWithheld=0;
    activationReported=activationFailureReported=false;
    untrackedFaceSources=0;
}

bool OwnershipBound() {
    auto head=headRef.As<USkeletalMeshComponent>();auto node=nodeRef.As<UMorphNodeMultiPose>();
    return !releasing && !disabled && busy && activeClip && activeLine && head && node && treeRef.Alive() && parentRef.Alive() &&
        head->Animations==treeRef.pointer && head->ParentAnimComponent==parentRef.pointer && node->SkelComponent==head;
}
bool RefreshOwnershipMap(OwnershipRoute& route,USkeletalMeshComponent* component) {
    static_assert(offsetof(USkeletalMeshComponent,RequiredBones)==0x2d0);
    auto& required=component->RequiredBones;
    // Native589ee0..589f3a checks membership in RequiredBones, then writes
    // the original raw skeleton index (r8b), NOT the position in that list.
    return FaceOwnership::RequiredMap(route.skeleton,required.GetData(),required.Count(),route.expected);
}
bool PrepareOwnershipRoute(OwnershipRoute& route,USkeletalMeshComponent* component) {
    if(route.failed)return false;
    if(route.prepared)return route.component.Alive() && route.mesh.Alive() && route.asset.Alive() &&
        component->SkeletalMesh==route.mesh.pointer && component->m_pBioFaceFXAsset==route.asset.pointer && RefreshOwnershipMap(route,component);
    if(!component->SkeletalMesh || !component->m_pBioFaceFXAsset)return false;
    route.mesh.Set(component->SkeletalMesh);route.asset.Set(component->m_pBioFaceFXAsset);
    if(!route.mesh.Alive() || !route.asset.Alive() ||
       !route.asset.pointer->GetFullPath().Equals(L"BIOG_FaceFX_Assets.SFX_HumanMale_FaceFX"))return false;
    unsigned owned=0;
    for(size_t i=0;i<FaceOwnership::BoneCount;++i){
        int index=-1;
        if(!EngineCalls::MatchBone(component,FaceOwnership::BoneNames[i],index) || index < -1 || index>=255){Log(L"OWNERSHIP BONE API REFUSED name=%ls index=%d",FaceOwnership::BoneNames[i],index);route.failed=true;return false;}
        route.skeleton[i]=index<0?255:static_cast<unsigned char>(index);
        if(FaceOwnership::Owned[i]){
            if(index<0){Log(L"OWNERSHIP OWNED BONE MISSING name=%ls component=%ls",FaceOwnership::BoneNames[i],*component->GetFullPath());route.failed=true;return false;}
            SFXName name;
            if(!EngineCalls::BoneName(component,index,name) || _wcsicmp(*name.ToString(),FaceOwnership::BoneNames[i])){Log(L"OWNERSHIP BONE NAME REFUSED expected=%ls index=%d",FaceOwnership::BoneNames[i],index);route.failed=true;return false;}
            ++owned;
        }
    }
    route.prepared=owned==23;
    if(route.prepared)Log(L"OWNERSHIP ROUTE component=%ls mesh=%ls asset=%ls namedOwnedBones=%u mapCount=%u atoms=%u",*component->GetFullPath(),*component->SkeletalMesh->GetFullPath(),*route.asset.pointer->GetFullPath(),owned,component->m_aBioFFXBoneMap.Count(),component->LocalAtoms.Count());
    return route.prepared && RefreshOwnershipMap(route,component);
}
void OwnershipFault(USkeletalMeshComponent* component,FaceOwnership::Lifecycle& life,const wchar_t* boundary) {
    disabled=true;restorationFault=true;life.refused=true;
    auto& map=component->m_aBioFFXBoneMap;
    Log(L"CRITICAL: ownership restoration failed boundary=%ls component=%p oldMap=%p currentMap=%p count=%u rebuilds=%u instance=%p; bridge disabled; restart game.",boundary,component,life.mask.data,map.GetData(),map.Count(),life.rebuilds,component->FaceFXActorInstance);
    if(map.GetData() && map.Count()==FaceOwnership::BoneCount){
        unsigned shown=0;
        for(unsigned i=0;i<FaceOwnership::BoneCount;++i)if(map(i)!=(life.mask.suppress && FaceOwnership::Owned[i]?255:life.mask.before[i]) && shown++<8)
            Log(L"OWNERSHIP AFTER name=%ls owned=%d before=%u actual=%u",FaceOwnership::BoneNames[i],FaceOwnership::Owned[i],life.mask.before[i],map(i));
    }
}
// Normal rebuild589ca0 is called inside590670 after audio/animation lifecycle
// updates. Remove our mask BEFORE it may resize/free the array. Its original
// operation runs once; remask only an exact, revalidated same-actor result.
void FaceRebuild(USkeletalMeshComponent* component) {
    auto scope=currentOwnershipScope;
    if(!scope || component!=scope->component || GetCurrentThreadId()!=gameThread.load(std::memory_order_relaxed)){
        originalFaceRebuild(component);return;
    }
    auto& life=scope->lifecycle;
    bool restored=life.BeforeRebuild(component->m_aBioFFXBoneMap.GetData(),component->m_aBioFFXBoneMap.Count());
    if(!restored)OwnershipFault(component,life,L"before-native-rebuild");
    originalFaceRebuild(component);
    ++ownershipRebuilds;
    std::uintptr_t instance=0,actor=0,assetActor=0;unsigned boneCount=0;
    bool valid=restored && OwnershipBound() && scope->route->component.Alive() &&
        scope->route->mesh.Alive() && scope->route->asset.Alive() &&
        component->SkeletalMesh==scope->route->mesh.pointer && component->m_pBioFaceFXAsset==scope->route->asset.pointer &&
        TraceRead(reinterpret_cast<std::uintptr_t>(&component->FaceFXActorInstance),&instance,8) && instance==scope->instance &&
        TraceRead(instance+0x10,&actor,8) && actor==scope->actor &&
        TraceRead(actor+0x28,&boneCount,4) && boneCount==FaceOwnership::BoneCount &&
        TraceRead(reinterpret_cast<std::uintptr_t>(&component->m_pBioFaceFXAsset->FaceFXActor),&assetActor,8) && assetActor==actor &&
        scope->atoms==&component->LocalAtoms && PrepareOwnershipRoute(*scope->route,component);
    bool applied=restored && life.AfterRebuild(component->m_aBioFFXBoneMap.GetData(),component->m_aBioFFXBoneMap.Count(),scope->route->expected,scope->atoms->Count(),valid);
    if(ownershipRebuilds<=4 || !applied)Log(L"OWNERSHIP REBUILD actor=%ls restoredBeforeResize=%d sameActorValid=%d reapplied=%d map=%p count=%u; original writer retained",activeLine?activeLine->actor:L"none",restored,valid,applied,component->m_aBioFFXBoneMap.GetData(),component->m_aBioFFXBoneMap.Count());
}
// The verified 590670 loops skip bone-map entries ==255, in both additive and
// overwrite modes. Mask final outputs, rather than mixed graph registers that
// would also suppress vanilla eye/blink outputs. At boundaries run vanilla
// once, then blend only validated named facial atoms toward their input pose.
void FaceUpdate(USkeletalMeshComponent* head,TArray<FBoneAtom>* atoms,float delta,int tickFace) {
    if(!head || GetCurrentThreadId()!=gameThread.load(std::memory_order_relaxed) || !OwnershipBound()){
        originalFaceUpdate(head,atoms,delta,tickFace);return;
    }
    int source=-1;
    for(unsigned i=0;i<ownershipRoutes.size();++i)if(ownershipRoutes[i].component.pointer==head && ownershipRoutes[i].component.Alive())source=static_cast<int>(i);
    if(source<0){
        auto visible=headRef.As<USkeletalMeshComponent>();
        if(visible && head->Owner==visible->Owner && untrackedFaceSources++<2)Log(L"OWNERSHIP UNTRACKED SAME-PAWN SOURCE component=%ls mesh=%ls asset=%ls",*head->GetFullPath(),head->SkeletalMesh?*head->SkeletalMesh->GetFullPath():L"null",head->m_pBioFaceFXAsset?*head->m_pBioFaceFXAsset->GetFullPath():L"null");
        originalFaceUpdate(head,atoms,delta,tickFace);return;
    }
    auto kind=source==0?FaceRouting::Source::Head:FaceRouting::Source::Parent;
    auto& route=ownershipRoutes[source];ownershipCoverage.Begin(kind,frame);
    if(insideFaceUpdate){
        if(currentOwnershipScope)currentOwnershipScope->lifecycle.refused=true;
        ++ownershipRefusals;ownershipCoverage.Refused(kind,frame);
        originalFaceUpdate(head,atoms,delta,tickFace);return;
    }
    auto visible=headRef.As<USkeletalMeshComponent>();
    const bool prepared=visible && head->Owner==visible->Owner && PrepareOwnershipRoute(route,head);
    auto const* expected=&route.expected;
    std::uintptr_t instance=0,actor=0,assetActor=0;unsigned boneCount=0;
    Ref asset;asset.Set(head->m_pBioFaceFXAsset);
    const bool valid=prepared && atoms==&head->LocalAtoms && asset.Alive() &&
        wcscmp(*asset.pointer->GetFullPath(),L"BIOG_FaceFX_Assets.SFX_HumanMale_FaceFX")==0 &&
        TraceRead(reinterpret_cast<std::uintptr_t>(&head->FaceFXActorInstance),&instance,8) && instance &&
        TraceRead(instance+0x10,&actor,8) && actor && TraceRead(actor+0x28,&boneCount,4) && boneCount==FaceOwnership::BoneCount &&
        TraceRead(reinterpret_cast<std::uintptr_t>(&head->m_pBioFaceFXAsset->FaceFXActor),&assetActor,8) && assetActor==actor;
    OwnershipScope scope{head,atoms,&route,instance,actor,{}};
    auto& life=scope.lifecycle;
    const bool boundary=std::min(transitionAlpha,transitionBrowAlpha)<1.f;
    std::array<FBoneAtom,FaceOwnership::BoneCount> inputPose{};
    auto inputMap=route.expected;
    auto* inputStorage=valid?atoms->GetData():nullptr;
    const unsigned inputCount=valid?atoms->Count():0;
    const bool captured=!boundary || (valid && FaceTransition::Capture(inputStorage,inputCount,inputMap,inputPose));
    if(!valid || !captured || !life.mask.Apply(head->m_aBioFFXBoneMap.GetData(),head->m_aBioFFXBoneMap.Count(),*expected,atoms->Count(),!boundary)){
        ++ownershipRefusals;
        ownershipCoverage.Refused(kind,frame);
        if(ownershipRefusals==1){
            Log(L"OWNERSHIP REFUSED actor=%ls source=%ls prepared=%d valid=%d count=%u atoms=%u instance=%p actor=%p assetActor=%p referenceCount=%u atomArgMatches=%d; A2F withheld",activeLine->actor,source?L"parent":L"head",prepared,valid,head->m_aBioFFXBoneMap.Count(),atoms?atoms->Count():0,reinterpret_cast<void*>(instance),reinterpret_cast<void*>(actor),reinterpret_cast<void*>(assetActor),boneCount,atoms==&head->LocalAtoms);
            if(prepared && head->m_aBioFFXBoneMap.Count()==FaceOwnership::BoneCount){
                unsigned mismatches=0;
                for(unsigned i=0;i<FaceOwnership::BoneCount;++i)if(head->m_aBioFFXBoneMap(i)!=route.expected[i] && mismatches++<6)Log(L"OWNERSHIP MAP MISMATCH name=%ls expected=%u actual=%u owned=%d",FaceOwnership::BoneNames[i],route.expected[i],head->m_aBioFFXBoneMap(i),FaceOwnership::Owned[i]);
            }
        }
        originalFaceUpdate(head,atoms,delta,tickFace);return;
    }
    // Read-only diagnostic: if these inputs already animate, the parent pose
    // may be another source of speech. Do not infer isolation from masks alone.
    std::uint64_t hash=14695981039346656037ull;
    for(size_t i=0;i<FaceOwnership::BoneCount;++i)if(FaceOwnership::Owned[i] && (*expected)[i]!=255){
        auto bytes=reinterpret_cast<const unsigned char*>(&(*atoms)((*expected)[i]));
        for(size_t j=0;j<sizeof(FBoneAtom);++j){hash^=bytes[j];hash*=1099511628211ull;}
    }
    if(ownershipCalls && hash!=ownershipInputHash)++ownershipInputChanges;
    ownershipInputHash=hash;
    insideFaceUpdate=true;
    currentOwnershipScope=&scope;
    originalFaceUpdate(head,atoms,delta,tickFace);
    currentOwnershipScope=nullptr;
    insideFaceUpdate=false;
    ++ownershipCalls;
    if(!route.component.Alive()){
        disabled=true;restorationFault=true;life.refused=true;
        Log(L"CRITICAL: facial component lifetime ended inside native evaluation; no stale map/pose write; bridge disabled.");
        ownershipCoverage.Refused(kind,frame);return;
    }
    std::uintptr_t afterInstance=0,afterActor=0;
    bool sameIdentity=route.mesh.Alive() && route.asset.Alive() &&
        head->SkeletalMesh==route.mesh.pointer && head->m_pBioFaceFXAsset==route.asset.pointer &&
        TraceRead(reinterpret_cast<std::uintptr_t>(&head->FaceFXActorInstance),&afterInstance,8) && afterInstance==scope.instance &&
        TraceRead(afterInstance+0x10,&afterActor,8) && afterActor==scope.actor;
    if(!sameIdentity){
        life.refused=true;
        Log(L"OWNERSHIP TRANSITION source=%ls oldInstance=%p newInstance=%p oldActor=%p newActor=%p; this evaluation withheld",source?L"parent":L"head",reinterpret_cast<void*>(scope.instance),reinterpret_cast<void*>(afterInstance),reinterpret_cast<void*>(scope.actor),reinterpret_cast<void*>(afterActor));
    }
    if(!life.Finish(head->m_aBioFFXBoneMap.GetData(),head->m_aBioFFXBoneMap.Count())){
        OwnershipFault(head,life,L"native-return");ownershipCoverage.Refused(kind,frame);return;
    }
    ++ownershipRestores;
    if(life.refused){++ownershipRefusals;ownershipCoverage.Refused(kind,frame);return;}
    if(boundary){
        // Never reuse snapshots across calls, actors, storage changes or LOD maps.
        const bool blendValid=OwnershipBound() && route.component.Alive() && atoms==&head->LocalAtoms &&
            atoms->GetData()==inputStorage && atoms->Count()==inputCount && route.expected==inputMap &&
            std::equal(inputMap.begin(),inputMap.end(),head->m_aBioFFXBoneMap.GetData()) &&
            FaceTransition::Blend(inputStorage,inputCount,inputMap,inputPose,transitionAlpha,transitionBrowAlpha);
        if(!blendValid){
            ++transitionRefusals;++ownershipRefusals;ownershipCoverage.Refused(kind,frame);
            if(transitionRefusals==1)Log(L"HANDOVER REFUSED actor=%ls source=%ls alpha=%.6f browAlpha=%.6f; vanilla retained, custom withheld",activeLine->actor,source?L"parent":L"head",transitionAlpha,transitionBrowAlpha);
            return;
        }
        ++transitionCalls;
        if(transitionCalls==1)Log(L"HANDOVER actor=%ls source=%ls alpha=%.6f browAlpha=%.6f audio=%.6f count=%u; original evaluated once, protected outputs untouched",activeLine->actor,source?L"parent":L"head",transitionAlpha,transitionBrowAlpha,lastAudioTime,inputCount);
    }
    ++route.calls;ownershipCoverage.Applied(kind,frame);
    if(route.calls==1)Log(L"OWNERSHIP actor=%ls source=%ls ownedBones=23 protectedEyeBlinkHeadBody=retained mapRestored=1; steady mask with boundary pose blend",activeLine->actor,source?L"parent":L"head");
}

// LE1's parent-pose branch at RVA 592867 can bypass the native morph pass at
// 592e33..592e72. Complete ONLY our node's evaluation after the original pose
// routine, before its caller schedules the component/render update. Do not
// change the parent-pose path, parent link, other nodes or curve morphs. The
// separate, verified FaceUpdate hook owns only selected facial bone outputs.
void Pose(USkeletalMeshComponent* component, float delta, int tickFace, int force) {
    originalPose(component, delta, tickFace, force);
    if (GetCurrentThreadId() != gameThread.load(std::memory_order_relaxed)) return;
    if (releasing || disabled || !busy || !headRef.pointer || component != headRef.pointer) return;
    auto head = headRef.As<USkeletalMeshComponent>();
    auto node = nodeRef.As<UMorphNodeMultiPose>();
    auto target = targetRef.As<UMorphTarget>();
    if (!head || !node || !target || !treeRef.Alive() || head->Animations != treeRef.pointer ||
        !parentRef.Alive() || head->ParentAnimComponent != parentRef.pointer || node->SkelComponent != head) return;
    ++poseCallbacks;
    if(!ownershipCoverage.Isolated(frame)){
        ++poseWithheld;
        if(poseWithheld==1)Log(L"POSE WITHHELD actor=%ls frame=%llu headSeen=%llu headMasked=%llu parentSeen=%llu parentMasked=%llu; waiting for current-tick ownership",activeLine?activeLine->actor:L"none",frame,ownershipCoverage.state[0].seen,ownershipCoverage.state[0].masked,ownershipCoverage.state[1].seen,ownershipCoverage.state[1].masked);
        for(auto& ref:facialTargetRefs)NativeMorph::RemoveOwned(head->ActiveMorphs,ref.As<UMorphTarget>());
        return; // Never knowingly stack A2F over unisolated vanilla speech.
    }
    if(node->Targets.Count()!=facialChannelCount || node->Weights.Count()!=facialChannelCount){
        disabled=true;Log(L"REFUSED: native multi-channel node size mismatch.");return;
    }
    bool nonzero=false;
    for(unsigned i=0;i<facialChannelCount;++i){
        auto owned=facialTargetRefs[i].As<UMorphTarget>();const float weight=node->Weights(i);
        if(!owned || node->Targets(i)!=owned || !std::isfinite(weight) || weight<0 || weight>1){
            disabled=true;Log(L"REFUSED: native multi-channel node state mismatch.");return;
        }
        nonzero|=weight>0;
    }
    for(auto& ref:facialTargetRefs)NativeMorph::RemoveOwned(head->ActiveMorphs,ref.As<UMorphTarget>());
    if (nonzero) {
        // Native UMorphNodeMultiPose evaluator, identified from the live CDO
        // vtable slot 0x2e8 and disassembled; it reads Targets/Weights and emits
        // 12-byte FActiveMorph records. No raw guessed array injection.
        evaluateMorph(node, &head->ActiveMorphs);
        ++poseApplications;
    }
    if (poseCallbacks == 1 || frame % 30 == 0) Log(L"NATIVE actor=%ls poseCalls=%llu applications=%llu nodeWeight=%.4f activeCount=%u",
        activeLine ? activeLine->actor : L"none", poseCallbacks, poseApplications, node->Weights(0), head->ActiveMorphs.Count());
}

UObject* Construct(UClass* type, UObject* outer, const wchar_t* name) {
    return type ? constructObject(type, outer, SFXName(name, 0), EObjectFlags::Transient,
        nullptr, errorDevice ? *errorDevice : nullptr, nullptr, nullptr) : nullptr;
}

// Empty head-only graph: no bone animation child, no skeletal controllers,
// no saved pose, and no changes to the parent component or its animation tree.
UAnimTree* CreateHeadTree(USkeletalMeshComponent* head) {
    auto parent = head->ParentAnimComponent;
    if (head->Animations || head->AnimTreeTemplate || !IsLiveInstance(head) ||
        !IsLiveInstance(parent) || !head->Owner || parent->Owner != head->Owner) {
        Log(L"REFUSED: temporary tree requires an empty live head and same-owner animation parent."); return nullptr;
    }
    auto prototype = static_cast<UAnimTree*>(Construct(UAnimTree::StaticClass(), head, L"MEF_SL_HeadMorphTemplate"));
    if (!prototype || prototype->RootMorphNodes.Count() || prototype->SkelControlLists.Count() ||
        prototype->AnimGroups.Count() || prototype->SavedPose.Count() || prototype->bUseSavedPose) {
        Log(L"REFUSED: default AnimTree is not empty."); return nullptr;
    }
    for (auto const& child : prototype->Children) if (child.Anim) {
        Log(L"REFUSED: head tree contains a bone animation child."); return nullptr;
    }
    auto parentAnimations = parent->Animations;
    auto faceActor = head->FaceFXActorInstance;
    auto faceDisabled = head->bFaceFXDisabled;
    auto faceMode = head->FaceFXBlendMode;
    const auto overrideParent = head->bOverrideParentSkeleton;
    ownedTemplateRef.Set(prototype); parentRef.Set(parent);
    Log(L"TREE CREATE: using SetAnimTreeTemplate; parent=%ls parentAnim=%ls faceDisabled=%d faceMode=%d overrideParent=%d",
        *parent->GetFullPath(), parentAnimations ? *parentAnimations->GetFullPath() : L"null",
        faceDisabled, faceMode, overrideParent);
    if (!EngineCalls::SetTree(head, prototype)) { Log(L"REFUSED: SetAnimTreeTemplate unavailable."); return nullptr; }
    auto tree = head->Animations ? head->Animations->Cast<UAnimTree>() : nullptr;
    const bool parentOK = head->ParentAnimComponent == parent && parent->Animations == parentAnimations;
    const bool faceOK = memcmp(&faceActor, &head->FaceFXActorInstance, sizeof(faceActor)) == 0 &&
        head->bFaceFXDisabled == faceDisabled && head->FaceFXBlendMode == faceMode &&
        head->bOverrideParentSkeleton == overrideParent;
    Log(L"TREE CREATE result: instance=%ls templateMatches=%d parentUnchanged=%d faceStateUnchanged=%d",
        tree ? *tree->GetFullPath() : L"null", head->AnimTreeTemplate == prototype, parentOK, faceOK);
    if (!parentOK || !faceOK || head->AnimTreeTemplate != prototype || !IsLiveInstance(tree) ||
        tree == prototype || tree->SkelComponent != head) {
        Log(L"REFUSED: engine did not create a dedicated head instance with protected links intact."); return nullptr;
    }
    return tree;
}

bool IsLiveInstance(UObject* object) {
    if (!object) return false;
    Ref r; r.Set(object);
    constexpr auto templates = static_cast<uint64_t>(EObjectFlags::ClassDefaultObject) |
        static_cast<uint64_t>(EObjectFlags::ArchetypeObject);
    return r.Alive() && (static_cast<uint64_t>(object->ObjectFlags) & templates) == 0;
}

// Participant tags are authored conversation roles, independent of storage
// packages. A resolved participant pointer, when present, must agree with the
// live speaker. Exact voice/entry metadata and audio ownership remain gates.
bool MatchesParticipant(UBioConversation* conversation, ABioPawn* speaker, const TestLine& line) {
    if(!IsLiveInstance(speaker))return false;
    bool role=false;
    for(const auto& participant:conversation->m_SpeakerList) {
        if(!participant.sSpeakerTag.ToString().Equals(line.participantTag))continue;
        role=true;
        if(participant.aSpeaker && participant.aSpeaker!=speaker)return false;
    }
    return CompatibleParticipant(line.participantTag,*speaker->Tag.ToString(),
        speaker==conversation->m_pConversationPlayer,role,true);
}

struct RigCache {Ref mesh,set;const TestRig* rig=nullptr;bool player=false;};
std::array<RigCache,8> rigCache;
const TestRig* ResolveRig(ABioPawn* speaker, USkeletalMeshComponent* head, bool player) {
    auto mesh=head->SkeletalMesh;
    if(!IsLiveInstance(mesh))return nullptr;
    for(auto& cached:rigCache) if(cached.mesh.As<USkeletalMesh>()==mesh && cached.player==player) {
        if(!cached.rig)return nullptr;
        auto set=cached.set.As<UMorphTargetSet>();
        if(set && CompatibleRig(*cached.rig,player,mesh->RefSkeleton.Count(),mesh->LODModels.ArrayNum,
            mesh,set->BaseSkelMesh,set->Targets.Count(),facialChannelCount))return cached.rig;
        cached={};
    }
    RigCache* slot=nullptr;
    for(auto& cached:rigCache)if(!cached.mesh.Alive()){slot=&cached;break;}
    if(!slot){Log(L"HEAD UNSUPPORTED reason=rig-cache-capacity mesh=%ls",*mesh->GetFullPath());return nullptr;}
    slot->mesh.Set(mesh);slot->player=player;
    for(const auto& profile:TestRigs) {
        if(profile.playerOnly!=player || profile.bones!=mesh->RefSkeleton.Count() ||
            profile.lods!=static_cast<unsigned>(mesh->LODModels.ArrayNum))continue;
        auto loaded=EngineCalls::LoadRig(speaker,profile.object);
        auto set=loaded ? loaded->Cast<UMorphTargetSet>() : nullptr;
        if(!IsLiveInstance(set) || !CompatibleRig(profile,player,mesh->RefSkeleton.Count(),mesh->LODModels.ArrayNum,
            mesh,set->BaseSkelMesh,set->Targets.Count(),facialChannelCount))continue;
        slot->set.Set(set);slot->rig=&profile;
        Log(L"HEAD RIG RESOLVED participant=%ls mesh=%ls rig=%ls bones=%u lods=%d basePointerMatch=1",
            *speaker->Tag.ToString(),*mesh->GetFullPath(),profile.object,mesh->RefSkeleton.Count(),mesh->LODModels.ArrayNum);
        return &profile;
    }
    Log(L"HEAD UNSUPPORTED reason=no-authored-compatible-rig participant=%ls mesh=%ls bones=%u lods=%d; vanilla retained",
        *speaker->Tag.ToString(),*mesh->GetFullPath(),mesh->RefSkeleton.Count(),mesh->LODModels.ArrayNum);
    return nullptr;
}

bool Bind(ABioPawn* speaker, USkeletalMeshComponent* head, UAudioComponent* audio, const TestRig& rig) {
    if (!activeLine) {
        Log(L"REFUSED: no exact selected dialogue line.");return false;
    }
    RenderState(head, L"before-bind");
    Log(L"BIND candidate pawn=%ls head=%ls mesh=%ls parent=%ls anim=%ls template=%ls lod=%d",
        *speaker->GetFullPath(), *head->GetFullPath(), *head->SkeletalMesh->GetFullPath(),
        head->ParentAnimComponent ? *head->ParentAnimComponent->GetFullPath() : L"null",
        head->Animations ? *head->Animations->GetFullPath() : L"null",
        head->AnimTreeTemplate ? *head->AnimTreeTemplate->GetFullPath() : L"null", head->PredictedLODLevel);
    if (!PrepareMaterials(speaker,head)) {
        Log(L"REFUSED: morph-compatible materials not verified; no morph graph installed."); return false;
    }
    auto loaded = EngineCalls::LoadRig(speaker, rig.object);
    auto set = loaded ? loaded->Cast<UMorphTargetSet>() : nullptr;
    if (!set || set->BaseSkelMesh != head->SkeletalMesh || set->Targets.Count() != facialChannelCount) {
        Log(L"REFUSED: missing rig, target count mismatch, or BaseSkelMesh pointer mismatch."); return false;
    }
    std::array<UMorphTarget*,facialChannelCount> targets{};
    for(unsigned i=0;i<facialChannelCount;++i){
        targets[i]=EngineCalls::Find(set,FacialChannels[i]);
        if(!targets[i] || targets[i]!=set->Targets(i)){Log(L"REFUSED: ordered A2F target unavailable: %ls",FacialChannels[i]);return false;}
    }
    auto target=targets[0];
    for (auto existing : head->MorphSets) if (existing == set) {
        Log(L"REFUSED: bridge set is already present without owned state."); return false;
    }
    headRef.Set(head); setRef.Set(set); targetRef.Set(target); audioRef.Set(audio);
    for(unsigned i=0;i<facialChannelCount;++i)facialTargetRefs[i].Set(targets[i]);
    parentRef.Set(head->ParentAnimComponent);
    head->MorphSets.Add(set); addedSet = true;
    if (!EngineCalls::Init(head)) { Log(L"REFUSED: InitMorphTargets unavailable."); Release(); return false; }
    auto tree = head->Animations ? head->Animations->Cast<UAnimTree>() : CreateHeadTree(head);
    if (!IsLiveInstance(tree) || tree == head->AnimTreeTemplate || tree->SkelComponent != head) {
        Log(L"REFUSED: no usable head AnimTree; restoring owned changes."); Release(); return false;
    }
    treeRef.Set(tree);
    auto nodeClass = UMorphNodeMultiPose::StaticClass();
    if (!nodeClass) { Log(L"REFUSED: MorphNodeMultiPose class missing."); Release(); return false; }
    auto node = static_cast<UMorphNodeMultiPose*>(constructObject(nodeClass, tree,
        SFXName(L"MEF_SL_BridgeNode", 0), static_cast<EObjectFlags>(0), nullptr,
        errorDevice ? *errorDevice : nullptr, nullptr, nullptr));
    if (!node) { Log(L"REFUSED: native node construction failed."); Release(); return false; }
    node->SkelComponent = head;
    node->NodeName = SFXName(L"MEF_SL_BridgeNode", 0);
    nodeRef.Set(node);
    tree->RootMorphNodes.Add(node);
    for(unsigned i=0;i<facialChannelCount;++i)if(!EngineCalls::Add(node,FacialChannels[i])){
        Log(L"REFUSED: AddMorphTarget returned false for %ls",FacialChannels[i]);Release();return false;
    }
    ownershipRoutes[0].component.Set(head);
    ownershipRoutes[1].component.Set(head->ParentAnimComponent);
    for(unsigned i=0;i<ownershipRoutes.size();++i)if(auto c=ownershipRoutes[i].component.As<USkeletalMeshComponent>()){
        Log(L"OWNERSHIP SOURCE actor=%ls source=%ls component=%ls mesh=%ls asset=%ls mapCount=%u atoms=%u",activeLine->actor,i?L"parent":L"head",*c->GetFullPath(),c->SkeletalMesh?*c->SkeletalMesh->GetFullPath():L"null",c->m_pBioFaceFXAsset?*c->m_pBioFaceFXAsset->GetFullPath():L"null",c->m_aBioFFXBoneMap.Count(),c->LocalAtoms.Count());
    }
    Log(L"BOUND: A2F MorphNodeMultiPose, %u channels, zero initial weights; scoped FaceFX mouth/cheek/brow output ownership; vanilla eyes/blink/body retained.",facialChannelCount);
    RenderState(head, L"after-bind-zero");
    return true;
}

UAudioComponent* MatchingAudio(ABioPawn* speaker, USkeletalMeshComponent* head, UBioConversation* conversation, const TestLine& line) {
    UAudioComponent* match = nullptr;
    UAudioComponent* candidates[] = {speaker->m_pFacialAudioComp, head->CachedFaceFXAudioComp, conversation->m_pNoFaceFXAudioComp};
    for (auto candidate : candidates) {
        if (!candidate || !candidate->SoundCue || candidate->bFinished ||
            !candidate->SoundCue->GetFullPath().Equals(line.cue)) continue;
        if (!EngineCalls::Playing(candidate) && candidate->nPauseCount <= 0) continue;
        if (match && match != candidate) { Log(L"REFUSED: multiple distinct matching audio instances."); return nullptr; }
        match = candidate;
    }
    return match;
}

void Update(UGameEngine* engine) {
    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
    const bool keyDown = foregroundPid == GetCurrentProcessId() && (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    if (keyDown && !f8Down && !restorationFault) { disabled = !disabled; Log(L"F8: bridge %ls", disabled ? L"disabled" : L"enabled"); }
    f8Down = keyDown;
    auto world = EngineCalls::World(engine);
    if (frame == 1) Log(L"WORLD: first world query completed; world=%p", static_cast<void*>(world));
    auto bioWorld = world ? world->Cast<ABioWorldInfo>() : nullptr;
    auto conversation = bioWorld ? bioWorld->m_oCurrentConversation : nullptr;
    bool eligible = false;
    ABioPawn* speaker = nullptr;
    USkeletalMeshComponent* head = nullptr;
    UAudioComponent* audio = nullptr;
    const TestLine* line = nullptr;
    const TestRig* rig = nullptr;
    if (!disabled && conversation && conversation->m_nResRefID == 205 &&
        conversation->GetFullPath().Equals(L"nor10_joker_D.nor10_joker_dlg") &&
        !conversation->m_bPlayerIsFemale && !conversation->m_bConversationEnded && !conversation->m_bSkipRequested &&
        conversation->m_pCurrentSound) {
        speaker = conversation->m_pSpeaker ? conversation->m_pSpeaker->Cast<ABioPawn>() : nullptr;
        head = speaker ? speaker->m_oHeadMesh : nullptr;
        for (auto const& test : SceneLines) {
            int text = 0, exportId = 0;
            if (!*test.participantTag) {
                if (conversation->m_ReplyList.Count() <= static_cast<unsigned>(test.entry) || speaker != conversation->m_pConversationPlayer) continue;
                text = conversation->m_ReplyList(test.entry).srText;
                exportId = conversation->m_ReplyList(test.entry).nExportID;
            } else {
                if (!MatchesParticipant(conversation,speaker,test) ||
                    conversation->m_EntryList.Count() <= static_cast<unsigned>(test.entry)) continue;
                text = conversation->m_EntryList(test.entry).srText;
                exportId = conversation->m_EntryList(test.entry).nExportID;
            }
            if (MatchesSceneLineIds(test, conversation->m_nResRefID, conversation->m_nCurrentEntry,
                text, exportId, *conversation->m_pCurrentSound->GetFullPath())) { line = &test; break; }
        }
        if (line && IsLiveInstance(head) && head->SkeletalMesh) {
            rig = ResolveRig(speaker,head,!*line->participantTag);
            audio = MatchingAudio(speaker, head, conversation, *line);
            eligible = rig && audio && head->PredictedLODLevel >= 0 && head->PredictedLODLevel <= rig->maxLOD;
        }
    }
    if (conversation && conversation->m_nResRefID == 205 && frame % 30 == 0) {
        if (line) Log(L"CANDIDATE actor=%ls knownRig=%d audioMatch=%d supportedLOD=%d", line->actor,
            rig != nullptr, audio != nullptr, rig && head && head->PredictedLODLevel >= 0 && head->PredictedLODLevel <= rig->maxLOD);
        Log(L"OBSERVE convo=%ls entry=%d reply=%d female=%d player=%ls speaker=%ls cue=%ls fx=%ls eligible=%d",
            *conversation->GetFullPath(), conversation->m_nCurrentEntry, conversation->m_nSelectedReply,
            conversation->m_bPlayerIsFemale,
            conversation->m_pConversationPlayer ? *conversation->m_pConversationPlayer->GetFullPath() : L"null",
            conversation->m_pSpeaker ? *conversation->m_pSpeaker->GetFullPath() : L"null",
            conversation->m_pCurrentSound ? *conversation->m_pCurrentSound->GetFullPath() : L"null",
            *conversation->m_sCurrentFaceFXAnim, eligible);
        auto player = conversation->m_pSpeaker ? conversation->m_pSpeaker->Cast<ABioPawn>() : nullptr;
        auto playerHead = player ? player->m_oHeadMesh : nullptr;
        if (playerHead) {
            Log(L"HEAD path=%ls mesh=%ls lod=%d anim=%ls parent=%ls facialAudio=%ls headAudio=%ls noFxAudio=%ls",
                *playerHead->GetFullPath(), playerHead->SkeletalMesh ? *playerHead->SkeletalMesh->GetFullPath() : L"null",
                playerHead->PredictedLODLevel, playerHead->Animations ? *playerHead->Animations->GetFullPath() : L"null",
                playerHead->ParentAnimComponent ? *playerHead->ParentAnimComponent->GetFullPath() : L"null",
                player->m_pFacialAudioComp && player->m_pFacialAudioComp->SoundCue ? *player->m_pFacialAudioComp->SoundCue->GetFullPath() : L"null",
                playerHead->CachedFaceFXAudioComp && playerHead->CachedFaceFXAudioComp->SoundCue ? *playerHead->CachedFaceFXAudioComp->SoundCue->GetFullPath() : L"null",
                conversation->m_pNoFaceFXAudioComp && conversation->m_pNoFaceFXAudioComp->SoundCue ? *conversation->m_pNoFaceFXAudioComp->SoundCue->GetFullPath() : L"null");
            Log(L"HEAD STATE template=%ls overrideParent=%d ignoreControllers=%d owner=%ls",
                playerHead->AnimTreeTemplate ? *playerHead->AnimTreeTemplate->GetFullPath() : L"null",
                playerHead->bOverrideParentSkeleton, playerHead->bIgnoreControllers,
                playerHead->Owner ? *playerHead->Owner->GetFullPath() : L"null");
        }
    }
    if (!eligible) { Release(); attempted = false; previouslyEligible = false; activeLine = nullptr; return; }
    if (activeLine != line) { Release(); attempted = false; previouslyEligible = false; activeLine = line; }
    if (!previouslyEligible) Log(L"LINE start: actor=%ls cue=%ls rig=%ls audio=%ls time=%.6f", line->actor, line->cue, rig->object, *audio->GetFullPath(), audio->PlaybackTime);
    previouslyEligible = true;
    if (headRef.pointer && (!headRef.Alive() || headRef.pointer != head || !treeRef.Alive() ||
        head->Animations != treeRef.pointer || !nodeRef.Alive() || !targetRef.Alive() || !audioRef.Alive() || audioRef.pointer != audio ||
        (ownedTemplateRef.pointer && (!ownedTemplateRef.Alive() || !parentRef.Alive() ||
            head->ParentAnimComponent != parentRef.pointer || head->AnimTreeTemplate != ownedTemplateRef.pointer)))) {
        Release(); // Do not repeatedly modify a graph the engine has replaced.
    }
    if (disabled) return;
    if (!headRef.pointer && !attempted) {
        attempted = true;
        if(Bind(speaker,head,audio,*rig))activeClip=&facialClips[static_cast<size_t>(line-SceneLines)];
    }
    auto node = nodeRef.As<UMorphNodeMultiPose>();
    auto target = targetRef.As<UMorphTarget>();
    if (!node || !target || !activeClip) return;
    const auto t = audio->PlaybackTime;
    if (!std::isfinite(t) || t < 0 || t > line->duration + .5f) { Log(L"REFUSED: invalid/out-of-range audio clock %.6f", t); Release(); return; }
    if (lastAudioTime >= 0 && t < lastAudioTime - 0.05f) Log(L"AUDIO restart/seek: %.6f -> %.6f", lastAudioTime, t);
    if(audio->nPauseCount<=0 && !activeClip->Sample(t,facialWeights.data(),facialChannelCount)){
        Log(L"REFUSED: invalid precomputed facial sample");Release();return;
    }
    if(audio->nPauseCount<=0){
        transitionAlpha=FaceTransition::Envelope(t,line->duration);
        transitionBrowAlpha=FaceTransition::Envelope(t,line->duration,.24f);
    }
    requestedWeight=0;
    for(unsigned i=0;i<facialChannelCount;++i){
        if(audio->nPauseCount<=0)facialWeights[i]*=(FaceTransition::BrowChannel(FacialChannels[i])?transitionBrowAlpha:transitionAlpha);
        auto owned=facialTargetRefs[i].As<UMorphTarget>();
        requestedWeight=std::max(requestedWeight,facialWeights[i]);
        if(!owned || !EngineCalls::Weight(node,owned,facialWeights[i])){Log(L"REFUSED: A2F channel update failed");Release();return;}
    }
    lastAudioTime = t;
}

void Tick(UGameEngine* engine, float delta) {
    if (busy) { originalTick(engine, delta); return; }
    busy = true; ++frame;
    if (frame == 1) gameThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    if (frame == 1) Log(L"TICK: first engine callback reached; engine=%p", static_cast<void*>(engine));
    Update(engine); // Candidate morph-graph route, before engine evaluation.
    originalTick(engine, delta);
    // Observe the result of the engine evaluator used by our pose hook.
    auto head = headRef.As<USkeletalMeshComponent>();
    auto target = targetRef.As<UMorphTarget>();
    if (head && target && frame % 4 == 0) {
        float active = 0; int found = 0;
        for(auto const& morph:head->ActiveMorphs)for(auto const& ref:facialTargetRefs)if(morph.Target==ref.pointer){active=std::max(active,morph.Weight);++found;break;}
        Log(L"A2F actor=%ls audio=%.6f peakRequested=%.4f peakObserved=%.4f activeChannels=%d lod=%d delta=%.6f",
            activeLine ? activeLine->actor : L"none", lastAudioTime, requestedWeight, active, found, head->PredictedLODLevel, delta);
        if(!activationReported && active>0 && found>0 && ownershipCoverage.Isolated(frame)){
            activationReported=true;
            Log(L"A2F ACTIVATION PROVEN actor=%ls observedPeak=%.4f activeTargets=%d ownershipRestores=%llu; runtime weight/ownership proof, visual quality still requires recording",activeLine?activeLine->actor:L"none",active,found,ownershipRestores);
        }
        if(!activationFailureReported && lastAudioTime>.5f && !poseApplications){
            activationFailureReported=true;
            Log(L"A2F ACTIVATION FAILED actor=%ls poseCalls=%llu withheld=%llu ownershipCalls=%llu refusals=%llu; this line is NOT a successful A2F test",activeLine?activeLine->actor:L"none",poseCallbacks,poseWithheld,ownershipCalls,ownershipRefusals);
        }
    }
    if (head && target && (poseCallbacks==1 || frame % 60 == 0)) RenderState(head, L"after-tick");
    busy = false;
}

void DiagnosticTick(UGameEngine* engine,float delta) {
    Tick(engine,delta);
    if(!busy && GetCurrentThreadId()==gameThread.load(std::memory_order_relaxed))FlushResourceTrace();
}

bool SupportedExe(const wchar_t* path) {
    // Reject another build before using SDK RVAs.
    FILE* f = nullptr; if (_wfopen_s(&f, path, L"rb") || !f) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    std::array<unsigned char, 65536> buffer{}; size_t n;
    while (ok && (n = fread(buffer.data(), 1, buffer.size(), f)) != 0)
        ok = BCryptHashData(hash, buffer.data(), static_cast<ULONG>(n), 0) >= 0;
    if (ferror(f)) ok = false;
    fclose(f);
    std::array<unsigned char, 32> digest{};
    if (ok) ok = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    wchar_t hex[65]{}; for (size_t i=0;i<digest.size();++i) swprintf_s(hex+i*2, 3, L"%02X", digest[i]);
    Log(L"Executable SHA256=%ls", hex);
    return ok && wcscmp(hex,L"13BE26ECC292EB62B9F3CBEE7EDB2743F1E64EFE88AEA09D7E1289A1C33C1031") == 0;
}
}

SPI_IMPLEMENT_ATTACH {
    wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    auto logPath = std::filesystem::path(exe).parent_path() / L"ASI" / L"MEFSingleLineBridge.log";
    // Permit live log inspection; the secure fopen variant denies sharing.
    logFile = _wfsopen(logPath.c_str(), L"w, ccs=UTF-8", _SH_DENYWR);
    if (!logFile) {
        OutputDebugStringW(L"MEF bridge: could not open diagnostic log; no hooks installed.\n");
        return true;
    }
    Log(L"MEFaces intro A2F %ls; SPI attach entered; precomputed GPU-authored clips; no gameplay inference; vanilla eyes/blink/body retained; scoped mouth/brow ownership.",pluginVersion);
    if (!SupportedExe(exe)) { Log(L"REFUSED: unsupported executable; no hooks installed."); return true; }
    for(size_t i=0;i<std::size(SceneLines);++i){
        auto path=std::filesystem::path(exe).parent_path()/L"ASI"/L"MEFIntroClips"/(L"VO_"+std::to_wstring(SceneLines[i].stringRef)+L"_M.mefc");
        if(!facialClips[i].Load(path,FacialSchemaHash,facialChannelCount,SceneClipHashes[i])){
            Log(L"REFUSED: missing/corrupt/mismatched clip %ls; no hooks installed.",path.c_str());return true;
        }
    }
    Log(L"A2F CLIPS READY: %u scene lines, %u channels; all file hashes/schema/endpoints verified.",static_cast<unsigned>(std::size(SceneLines)),facialChannelCount);
    LESDK::Initializer init{InterfacePtr, "MEFSingleLineBridge"};
    GMalloc = init.ResolveTyped<FMallocLike*>(BUILTIN_GMALLOC_RVA);
    UObject::GObjObjects = init.ResolveTyped<TArray<UObject*>>(BUILTIN_GOBJECTS_RVA);
    SFXName::GBioNamePools = init.ResolveTyped<SFXNameEntry const*>(BUILTIN_SFXNAMEPOOLS_RVA);
    SFXName::GInitMethod = init.ResolveTyped<SFXName::tInitMethod>(BUILTIN_SFXNAMEINIT_RVA);
    constructObject = init.ResolveTyped<ConstructFn>(BUILTIN_STATICCONSTRUCTOBJECT_RVA);
    errorDevice = init.ResolveTyped<void*>(BUILTIN_GERROR_RVA);
    auto tick = init.ResolveTyped<TickFn>(BUILTIN_GAMEENGINETICK_RVA);
    if (!GMalloc || !UObject::GObjObjects || !SFXName::GBioNamePools || !SFXName::GInitMethod || !constructObject || !tick) {
        Log(L"REFUSED: missing SDK resolution."); return true;
    }
    auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    moduleBase=module;
    auto pose = reinterpret_cast<PoseFn*>(module + NativeMorph::PoseRva);
    auto faceUpdate=reinterpret_cast<FaceUpdateFn*>(module+FaceOwnershipNative::RVA);
    auto faceRebuild=reinterpret_cast<FaceRebuildFn*>(module+FaceRebuildNative::RVA);
    evaluateMorph = reinterpret_cast<EvaluateFn*>(module + NativeMorph::EvaluateRva);
    if (!NativeMorph::Matches(pose, evaluateMorph)) {
        Log(L"REFUSED: native pose/evaluator code fingerprint mismatch; no hooks installed."); return true;
    }
    if(!FaceOwnershipNative::Matches(faceUpdate)){Log(L"REFUSED: full native FaceFX bone-output fingerprint mismatch; no hooks installed.");return true;}
    if(!FaceRebuildNative::Matches(faceRebuild)){Log(L"REFUSED: full native FaceFX map-rebuild fingerprint mismatch; no hooks installed.");return true;}
    auto componentUsage=reinterpret_cast<ComponentUsageFn*>(module+RenderTraceNative::ComponentUsageRva);
    auto instanceUsage=reinterpret_cast<InstanceUsageFn*>(module+RenderTraceNative::InstanceUsageRva);
    auto baseUsage=reinterpret_cast<BaseUsageFn*>(module+RenderTraceNative::BaseUsageRva);
    auto materialCopy=reinterpret_cast<MaterialCopyFn*>(module+RenderTraceNative::MaterialArrayCopyRva);
    auto instanceResource=reinterpret_cast<ResourceFn*>(module+ResourceTraceNative::InstanceResourceRva);
    auto baseResource=reinterpret_cast<ResourceFn*>(module+ResourceTraceNative::BaseResourceRva);
    if(!RenderTraceNative::Matches(componentUsage,RenderTraceNative::ComponentUsageBytes) ||
       !RenderTraceNative::Matches(instanceUsage,RenderTraceNative::InstanceUsageBytes) ||
       !RenderTraceNative::Matches(baseUsage,RenderTraceNative::BaseUsageBytes) ||
       !RenderTraceNative::Matches(materialCopy,RenderTraceNative::MaterialArrayCopyBytes) ||
       !RenderTraceNative::Matches(reinterpret_cast<void*>(module+RenderTraceNative::FallbackDecisionRva),RenderTraceNative::FallbackDecisionBytes) ||
       !RenderTraceNative::Matches(instanceResource,ResourceTraceNative::InstanceResourceBytes) ||
       !RenderTraceNative::Matches(baseResource,ResourceTraceNative::BaseResourceBytes)) {
        Log(L"REFUSED: native render-trace fingerprint mismatch; no hooks installed.");return true;
    }
    originalBaseResource=reinterpret_cast<ResourceFn*>(init.InstallHook("MEFTraceBaseResource",baseResource,TraceBaseResource));
    originalInstanceResource=reinterpret_cast<ResourceFn*>(init.InstallHook("MEFTraceInstanceResource",instanceResource,TraceInstanceResource));
    if(!originalBaseResource || !originalInstanceResource){Log(L"REFUSED: resource trace dependency hook failed; no intervention.");return true;}
    Log(L"RESOURCE TRACE READY: instance=84d350 base=200af0; original calls/results; eight samples per phase per game/other-thread group; render events drained on game thread; no extra resource calls or GPU state changes.");
    originalMaterialCopy=reinterpret_cast<MaterialCopyFn*>(init.InstallHook("MEFTraceMaterialCopy",materialCopy,TraceMaterialCopy));
    originalBaseUsage=reinterpret_cast<BaseUsageFn*>(init.InstallHook("MEFTraceBaseUsage",baseUsage,TraceBaseUsage));
    originalInstanceUsage=reinterpret_cast<InstanceUsageFn*>(init.InstallHook("MEFTraceInstanceUsage",instanceUsage,TraceInstanceUsage));
    if(!originalMaterialCopy || !originalBaseUsage || !originalInstanceUsage){Log(L"REFUSED: trace dependency hook failed; no intervention.");return true;}
    originalComponentUsage=reinterpret_cast<ComponentUsageFn*>(init.InstallHook("MEFTraceComponentUsage",componentUsage,TraceComponentUsage));
    if(!originalComponentUsage){Log(L"REFUSED: component trace hook failed; no intervention.");return true;}
    Log(L"RENDER TRACE READY: component=5b4d30 instance=8443b0 base=20b0b0 copy=595320; observed native results and selected command arrays; maximum six transitions per NPC; no render decisions overridden.");
    originalFaceRebuild=reinterpret_cast<FaceRebuildFn*>(init.InstallHook("MEFFacialMapLifecycle",faceRebuild,FaceRebuild));
    if(!originalFaceRebuild){Log(L"REFUSED: facial map lifecycle hook failed; no intervention.");return true;}
    Log(L"OWNERSHIP LIFECYCLE READY: guarded589ca0 restores before native map resize/rebuild and validates/remasks same-actor output; no stale-array restoration.");
    originalFaceUpdate=reinterpret_cast<FaceUpdateFn*>(init.InstallHook("MEFFacialOwnership",faceUpdate,FaceUpdate));
    if(!originalFaceUpdate){Log(L"REFUSED: facial ownership hook failed; no intervention.");return true;}
    originalPose = reinterpret_cast<PoseFn*>(init.InstallHook("MEFHeadPose", pose, Pose));
    if (!originalPose) { Log(L"REFUSED: pose hook failed; no intervention."); return true; }
    Log(L"NATIVE READY: verified pose RVA=592440, evaluator RVA=438d40; pose hook installed.");
    originalTick = reinterpret_cast<TickFn*>(init.InstallHook("MEFBridgeTick", tick, DiagnosticTick));
    Log(originalTick ? L"READY: tick hook installed; F8 toggles intervention." : L"REFUSED: hook installation failed.");
    return true;
}

// Required by LEBinkProxy's LoadConditionalProcs even though shutdown delivery
// is not guaranteed. Never touch UE objects during loader/process teardown.
// Runtime cleanup uses Release() on the game thread; hot-unload is unsupported.
SPI_IMPLEMENT_DETACH {
    (void)InterfacePtr;
    return true;
}


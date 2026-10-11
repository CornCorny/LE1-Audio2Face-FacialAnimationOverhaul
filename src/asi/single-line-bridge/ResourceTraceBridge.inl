// Included inside Bridge.cpp's namespace after TraceRead/TraceObject.
// Only Arm/Flush use UObject APIs, on the established game thread. Native
// resource hooks collect pointer values with safe reads, retaining all results.
using ResourceFn=std::uintptr_t(void*,void*);
static_assert(offsetof(UMaterialInstance,Resources)==0xc4 && offsetof(UMaterialInstance,StaticPermutationResources)==0xb4);
static_assert(offsetof(UMaterialInstance,Parent)==0xec && offsetof(UMaterial,MaterialResources)==0xa4 && offsetof(UMaterial,DefaultMaterialInstances)==0xb4);
ResourceFn* originalInstanceResource=nullptr;
ResourceFn* originalBaseResource=nullptr;
struct ResourceWatch {
    std::array<std::atomic<std::uintptr_t>,2> roots{};
    std::atomic<ULONGLONG> until{0};
    // Game-thread queries cannot spend the renderer's observation budget.
    std::array<ResourceTrace::Gate,2> gates;
};
std::array<ResourceWatch,2> resourceWatches;
ResourceTrace::Queue<ResourceTrace::Record,16> resourceRecords;
thread_local ResourceTrace::Record* currentResourceTrace=nullptr;
thread_local unsigned resourceDepth=0;

std::uintptr_t ResourceCall(void* proxy,void* context,ResourceFn* original,unsigned kind,std::uintptr_t caller) {
    const auto entryError=GetLastError();
    // Do not clear a multi-kilobyte record on the global unsampled hot path.
    std::optional<ResourceTrace::Record> local;
    auto trace=currentResourceTrace;
    bool root=false;
    if(!trace && kind==0) {
        const auto address=reinterpret_cast<std::uintptr_t>(proxy);
        for(unsigned actor=0;actor<resourceWatches.size();++actor) {
            auto& watch=resourceWatches[actor];
            if(address!=watch.roots[0].load(std::memory_order_acquire) && address!=watch.roots[1].load(std::memory_order_acquire))continue;
            if(GetTickCount64()>watch.until.load(std::memory_order_acquire))break;
            unsigned phase=0,ordinal=0;
            const unsigned lane=GetCurrentThreadId()==gameThread.load(std::memory_order_relaxed) ? 0 : 1;
            if(!watch.gates[lane].Claim(phase,ordinal))break;
            local.emplace();local->actor=actor;local->phase=phase;local->ordinal=ordinal;local->thread=GetCurrentThreadId();local->milliseconds=GetTickCount64();
            trace=&*local;currentResourceTrace=trace;root=true;break;
        }
    }
    if(!trace) {SetLastError(entryError);return original(proxy,context);}
    const unsigned depth=resourceDepth++;
    ResourceTrace::Event* event=nullptr;
    if(trace->count<trace->events.size()) {
        event=&trace->events[trace->count++];event->kind=kind;event->depth=depth;event->caller=caller;
        event->proxy=reinterpret_cast<std::uintptr_t>(proxy);
        if(TraceRead(event->proxy,&event->vtable,8))event->reads|=1;
        if(TraceRead(event->proxy+(kind==0 ? 0xf0 : 0xe0),&event->owner,8))event->reads|=2;
        if(kind==0) {
            if(TraceRead(event->proxy+0xe8,&event->parent,8))event->reads|=4;
            if(event->owner && TraceRead(event->owner+0xf4,&event->flags,4))event->reads|=8;
            if(TraceRead(moduleBase+ResourceTraceNative::QualityGlobalRva,&event->quality,4))event->reads|=16;
            if(event->owner && event->quality>=0 && event->quality<2 && TraceRead(event->owner+0xb4+8*event->quality,&event->candidate,8)) {
                event->reads|=32;
                if(event->candidate && TraceRead(event->candidate+0x74,&event->candidate74,8))event->reads|=64;
            }
        }
    } else trace->overflow=true;
    SetLastError(entryError);
    const auto result=original(proxy,context);
    const auto exitError=GetLastError();
    if(event) {
        event->result=result;
        if(result && TraceRead(result+0x74,&event->result74,8))event->reads|=128;
    }
    --resourceDepth;
    if(root) {currentResourceTrace=nullptr;resourceRecords.Push(*local);}
    SetLastError(exitError);return result;
}
std::uintptr_t TraceInstanceResource(void* proxy,void* context) {
    return ResourceCall(proxy,context,originalInstanceResource,0,reinterpret_cast<std::uintptr_t>(_ReturnAddress())-moduleBase);
}
std::uintptr_t TraceBaseResource(void* proxy,void* context) {
    return ResourceCall(proxy,context,originalBaseResource,1,reinterpret_cast<std::uintptr_t>(_ReturnAddress())-moduleBase);
}
void LogResourcePointer(unsigned actor,unsigned phase,UObject* owner,const wchar_t* role,unsigned slot,std::uintptr_t pointer) {
    std::uintptr_t field74=0,vtable=0;
    const bool tableRead=pointer && TraceRead(pointer,&vtable,8);
    const bool fieldRead=pointer && TraceRead(pointer+0x74,&field74,8);
    Log(L"RESOURCE INVENTORY actor=%ls phase=%u ownerPtr=%p owner=%ls role=%ls slot=%u pointer=%p vtableRVA=%llx field74=%p tableRead=%d fieldRead=%d",
        actor==0 ? L"Kaidan" : L"Joker",phase,owner,owner ? *owner->GetFullPath() : L"null",role,slot,
        reinterpret_cast<void*>(pointer),tableRead ? vtable-moduleBase : 0,reinterpret_cast<void*>(field74),tableRead,fieldRead);
}
void InventoryMaterial(unsigned actor,unsigned phase,UMaterialInterface* material,std::array<UMaterialInterface*,64>& seen,unsigned& count) {
    if(!material || !IsLiveInstance(material))return;
    for(unsigned i=0;i<count;++i)if(seen[i]==material)return;
    if(count>=seen.size()){Log(L"RESOURCE INVENTORY OVERFLOW actor=%u phase=%u",actor,phase);return;}
    seen[count++]=material;
    if(auto instance=material->Cast<UMaterialInstance>()) {
        Log(L"RESOURCE INSTANCE actor=%ls phase=%u objectPtr=%p object=%ls parentPtr=%p static=%d dirty=%d",
            actor==0 ? L"Kaidan" : L"Joker",phase,instance,*instance->GetFullPath(),instance->Parent,instance->bHasStaticPermutationResource,instance->bStaticPermutationDirty);
        for(unsigned slot=0;slot<2;++slot) {
            std::uintptr_t pointer=0;
            TraceRead(reinterpret_cast<std::uintptr_t>(instance)+0xc4+8*slot,&pointer,8);LogResourcePointer(actor,phase,instance,L"instance-proxy",slot,pointer);
            TraceRead(reinterpret_cast<std::uintptr_t>(instance)+0xb4+8*slot,&pointer,8);LogResourcePointer(actor,phase,instance,L"static-permutation",slot,pointer);
        }
        InventoryMaterial(actor,phase,instance->Parent,seen,count);
    } else if(auto base=material->Cast<UMaterial>()) {
        for(unsigned slot=0;slot<2;++slot) {
            std::uintptr_t pointer=0;
            TraceRead(reinterpret_cast<std::uintptr_t>(base)+0xa4+8*slot,&pointer,8);LogResourcePointer(actor,phase,base,L"material-resource",slot,pointer);
            TraceRead(reinterpret_cast<std::uintptr_t>(base)+0xb4+8*slot,&pointer,8);LogResourcePointer(actor,phase,base,L"base-proxy",slot,pointer);
        }
    } else if(auto user=material->Cast<URvrEffectsMaterialUser>()) {
        Log(L"RESOURCE EFFECT actor=%ls phase=%u objectPtr=%p object=%ls parentPtr=%p basePtr=%p",actor==0 ? L"Kaidan" : L"Joker",phase,user,*user->GetFullPath(),user->m_pParentMaterial,user->m_pBaseMaterial);
        InventoryMaterial(actor,phase,user->m_pParentMaterial,seen,count);InventoryMaterial(actor,phase,user->m_pBaseMaterial,seen,count);
    } else if(auto mux=material->Cast<URvrMaterialMultiplexor>()) {
        InventoryMaterial(actor,phase,mux->m_pDefaultMaterial,seen,count);
        if(mux->m_lstParents.Count()<=64)for(auto const& entry:mux->m_lstParents)InventoryMaterial(actor,phase,entry.m_pMaterial,seen,count);
    }
}
void ArmResourceTrace(unsigned actor,unsigned phase,USkeletalMeshComponent* head) {
    auto displayed=head->m_aEffectsMaterialMICs.Count()>2 ? head->m_aEffectsMaterialMICs(2) : nullptr;
    auto& watch=resourceWatches[actor];
    for(unsigned slot=0;slot<2;++slot) {
        std::uintptr_t pointer=0;
        if(displayed)TraceRead(reinterpret_cast<std::uintptr_t>(displayed)+0xc4+8*slot,&pointer,8);
        watch.roots[slot].store(pointer,std::memory_order_release);
    }
    watch.until.store(GetTickCount64()+10000,std::memory_order_release);
    for(auto& gate:watch.gates)gate.Arm(phase);
    Log(L"RESOURCE ARM actor=%ls phase=%u active=%u displayed=%p thread=%lu milliseconds=%llu roots=%p,%p; phase labels game-thread trigger, not a render fence",
        actor==0 ? L"Kaidan" : L"Joker",phase,head->ActiveMorphs.Count(),displayed,GetCurrentThreadId(),GetTickCount64(),
        reinterpret_cast<void*>(watch.roots[0].load()),reinterpret_cast<void*>(watch.roots[1].load()));
    std::array<UMaterialInterface*,64> seen{};unsigned count=0;
    InventoryMaterial(actor,phase,displayed,seen,count);
    // The default's resource identities distinguish a late fallback even when
    // the earlier scene-proxy material list retains the intended face MIC.
    std::uintptr_t engine=0,defaultMaterial=0;
    TraceRead(moduleBase+RenderTraceNative::EngineGlobalRva,&engine,8);
    if(engine)TraceRead(engine+0x620,&defaultMaterial,8);
    if(auto object=TraceObject(defaultMaterial))InventoryMaterial(actor,phase,object->Cast<UMaterialInterface>(),seen,count);
    auto cache=UObject::FindObject<UObject>(L"ShaderCache MEFFaceShaders.SeekFreeShaderCache");
    Log(L"RESOURCE CACHE actor=%ls phase=%u objectPtr=%p live=%d lookup=class-qualified",actor==0 ? L"Kaidan" : L"Joker",phase,cache,IsLiveInstance(cache));
}
void FlushResourceTrace() {
    resourceRecords.Drain([](const ResourceTrace::Record& record) {
        Log(L"RESOURCE TRACE BEGIN actor=%ls phase=%u ordinal=%u thread=%u milliseconds=%llu events=%u overflow=%d",
            record.actor==0 ? L"Kaidan" : L"Joker",record.phase,record.ordinal,record.thread,record.milliseconds,record.count,record.overflow);
        for(unsigned i=0;i<record.count;++i) {
            const auto& e=record.events[i];auto owner=TraceObject(e.owner);auto parent=TraceObject(e.parent);
            Log(L"RESOURCE TRACE EVENT index=%u kind=%ls depth=%u proxy=%p vtableRVA=%llx ownerPtr=%p owner=%ls parentPtr=%p parent=%ls flags=%x quality=%d candidate=%p candidate74=%p result=%p result74=%p callerRVA=%llx reads=%x",
                i,e.kind==0 ? L"instance" : L"base",e.depth,reinterpret_cast<void*>(e.proxy),e.vtable ? e.vtable-moduleBase : 0,
                reinterpret_cast<void*>(e.owner),owner ? *owner->GetFullPath() : L"unresolved",reinterpret_cast<void*>(e.parent),parent ? *parent->GetFullPath() : L"none",
                e.flags,e.quality,reinterpret_cast<void*>(e.candidate),reinterpret_cast<void*>(e.candidate74),reinterpret_cast<void*>(e.result),reinterpret_cast<void*>(e.result74),e.caller,e.reads);
        }
        Log(L"RESOURCE TRACE END actor=%ls",record.actor==0 ? L"Kaidan" : L"Joker");
    });
    const auto dropped=resourceRecords.dropped.exchange(0,std::memory_order_relaxed);
    if(dropped)Log(L"RESOURCE TRACE DROPPED count=%u",dropped);
}

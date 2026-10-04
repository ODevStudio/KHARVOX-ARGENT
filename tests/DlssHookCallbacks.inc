using DlssResult=argent::dlss::Result;
void* const leftHistory=reinterpret_cast<void*>(uintptr_t(0x120));
void* const rightHistory=reinterpret_cast<void*>(uintptr_t(0x220));
void* const priorRight=reinterpret_cast<void*>(uintptr_t(0x320));
void* const nativeParameters=reinterpret_cast<void*>(uintptr_t(0x420));
const auto commandBuffer=reinterpret_cast<VkCommandBuffer>(uintptr_t(0x520));
unsigned nativeCreates{},nativeReleases{},nativeEvaluations{};
unsigned throwEvaluationAt{};
DlssResult leftCreation{1},rightCreation{1},leftEvaluation{1},rightEvaluation{1},leftRelease{1},rightRelease{1};
bool nullLeft{},nullRight{},aliasRight{},armCreationOom{},throwNativeCreate{},throwNativeRelease{},throwNativeEvaluate{};
std::array<void*,4> released{};
std::array<void*,2> evaluated{};
std::array<argent::dlss::Parameters,2> evaluatedParameters{};
const argent::dlss::Parameters* originalParameters{};
DlssResult __fastcall nativeCreate(VkCommandBuffer command,uint32_t,void* parameters,void** output){
    check(command==commandBuffer&&parameters==nativeParameters,"Creation changed native arguments");++nativeCreates;if(throwNativeCreate)throw std::bad_alloc{};
    if(output)*output=nativeCreates==1?(nullLeft?nullptr:leftHistory):(nullRight?nullptr:aliasRight?leftHistory:rightHistory);
    if(nativeCreates==2&&armCreationOom)failAfter=1;return nativeCreates==1?leftCreation:rightCreation;
}
DlssResult __fastcall nativeRelease(void* handle){
    check(nativeReleases<released.size(),"Release called more than expected");released[nativeReleases++]=handle;if(throwNativeRelease)throw std::bad_alloc{};
    return handle==leftHistory?leftRelease:rightRelease;
}
DlssResult __fastcall nativeEvaluate(VkCommandBuffer command,void* handle,void* parameters,const argent::dlss::Parameters* input){
    check(command==commandBuffer&&parameters==nativeParameters,"Evaluation changed native arguments");check(nativeEvaluations<evaluated.size(),"Evaluation called more than expected");
    evaluated[nativeEvaluations]=handle;if(input)evaluatedParameters[nativeEvaluations]=*input;++nativeEvaluations;if(throwNativeEvaluate||nativeEvaluations==throwEvaluationAt)throw std::bad_alloc{};
    if(input&&input!=originalParameters){
        for(unsigned i=0;i<4;++i){const auto resource=input->get<const argent::dlss::Resource*>(argent::dlss::resourceOffsets[i]);check(resource&&resource!=originalParameters->get<const argent::dlss::Resource*>(argent::dlss::resourceOffsets[i])&&resource->range.baseArrayLayer==nativeEvaluations-1,"Evaluation borrowed the wrong eye resource");}
    }
    return handle==leftHistory?leftEvaluation:rightEvaluation;
}
void checkFallback(const Requests& requests,bool expected){
    check(argent::dlss::failed()==expected,"Callback changed fallback state");check(requests.written()==(expected&&!argent::cleanRelease),"Callback diagnostics changed the AA request");
}
void runCallback(std::string_view scenario,const Requests& requests){
    using namespace argent::dlss;
    const auto split=scenario.rfind('-');const auto kind=scenario.substr(9,split-9),mode=scenario.substr(split+1);
    throwLogs=mode=="log";const bool oom=mode=="oom";
    createOriginal=&nativeCreate;releaseOriginal=&nativeRelease;evaluateOriginal=&nativeEvaluate;
    if(kind=="create-native-throw"||kind=="release-native-throw"||kind=="evaluate-native-throw"){
        throwNativeCreate=kind=="create-native-throw";throwNativeRelease=kind=="release-native-throw";throwNativeEvaluate=kind=="evaluate-native-throw";
        bool propagated{};void* output{};try{
            if(throwNativeCreate)create(commandBuffer,1,nativeParameters,&output);
            else if(throwNativeRelease)release(leftHistory);
            else evaluate(commandBuffer,leftHistory,nativeParameters,nullptr);
        }catch(const std::bad_alloc&){propagated=true;}
        check(propagated&&nativeCreates+nativeReleases+nativeEvaluations==1,"Adapter swallowed or retried a native exception");return;
    }
    if(kind=="create"||kind=="duplicate"||kind=="right-error"||kind=="right-null"||kind=="right-alias"||kind=="left-error"||kind=="left-null"||kind=="output-null"||kind=="other-feature"||kind=="registration-cleanup-error"){
        if(kind=="duplicate")pairs.emplace(leftHistory,Pair{priorRight});
        if(kind=="right-error")rightCreation=9;
        if(kind=="right-null")nullRight=true;
        if(kind=="right-alias")aliasRight=true;
        if(kind=="left-error")leftCreation=8;
        if(kind=="left-null")nullLeft=true;
        if(kind=="registration-cleanup-error"){rightRelease=9;armCreationOom=true;}
        armCreationOom=armCreationOom||oom;void* output{};
        const auto result=create(commandBuffer,kind=="other-feature"?2:1,nativeParameters,kind=="output-null"?nullptr:&output);failAfter=-1;
        check(result==leftCreation&&(kind=="output-null"||output==(nullLeft?nullptr:leftHistory)),"Creation changed the successful native left output");
        const bool two=kind!="left-error"&&kind!="left-null"&&kind!="output-null"&&kind!="other-feature";
        check(nativeCreates==(two?2u:1u),"Creation changed native feature call count");
        const bool abandoned=kind=="duplicate"||(kind=="create"&&oom)||kind=="registration-cleanup-error";
        if(abandoned)check(nativeReleases==1&&released[0]==rightHistory,"Failed registration leaked the owned right history");
        else check(!nativeReleases,"Creation released an unowned or published history");
        if(kind=="duplicate")check(pairs.size()==1&&pairs.at(leftHistory).right==priorRight,"Duplicate registration replaced the existing history");
        else if(kind=="create"&&!oom)check(pairs.size()==1&&pairs.at(leftHistory).right==rightHistory,"Creation diagnostic lost published stereo history");
        else check(pairs.empty(),"Rejected creation published an invalid history");
        checkFallback(requests,two&&(abandoned||kind!="create"));
    }else if(kind=="release"||kind=="release-left-error"||kind=="release-right-error"){
        pairs.emplace(leftHistory,Pair{rightHistory});if(kind=="release-left-error")leftRelease=7;if(kind=="release-right-error")rightRelease=9;if(oom)failAfter=1;
        const auto result=release(leftHistory);failAfter=-1;
        check(result==leftRelease&&pairs.empty()&&nativeReleases==2&&released[0]==rightHistory&&released[1]==leftHistory,"Release diagnostic skipped native left release or changed its result");checkFallback(requests,kind=="release-right-error");
    }else {
        Parameters input;std::array<Resource,4> resources{};
        for(unsigned i=0;i<4;++i){resources[i].view=reinterpret_cast<VkImageView>(uintptr_t(10+i));resources[i].image=reinterpret_cast<VkImage>(uintptr_t(20+i));input.set<const Resource*>(resourceOffsets[i],&resources[i]);}
        input.set<uint32_t>(0x30,100);input.set<uint32_t>(0x34,200);const auto bytes=input.bytes;originalParameters=&input;resolvedPose.serial=4;
        if(kind!="missing")pairs.emplace(leftHistory,Pair{rightHistory});
        if(kind.rfind("recovery-",0)==0){
            const auto failure=kind.substr(9);auto& pair=pairs.at(leftHistory);
            pair.seen=true;pair.serial=3;pair.tick=testTick();pair.samples=17;
            const bool nativeException=failure=="left-throw"||failure=="right-throw";
            if(failure=="left-error")leftEvaluation=8;
            if(failure=="right-error")rightEvaluation=9;
            if(failure=="resolve-error")resolveReady=false;
            if(failure=="resolve-exception")throwResolve=true;
            if(nativeException)throwEvaluationAt=failure=="left-throw"?1:2;
            DlssResult result{};bool escaped{};
            try{result=evaluate(commandBuffer,leftHistory,nativeParameters,failure=="null-input"?nullptr:&input);}catch(const std::bad_alloc&){escaped=true;}
            check(escaped==nativeException,"History recovery changed native exception propagation");
            check(nativeException||result==(failure=="right-error"?rightEvaluation:leftEvaluation),"History recovery changed native failure result");
            check(nativeEvaluations==((failure=="right-error"||failure=="right-throw")?2u:1u),"History failure changed native eye call count");
            check(!pair.seen&&pair.serial==3&&pair.samples==17,"Partial evaluation retained a synchronized history marker");
            checkFallback(requests,!nativeException);
            nativeEvaluations=throwEvaluationAt=0;leftEvaluation=rightEvaluation=1;resolveReady=true;throwResolve=false;
            check(evaluate(commandBuffer,leftHistory,nativeParameters,&input)==success&&nativeEvaluations==2,"Stereo histories did not recover after evaluation failure");
            check(evaluatedParameters[0].get<int>(0x38)==1&&evaluatedParameters[1].get<int>(0x38)==1,"Recovered evaluation did not reset both eye histories");
            check(pair.seen&&pair.serial==4&&pair.tick==testTick()&&pair.samples==18&&input.bytes==bytes,"Recovered evaluation changed caller parameters or lost temporal state");
            checkFallback(requests,!nativeException);return;
        }
        const Parameters* supplied=&input;
        if(kind=="null-input")supplied=nullptr;
        if(kind=="resources-invalid"){resources[0].type=1;}
        if(kind=="resolve-error")resolveReady=false;
        if(kind=="resolve-exception")throwResolve=true;
        if(kind=="left-eval-error")leftEvaluation=8;
        if(kind=="right-eval-error")rightEvaluation=9;
        if(kind=="window"){
            auto& pair=pairs.at(leftHistory);pair.samples=239;pair.windowSamples=10;pair.cpuNs=1000;pair.maxNs=100;pair.seen=true;pair.serial=3;pair.tick=testTick();
        }
        if(oom)failAfter=1;const auto result=evaluate(commandBuffer,leftHistory,nativeParameters,supplied);failAfter=-1;
        const bool mono=kind=="missing"||kind=="null-input"||kind=="resources-invalid"||kind=="resolve-error"||kind=="resolve-exception";
        const bool nativeError=kind=="left-eval-error"||kind=="right-eval-error";
        check(result==(kind=="right-eval-error"?rightEvaluation:leftEvaluation),"Evaluation diagnostic changed the native result");
        check(nativeEvaluations==(mono||kind=="left-eval-error"?1u:2u)&&evaluated[0]==leftHistory&&(nativeEvaluations==1||evaluated[1]==rightHistory),"Evaluation changed native eye order or fallback count");
        if(!mono){
            const auto& pair=pairs.at(leftHistory);check(pair.seen==!nativeError,"Evaluation published history after a native failure");
            if(!nativeError)check(pair.serial==4&&pair.tick==testTick()&&pair.samples==(kind=="window"?240u:1u),"Evaluation diagnostic lost temporal state");
            if(!nativeError&&argent::extendedLogging())check(!pair.windowSamples&&!pair.cpuNs&&!pair.maxNs,"Diagnostic failure retained the timing report window");
            check(evaluatedParameters[0].get<int>(0x38)==(kind=="window"?0:1),"Evaluation changed the temporal reset contract");
        }
        check(input.bytes==bytes,"Evaluation changed caller-owned parameters");checkFallback(requests,mono||nativeError);
    }
    if(throwLogs)check(logFailures>0,"Callback logging fault was not exercised");
    if(oom)check(allocationFailures==1,"Callback allocation fault was not exercised");
}

// Offline authoring only. NVIDIA inference and blendshape solve both use GPU.
#include "audio2face/audio2face.h"
#include "audio2emotion/audio2emotion.h"
#include <sstream>
#include <array>
#include <cmath>
#include "audio2x/cuda_utils.h"
#include "AudioFile.h"
#include <cuda_runtime_api.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <stdexcept>
struct Destroyer { template<class T> void operator()(T* p) const {if(p)p->Destroy();} };
template<class T> using Ptr=std::unique_ptr<T,Destroyer>;
void Check(std::error_code e){if(e)throw std::runtime_error(e.message());}
struct Frame {std::int64_t timestamp;std::vector<float> weights;};
struct Capture {std::vector<Frame> frames;};
bool Collect(void* p,const nva2f::IBlendshapeExecutor::DeviceResults& r){
    auto& c=*static_cast<Capture*>(p);Frame f{r.timeStampCurrentFrame,std::vector<float>(r.weights.Size())};
    if(cudaMemcpyAsync(f.weights.data(),r.weights.Data(),f.weights.size()*sizeof(float),cudaMemcpyDeviceToHost,r.cudaStream)!=cudaSuccess || cudaStreamSynchronize(r.cudaStream)!=cudaSuccess)throw std::runtime_error("GPU result copy failed");
    c.frames.push_back(std::move(f));return true;
}

const std::array<const char*,10> EmotionNames={"amazement","anger","cheekiness","disgust","fear","grief","joy","outofbreath","pain","sadness"};
std::vector<std::string> Split(const std::string& row,char delimiter){
    std::vector<std::string> result;std::stringstream stream(row);std::string item;
    while(std::getline(stream,item,delimiter))result.push_back(item);return result;
}
void FeedEmotion(nva2x::IEmotionAccumulator& emo,cudaStream_t stream,const std::string& path){
    if(path.empty() || path=="-"){
        std::vector<float> zero(emo.GetEmotionSize(),0.f);
        Check(emo.Accumulate(0,nva2x::HostTensorFloatConstView{zero.data(),zero.size()},stream));
    }else{
        std::ifstream input(path);if(!input)throw std::runtime_error("Missing emotion CSV: "+path);
        std::string row;std::getline(input,row);auto names=Split(row,',');
        if(names.size()!=11||emo.GetEmotionSize()!=10)throw std::runtime_error("Emotion dimensions mismatch");
        for(size_t i=0;i<10;++i)if(names[i+1]!=EmotionNames[i])throw std::runtime_error("Emotion schema mismatch");
        std::int64_t previous=-1;
        while(std::getline(input,row)){
            auto fields=Split(row,',');if(fields.size()!=11)throw std::runtime_error("Bad emotion row");
            auto t=std::stoll(fields[0]);if(t<0||t<=previous)throw std::runtime_error("Bad emotion timestamp");previous=t;
            std::array<float,10> values{};
            for(size_t i=0;i<10;++i){values[i]=std::stof(fields[i+1]);if(!std::isfinite(values[i])||values[i]<0||values[i]>1)throw std::runtime_error("Bad emotion weight");}
            Check(emo.Accumulate(t,nva2x::HostTensorFloatConstView{values.data(),values.size()},stream));
        }
        if(previous<0)throw std::runtime_error("Empty emotion CSV");
    }
    Check(emo.Close());
}
bool CollectEmotion(void* p,const nva2e::IEmotionExecutor::Results& r){
    auto& c=*static_cast<Capture*>(p);Frame f{r.timeStampCurrentFrame,std::vector<float>(r.emotions.Size())};
    if(cudaMemcpyAsync(f.weights.data(),r.emotions.Data(),f.weights.size()*sizeof(float),cudaMemcpyDeviceToHost,r.cudaStream)!=cudaSuccess || cudaStreamSynchronize(r.cudaStream)!=cudaSuccess)throw std::runtime_error("Emotion GPU copy failed");
    c.frames.push_back(std::move(f));return true;
}
bool CollectGeometry(void* p,const nva2f::IGeometryExecutor::Results& r){
    auto& out=*static_cast<std::ofstream*>(p);std::vector<float> data(r.skinGeometry.Size());
    if(cudaMemcpyAsync(data.data(),r.skinGeometry.Data(),data.size()*sizeof(float),cudaMemcpyDeviceToHost,r.skinCudaStream)!=cudaSuccess || cudaStreamSynchronize(r.skinCudaStream)!=cudaSuccess)throw std::runtime_error("Geometry GPU copy failed");
    const std::int64_t time=r.timeStampCurrentFrame;const std::uint32_t size=static_cast<std::uint32_t>(data.size());
    for(float v:data)if(!std::isfinite(v))throw std::runtime_error("Nonfinite source geometry");
    out.write(reinterpret_cast<const char*>(&time),8);out.write(reinterpret_cast<const char*>(&size),4);out.write(reinterpret_cast<const char*>(data.data()),data.size()*4);
    if(!out)throw std::runtime_error("Geometry write failed");return true;
}
int main(int argc,char** argv){try{
    if(argc!=4)throw std::runtime_error("Usage: MEFA2FExport model.json batch.tsv regression|diffusion|geometry|emotion (TSV: audio, output, optional emotion CSV)");
    const std::string mode=argv[3];if(mode!="regression"&&mode!="diffusion"&&mode!="geometry"&&mode!="emotion")throw std::runtime_error("Unknown mode");
    Check(nva2x::SetCudaDeviceIfNeeded(0));cudaDeviceProp prop{};cudaGetDeviceProperties(&prop,0);std::cout<<"GPU: "<<prop.name<<"; mode="<<mode<<std::endl;
    std::ifstream batch(argv[2]);if(!batch)throw std::runtime_error("Missing batch");
    std::string row;while(std::getline(batch,row)){
        if(!row.empty()&&row.back()=='\r')row.pop_back();if(row.empty())continue;
        auto fields=Split(row,'\t');if(fields.size()<2||fields.size()>3)throw std::runtime_error("Invalid batch row");
        auto audio=fields[0],out=fields[1],emotion=fields.size()==3?fields[2]:"";
        AudioFile<float> wav;if(!wav.load(audio)||wav.getSampleRate()!=16000||wav.getNumChannels()!=1)throw std::runtime_error("Expected mono16k WAV: "+audio);
        Capture capture;std::vector<std::string> names;
        if(mode=="emotion"){
            Ptr<nva2x::ICudaStream> stream(nva2x::CreateCudaStream());
            Ptr<nva2x::IAudioAccumulator> acc(nva2x::CreateAudioAccumulator(16000,0));
            Ptr<nva2e::IClassifierModel::IEmotionModelInfo> info(nva2e::ReadClassifierModelInfo(argv[1]));
            if(!stream||!acc||!info)throw std::runtime_error("Cannot load emotion model");
            const nva2x::IAudioAccumulator* shared=acc.get();
            nva2e::EmotionExecutorCreationParameters params;params.cudaStream=stream->Data();params.nbTracks=1;params.sharedAudioAccumulators=&shared;
            auto classifier=info->GetExecutorCreationParameters(60000,30,1,0);
            classifier.postProcessParams.enablePreferredEmotion=false;
            Ptr<nva2e::IEmotionExecutor> executor(nva2e::CreateClassifierEmotionExecutor(params,classifier));
            if(!executor)throw std::runtime_error("Cannot create GPU emotion executor");
            Check(executor->SetResultsCallback(CollectEmotion,&capture));
            Check(acc->Accumulate(nva2x::HostTensorFloatConstView{wav.samples[0].data(),wav.samples[0].size()},stream->Data()));Check(acc->Close());
            while(nva2x::GetNbReadyTracks(*executor))Check(executor->Execute(nullptr));
            if(cudaStreamSynchronize(stream->Data())!=cudaSuccess)throw std::runtime_error("Emotion completion failed");
            names.assign(EmotionNames.begin(),EmotionNames.end());
        }else if(mode=="geometry"){
            Ptr<nva2f::IGeometryExecutorBundle> bundle(nva2f::ReadDiffusionGeometryExecutorBundle(1,argv[1],nva2f::IGeometryExecutor::ExecutionOption::Skin,2,true,nullptr));
            if(!bundle)throw std::runtime_error("Cannot create source geometry executor");
            std::ofstream binary(out,std::ios::binary);if(!binary)throw std::runtime_error("Cannot open geometry output");
            auto& executor=bundle->GetExecutor();Check(executor.SetResultsCallback(CollectGeometry,&binary));
            FeedEmotion(bundle->GetEmotionAccumulator(0),bundle->GetCudaStream().Data(),emotion);
            auto& acc=bundle->GetAudioAccumulator(0);Check(acc.Accumulate(nva2x::HostTensorFloatConstView{wav.samples[0].data(),wav.samples[0].size()},bundle->GetCudaStream().Data()));Check(acc.Close());
            while(nva2x::GetNbReadyTracks(executor))Check(executor.Execute(nullptr));
            if(cudaStreamSynchronize(bundle->GetCudaStream().Data())!=cudaSuccess)throw std::runtime_error("Geometry completion failed");
            std::cout<<audio<<": source geometry -> "<<out<<std::endl;continue;
        }else{
            Ptr<nva2f::IBlendshapeExecutorBundle> bundle(mode=="diffusion" ? nva2f::ReadDiffusionBlendshapeSolveExecutorBundle(1,argv[1],nva2f::IGeometryExecutor::ExecutionOption::Skin,true,2,true,nullptr,nullptr) : nva2f::ReadRegressionBlendshapeSolveExecutorBundle(1,argv[1],nva2f::IGeometryExecutor::ExecutionOption::Skin,true,60,1,nullptr,nullptr));
            if(!bundle)throw std::runtime_error("Failed to load GPU bundle");
            auto& executor=bundle->GetExecutor();if(executor.GetResultType()!=nva2f::IBlendshapeExecutor::ResultsType::DEVICE)throw std::runtime_error("GPU solve must return device results");
            nva2f::IBlendshapeSolver* solver=nullptr;Check(nva2f::GetExecutorSkinSolver(executor,0,&solver));if(!solver)throw std::runtime_error("Missing skin solver");
            Check(executor.SetResultsCallback(Collect,&capture));
            FeedEmotion(bundle->GetEmotionAccumulator(0),bundle->GetCudaStream().Data(),emotion);
            auto& acc=bundle->GetAudioAccumulator(0);Check(acc.Accumulate(nva2x::HostTensorFloatConstView{wav.samples[0].data(),wav.samples[0].size()},bundle->GetCudaStream().Data()));Check(acc.Close());
            while(nva2x::GetNbReadyTracks(executor))Check(executor.Execute(nullptr));Check(executor.Wait(0));
            for(int i=0;i<solver->NumBlendshapePoses();++i)names.push_back(solver->GetPoseName(i));
        }
        if(capture.frames.empty())throw std::runtime_error("No output frames");
        std::ofstream stream(out,std::ios::binary);if(!stream)throw std::runtime_error("Cannot create "+out);
        stream<<"timestamp";for(auto& n:names)stream<<','<<n;stream<<'\n';
        for(auto& f:capture.frames){if(f.weights.size()!=names.size())throw std::runtime_error("Output dimension mismatch");stream<<f.timestamp;for(float w:f.weights){if(!std::isfinite(w))throw std::runtime_error("Nonfinite output");stream<<','<<w;}stream<<'\n';}
        if(!stream)throw std::runtime_error("Write failed");std::cout<<audio<<": "<<capture.frames.size()<<" frames, "<<names.size()<<" channels -> "<<out<<std::endl;
    }return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}

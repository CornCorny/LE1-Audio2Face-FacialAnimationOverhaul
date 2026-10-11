#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <fstream>
#include <filesystem>
namespace FacialClip {
inline std::uint64_t Hash(const std::vector<unsigned char>& bytes){
    std::uint64_t h=14695981039346656037ull;
    for(auto v:bytes)h=(h^v)*1099511628211ull;
    return h;
}
struct Clip {
    std::uint32_t channels=0,sampleRate=0,audioSamples=0;
    std::vector<std::uint32_t> timestamps;
    std::vector<float> weights;
    bool Decode(const std::vector<unsigned char>& bytes,std::uint64_t schema,unsigned expectedChannels,std::uint64_t expectedHash){
        *this={};if(bytes.size()<32 || bytes.size()>64*1024*1024 || Hash(bytes)!=expectedHash)return false;
        auto word=[&](size_t p){std::uint32_t v=0;std::memcpy(&v,bytes.data()+p,4);return v;};
        std::uint64_t storedSchema=0;std::memcpy(&storedSchema,bytes.data()+24,8);
        const auto count=word(12),c=word(8),rate=word(16),samples=word(20);
        if(word(0)!=0x4346454d || word(4)!=1 || c!=expectedChannels || c==0 || c>64 || count<2 || count>120000 || rate!=16000 || samples==0 || samples>rate*600 || storedSchema!=schema)return false;
        if(bytes.size()!=32+static_cast<size_t>(count)*(4+static_cast<size_t>(c)*4))return false;
        std::vector<std::uint32_t> ts(count);std::vector<float> ws(static_cast<size_t>(count)*c);
        for(unsigned i=0;i<count;++i){
            const auto p=32+static_cast<size_t>(i)*(4+static_cast<size_t>(c)*4);ts[i]=word(p);
            if(ts[i]>samples || (i&&ts[i]<=ts[i-1]))return false;
            std::memcpy(ws.data()+static_cast<size_t>(i)*c,bytes.data()+p+4,c*4);
            for(unsigned j=0;j<c;++j){const float v=ws[static_cast<size_t>(i)*c+j];if(!std::isfinite(v)||v<0||v>1)return false;}
        }
        if(ts.front()!=0 || ts.back()!=samples)return false;
        for(unsigned j=0;j<c;++j)if(ws[j]!=0 || ws[(static_cast<size_t>(count)-1)*c+j]!=0)return false;
        channels=c;sampleRate=rate;audioSamples=samples;timestamps=std::move(ts);weights=std::move(ws);return true;
    }
    bool Load(const std::filesystem::path& path,std::uint64_t schema,unsigned channelsExpected,std::uint64_t expectedHash){
        std::ifstream in(path,std::ios::binary|std::ios::ate);if(!in)return false;auto size=in.tellg();
        if(size<32 || size>64*1024*1024)return false;
        std::vector<unsigned char> b(static_cast<size_t>(size));in.seekg(0);in.read(reinterpret_cast<char*>(b.data()),size);
        return in.good()&&Decode(b,schema,channelsExpected,expectedHash);
    }
    bool Sample(float seconds,float* output,unsigned count)const{
        if(!output || count!=channels || timestamps.size()<2 || !std::isfinite(seconds) || seconds<0)return false;
        const double sample=static_cast<double>(seconds)*sampleRate;
        if(sample>=audioSamples){std::fill_n(output,count,0.f);return true;}
        const auto next=std::upper_bound(timestamps.begin(),timestamps.end(),sample);
        const size_t b=static_cast<size_t>(next-timestamps.begin()),a=b?b-1:0;
        if(b>=timestamps.size())return false;
        const float blend=static_cast<float>((sample-timestamps[a])/(timestamps[b]-timestamps[a]));
        for(unsigned j=0;j<count;++j)output[j]=weights[a*channels+j]*(1-blend)+weights[b*channels+j]*blend;
        return true;
    }
};
}

#pragma once
#include "GeneratedFaceOwnership.hpp"
#include <array>
#include <cmath>
#include <algorithm>
#include <string_view>

namespace FaceTransition {
// Audio-relative: pauses and variable render rates do not advance the blend.
inline float Smooth(float x) { x=std::clamp(x,0.f,1.f);return x*x*(3.f-2.f*x); }
inline float Envelope(float time,float duration,float span=.16f) {
    if(!std::isfinite(time)||!std::isfinite(duration)||!std::isfinite(span)||duration<=0||span<=0)return 0;
    return std::min(Smooth(time/span),Smooth((duration-time)/span));
}
inline bool BrowBone(size_t i) {
    const std::wstring_view n=FaceOwnership::BoneNames[i];
    return n==L"brow_left"||n==L"brow_right"||n==L"outBrow_left"||n==L"outBrow_Right";
}
inline bool BrowChannel(std::wstring_view n) {
    return n.starts_with(L"MEF_A2F_brow")||n.starts_with(L"MEF_A2F_detailBrow");
}
template<class Atom> bool Valid(const Atom& a) {
    const auto& q=a.Rotation;const auto& p=a.Translation;
    float n=q.X*q.X+q.Y*q.Y+q.Z*q.Z+q.W*q.W;
    return std::isfinite(n)&&n>.5f&&n<1.5f&&std::isfinite(p.X)&&std::isfinite(p.Y)&&std::isfinite(p.Z)&&
        std::isfinite(a.Scale)&&a.Scale>0&&a.Scale<10;
}
template<class Atom> bool Capture(const Atom* atoms,unsigned count,
    const std::array<unsigned char,FaceOwnership::BoneCount>& map,
    std::array<Atom,FaceOwnership::BoneCount>& before) {
    if(!atoms||count==0||count>255)return false;
    std::array<bool,256> seen{};
    for(size_t i=0;i<map.size();++i)if(FaceOwnership::Owned[i]) {
        auto index=map[i];
        if(index==255||index>=count||seen[index]||!Valid(atoms[index]))return false;
        seen[index]=true;before[i]=atoms[index];
    }
    return true;
}
// Runs AFTER exactly one vanilla evaluation, with its map already restored.
// alpha=0 preserves that result; alpha=1 restores the pre-FaceFX input.
// Only the verified owned outputs are touched, and only after ALL validate.
template<class Atom> bool Blend(Atom* atoms,unsigned count,
    const std::array<unsigned char,FaceOwnership::BoneCount>& map,
    const std::array<Atom,FaceOwnership::BoneCount>& before,float alpha,float browAlpha=-1.f) {
    if(browAlpha==-1.f)browAlpha=alpha;
    if(!std::isfinite(alpha)||alpha<0||alpha>1)return false;
    if(!std::isfinite(browAlpha)||browAlpha<0||browAlpha>1)return false;
    std::array<Atom,FaceOwnership::BoneCount> after{},staged{};
    if(!Capture(atoms,count,map,after))return false;
    for(size_t i=0;i<map.size();++i)if(FaceOwnership::Owned[i]) {
        const float weight=BrowBone(i)?browAlpha:alpha;
        if(!Valid(before[i]))return false;
        if(weight==0){staged[i]=after[i];continue;}
        if(weight==1){staged[i]=before[i];continue;}
        auto a=after[i];const auto& b=before[i];
        const float sign=(a.Rotation.X*b.Rotation.X+a.Rotation.Y*b.Rotation.Y+
            a.Rotation.Z*b.Rotation.Z+a.Rotation.W*b.Rotation.W)<0?-1.f:1.f;
        auto lerp=[weight](float x,float y){return x+(y-x)*weight;};
        a.Rotation.X=lerp(a.Rotation.X,b.Rotation.X*sign);
        a.Rotation.Y=lerp(a.Rotation.Y,b.Rotation.Y*sign);
        a.Rotation.Z=lerp(a.Rotation.Z,b.Rotation.Z*sign);
        a.Rotation.W=lerp(a.Rotation.W,b.Rotation.W*sign);
        float norm=std::sqrt(a.Rotation.X*a.Rotation.X+a.Rotation.Y*a.Rotation.Y+
            a.Rotation.Z*a.Rotation.Z+a.Rotation.W*a.Rotation.W);
        if(!std::isfinite(norm)||norm<.001f)return false;
        a.Rotation.X/=norm;a.Rotation.Y/=norm;a.Rotation.Z/=norm;a.Rotation.W/=norm;
        a.Translation.X=lerp(a.Translation.X,b.Translation.X);
        a.Translation.Y=lerp(a.Translation.Y,b.Translation.Y);
        a.Translation.Z=lerp(a.Translation.Z,b.Translation.Z);
        a.Scale=lerp(a.Scale,b.Scale);
        if(!Valid(a))return false;
        staged[i]=a;
    }
    if(alpha>0||browAlpha>0)for(size_t i=0;i<map.size();++i)if(FaceOwnership::Owned[i])atoms[map[i]]=staged[i];
    return true;
}
}

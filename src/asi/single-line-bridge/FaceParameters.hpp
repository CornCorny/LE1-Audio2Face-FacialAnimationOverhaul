#pragma once
#include <vector>
#include <type_traits>
#include <algorithm>

// Leaf-to-root input. First occurrence wins, matching material inheritance.
// Pure helper also exercised with recorded-style layered texture parameters.
namespace FaceParameters {
template<class T, class Layers, class Name, class Equal>
bool Matches(const Layers& layers, const auto& actual, Name name, Equal equal) {
    std::vector<T> effective;
    for (const auto* layer:layers) for (const auto& value:*layer)
        if (std::none_of(effective.begin(),effective.end(),[&](const auto& old){return name(old)==name(value);})) effective.push_back(value);
    if (effective.size()!=actual.Count())return false;
    for (const auto& wanted:effective) {
        bool found=false;
        for (const auto& got:actual) if(name(wanted)==name(got)){found=equal(wanted,got);break;}
        if(!found)return false;
    }
    return true;
}
}

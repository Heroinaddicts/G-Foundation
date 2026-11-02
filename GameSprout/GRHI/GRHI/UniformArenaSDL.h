#pragma once
#include <cstdint>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <vector>

namespace GRHI {
// One render pass owns every immutable uniform snapshot referenced by its draws.
// Identical material/camera data shares storage; caller memory is never retained.
class UniformArenaSDL {
public:
    const std::vector<std::uint8_t>* Store(const void* source,std::size_t size) {
        const auto* bytes=static_cast<const std::uint8_t*>(source);
        std::size_t hash=size;
        for(std::size_t index=0;index<size;++index)hash=(hash^bytes[index])*std::size_t{16777619};
        const auto [first,last]=_Lookup.equal_range(hash);
        for(auto entry=first;entry!=last;++entry) {
            const auto& existing=_Bytes[entry->second];
            if(existing.size()==size && std::memcmp(existing.data(),bytes,size)==0)return &existing;
        }
        _Bytes.emplace_back(bytes,bytes+size);
        _Lookup.emplace(hash,_Bytes.size()-1);
        return &_Bytes.back();
    }
    std::size_t Size() const {return _Bytes.size();}
private:
    std::deque<std::vector<std::uint8_t>> _Bytes;
    std::unordered_multimap<std::size_t,std::size_t> _Lookup;
};
}

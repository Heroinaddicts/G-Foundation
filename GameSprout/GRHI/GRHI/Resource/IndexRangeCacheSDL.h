#pragma once
#include "GRHIApi.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <span>
#include <vector>

namespace GRHI {
// Borrowed bytes must remain immutable for this cache's entire lifetime. A
// BufferSDL owns both the upload shadow and this bounded, derived range cache.
// The format is part of the key: identical bytes have different UInt16/32 values.
class IndexRangeCacheSDL final {
public:
    struct Range {std::uint32_t Minimum=0,Maximum=0;};
    static constexpr std::size_t MaximumRanges=64;
    explicit IndexRangeCacheSDL(std::span<const std::uint8_t> bytes):_Bytes(bytes) {}
    bool Query(std::size_t byteOffset,std::uint32_t count,IndexFormat format,Range& output) {
        output={};
        if(!count||static_cast<unsigned>(format)>static_cast<unsigned>(IndexFormat::UInt32))return false;
        const std::size_t unit=format==IndexFormat::UInt16?2:4;
        if(byteOffset%unit||byteOffset>_Bytes.size()||count>(_Bytes.size()-byteOffset)/unit)return false;
        // Commands can be recorded on their owning threads; protect only this
        // newly shared derived cache, never hold a lock while invoking SDL.
        const std::lock_guard<std::mutex> lock(_Mutex);
        for(const auto& entry:_Entries)if(entry.Offset==byteOffset&&entry.Count==count&&entry.Format==format) {
            output=entry.Value;return true;
        }
        Range range{std::numeric_limits<std::uint32_t>::max(),0};
        const auto* source=_Bytes.data()+byteOffset;
        for(std::uint32_t index=0;index<count;++index) {
            std::uint32_t value=0;
            if(unit==2){std::uint16_t small;std::memcpy(&small,source+std::size_t(index)*2,2);value=small;}
            else std::memcpy(&value,source+std::size_t(index)*4,4);
            range.Minimum=std::min(range.Minimum,value);range.Maximum=std::max(range.Maximum,value);
        }
        _ScannedIndices+=count;
        const Entry entry{byteOffset,count,format,range};
        if(_Entries.size()<MaximumRanges)_Entries.push_back(entry);
        else {_Entries[_NextVictim]=entry;_NextVictim=(_NextVictim+1)%MaximumRanges;}
        output=range;return true;
    }
    void Clear() {
        const std::lock_guard<std::mutex> lock(_Mutex);
        std::vector<Entry>().swap(_Entries);_NextVictim=0;
    }
    // Internal diagnostics used by the focused cache tests; no public RHI API.
    std::size_t CachedRangeCount() const {const std::lock_guard<std::mutex> lock(_Mutex);return _Entries.size();}
    std::uint64_t ScannedIndexCount() const {const std::lock_guard<std::mutex> lock(_Mutex);return _ScannedIndices;}
private:
    struct Entry {std::size_t Offset;std::uint32_t Count;IndexFormat Format;Range Value;};
    const std::span<const std::uint8_t> _Bytes;
    mutable std::mutex _Mutex;
    std::vector<Entry> _Entries;
    std::size_t _NextVictim=0;
    std::uint64_t _ScannedIndices=0;
};
}

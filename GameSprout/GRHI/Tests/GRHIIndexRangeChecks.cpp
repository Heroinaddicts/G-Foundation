#include "../GRHI/Resource/IndexRangeCacheSDL.h"
#include <atomic>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
std::vector<std::uint8_t> Bytes(std::initializer_list<std::uint32_t> values) {
    std::vector<std::uint8_t> result(values.size()*4);std::size_t at=0;
    for(auto value:values){std::memcpy(result.data()+at,&value,4);at+=4;}return result;
}
}
int main() {
    try {
        const auto bytes=Bytes({0,4,7,9,3,5,1,2,4,6});GRHI::IndexRangeCacheSDL cache(bytes);
        GRHI::IndexRangeCacheSDL::Range range;
        Check(cache.Query(0,3,GRHI::IndexFormat::UInt32,range)&&range.Minimum==0&&range.Maximum==7,"Wrong initial UInt32 range");
        const auto scanned=cache.ScannedIndexCount();
        for(unsigned repeat=0;repeat<1000;++repeat)
            Check(cache.Query(0,3,GRHI::IndexFormat::UInt32,range)&&range.Maximum==7,"Repeated range changed extrema");
        Check(cache.ScannedIndexCount()==scanned&&cache.CachedRangeCount()==1,"Immutable range was scanned again on cache hits");
        Check(cache.Query(4,3,GRHI::IndexFormat::UInt32,range)&&range.Minimum==4&&range.Maximum==9,"Byte offset was omitted from cache key");
        Check(cache.Query(0,2,GRHI::IndexFormat::UInt32,range)&&range.Maximum==4,"Count was omitted from cache key");
        std::uint16_t expected[3];std::memcpy(expected,bytes.data(),sizeof(expected));
        Check(cache.Query(0,3,GRHI::IndexFormat::UInt16,range)&&range.Minimum==*std::min_element(expected,expected+3)&&
            range.Maximum==*std::max_element(expected,expected+3),"Index format was omitted from cache key");
        Check(!cache.Query(1,3,GRHI::IndexFormat::UInt32,range),"Misaligned range was accepted");
        Check(!cache.Query(0,0,GRHI::IndexFormat::UInt32,range),"Zero count was accepted");
        Check(!cache.Query(0,3,static_cast<GRHI::IndexFormat>(99),range),"Unknown index format was accepted");
        Check(!cache.Query(0,11,GRHI::IndexFormat::UInt32,range),"Out-of-range index count was accepted");
        Check(!cache.Query(std::numeric_limits<std::size_t>::max(),1,GRHI::IndexFormat::UInt32,range),"Overflowing byte offset was accepted");
        Check(!cache.Query(bytes.size(),1,GRHI::IndexFormat::UInt32,range),"Range past the final byte was accepted");
        Check(!cache.Query(0,std::numeric_limits<std::uint32_t>::max(),GRHI::IndexFormat::UInt32,range),"Overflowing index span was accepted");
        const auto extremes=Bytes({0xffffffffu,0x80000000u});GRHI::IndexRangeCacheSDL wide(extremes);
        Check(wide.Query(0,2,GRHI::IndexFormat::UInt32,range)&&range.Minimum==0x80000000u&&range.Maximum==0xffffffffu,
            "UInt32 extrema were narrowed to signed values");

        std::vector<std::uint8_t> many(4*256);
        for(std::uint32_t value=0;value<256;++value)std::memcpy(many.data()+value*4,&value,4);
        GRHI::IndexRangeCacheSDL bounded(many);
        for(std::uint32_t value=0;value<256;++value)
            Check(bounded.Query(value*4,1,GRHI::IndexFormat::UInt32,range)&&range.Minimum==value,"Distinct range lookup failed");
        Check(bounded.CachedRangeCount()==GRHI::IndexRangeCacheSDL::MaximumRanges,"Index cache grew beyond its per-buffer bound");
        const auto before=bounded.ScannedIndexCount();
        Check(bounded.Query(0,1,GRHI::IndexFormat::UInt32,range)&&range.Maximum==0&&bounded.ScannedIndexCount()==before+1,
            "Evicted range was not correctly scanned again");
        bounded.Clear();Check(bounded.CachedRangeCount()==0,"Buffer release did not clear derived cache");

        // A derived immutable-buffer cache can be queried by separately owned
        // recording threads without racing publication or duplicating the scan.
        GRHI::IndexRangeCacheSDL concurrent(bytes);std::atomic<bool> failed{false};std::vector<std::thread> workers;
        for(unsigned worker=0;worker<8;++worker)workers.emplace_back([&]{
            GRHI::IndexRangeCacheSDL::Range value;
            for(unsigned repeat=0;repeat<200;++repeat)
                if(!concurrent.Query(0,3,GRHI::IndexFormat::UInt32,value)||value.Maximum!=7)failed=true;
        });
        for(auto& worker:workers)worker.join();
        Check(!failed&&concurrent.ScannedIndexCount()==3,"Concurrent cache publication lost or rescanned a range");
        std::cout<<"PASS immutable index ranges: cache hits, byte/count/type keys, bounds, eviction and concurrent publication\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}

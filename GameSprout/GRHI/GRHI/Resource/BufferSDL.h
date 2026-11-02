#pragma once
#include "GRHIApi.h"
#include "IndexRangeCacheSDL.h"
#include <vector>
struct SDL_GPUBuffer;
namespace GRHI {
class DeviceSDL;
class BufferSDL final : public IBuffer {
public:
    BufferSDL(DeviceSDL* owner,uint64_t id,SDL_GPUBuffer* native,const BufferDesc& desc,std::vector<uint8_t> indices);
    ~BufferSDL() override;
    void Release() override;
    size_t GetSize() const override { return Desc.Size; }
    BufferUsage GetUsage() const override { return Desc.Usage; }
    DeviceSDL* const Owner;
    SDL_GPUBuffer* Native;
    const BufferDesc Desc;
    // Index-capable buffers keep the uploaded bytes for bounds validation before any GPU command is emitted.
    const std::vector<uint8_t> IndexBytes;
    bool GetIndexRange(std::size_t byteOffset,std::uint32_t count,IndexFormat format,IndexRangeCacheSDL::Range& range) {
        return _IndexRanges.Query(byteOffset,count,format,range);
    }
private:
    IndexRangeCacheSDL _IndexRanges;
};
}

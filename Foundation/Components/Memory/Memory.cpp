#include "Memory.h"
#include "MMap.h"
#include "ShareMemoryMap.h"

namespace Foundation {
    bool Memory::Initialize(FoundationApi* const foundation) {
        return true;
    }

    bool Memory::Launch(FoundationApi* const foundation) {
        return true;
    }

    void Memory::Release(FoundationApi* const foundation) {

    }

    void Memory::EarlyUpdate(FoundationApi* const foundation) {

    }

    void Memory::Update(FoundationApi* const foundation) {

    }

    void Memory::LaterUpdate(FoundationApi* const foundation) {

    }

    IMMap* Memory::CreateMMap(const std::string& file, const eAccess a) {
        return MMap::Create(file, a);
    }

    void Memory::CloseMMap(IMMap* mmap) {
        MMap::Release(static_cast<MMap*>(mmap));
    }

    void Memory::DestroyShareMemoryMap(const std::string& shareMemoryName, const std::string& mapName) {
        ShareMemoryMap::Destroy(shareMemoryName, mapName);
    }

    IShareMemoryMap* Memory::OpenOrCreateShareMemoryMapInternal(const std::string& shareMemoryName, const UInt64 shareMemorySize, const std::string& name, const UInt64 keySize, const UInt64 valueSize) {
        return ShareMemoryMap::OpenOrCreate(shareMemoryName, shareMemorySize, name, keySize, valueSize);
    }

    IShareMemoryMap* Memory::LoadShareMemoryMapInternal(const std::string& shareMemoryName, const std::string& name, const UInt64 keySize, const UInt64 valueSize) {
        return ShareMemoryMap::Load(shareMemoryName, name, keySize, valueSize);
    }

    void Memory::ReleaseShareMemoryMapInternal(IShareMemoryMap* map) {
        ShareMemoryMap::Release(static_cast<ShareMemoryMap*>(map));
    }
}

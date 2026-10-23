#ifndef __Memory_h__
#define __Memory_h__

#include "Header.h"

namespace Foundation {
    class Memory : public IMemory {
    public:
        virtual ~Memory() {}

        // 通过 IMemory 继承
        bool Initialize(FoundationApi* const foundation) override;
        bool Launch(FoundationApi* const foundation) override;
        void Release(FoundationApi* const foundation) override;
        void EarlyUpdate(FoundationApi* const foundation) override;
        void Update(FoundationApi* const foundation) override;
        void LaterUpdate(FoundationApi* const foundation) override;
        IMMap* CreateMMap(const std::string& file, const eAccess a) override;
        void CloseMMap(IMMap* mmap) override;
        void DestroyShareMemoryMap(const std::string& shareMemoryName, const std::string& mapName) override;

    protected:
        IShareMemoryMap* OpenOrCreateShareMemoryMapInternal(const std::string& shareMemoryName, const UInt64 shareMemorySize, const std::string& name, const UInt64 keySize, const UInt64 valueSize) override;
        IShareMemoryMap* LoadShareMemoryMapInternal(const std::string& shareMemoryName, const std::string& name, const UInt64 keySize, const UInt64 valueSize) override;
        void ReleaseShareMemoryMapInternal(IShareMemoryMap* map) override;
    };
}

#endif //__Memory_h__

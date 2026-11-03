#pragma once

namespace GRHI { class IDevice; }
namespace GameSproutRuntime {
class Runtime;
class IRuntimeResourceProvider;
class RenderResourceCache;

// Internal ownership only. The Runtime/provider pair chooses one cache on the
// Runtime's device; each viewport keeps it alive until all of its private state
// has been destroyed. Construction failure automatically returns the lease.
class RenderResourceLease final {
public:
    RenderResourceLease(Runtime&,GRHI::IDevice&,IRuntimeResourceProvider&);
    ~RenderResourceLease();
    RenderResourceLease(const RenderResourceLease&)=delete;
    RenderResourceLease& operator=(const RenderResourceLease&)=delete;
    RenderResourceCache& Get() const {return *_Cache;}
private:
    Runtime& _Owner;
    IRuntimeResourceProvider* _Provider=nullptr;
    RenderResourceCache* _Cache=nullptr;
};
}

#include "INetApi.h"

namespace {
class ModuleSession final : public Foundation::ITcpSession {
public:
    int OnReceive(const void*, int size) override { return size; }
};
}

extern "C" __attribute__((visibility("default"))) Foundation::ITcpSession* CreateManualOwnershipSession() {
    return new ModuleSession;
}

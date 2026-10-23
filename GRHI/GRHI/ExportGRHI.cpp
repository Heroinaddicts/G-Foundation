#include "GRHIExport.h"
#include "GRHIFinal.h"

// Preserve the existing factory symbol used by the public dynamic loader.
extern "C" API GRHI::GRHIApi* CALL CreateFoundation() noexcept {
    try {
        return GRHI::GRHIFinal::GetInstance();
    } catch (...) {
        return nullptr;
    }
}

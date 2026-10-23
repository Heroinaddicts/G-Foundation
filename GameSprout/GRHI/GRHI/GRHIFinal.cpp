#include "GRHIFinal.h"
#include "DeviceSDL.h"
#include <SDL3/SDL.h>

namespace GRHI {
    GRHIFinal * GRHIFinal::_Instance = nullptr;

    GRHIFinal::~GRHIFinal() {
        if (_Instance == this) {
            _Instance = nullptr;
        }
        if (_VideoInitialized) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    }

    GRHIFinal * GRHIFinal::GetInstance() {
        if (_Instance == nullptr) {
            GRHIFinal * instance = new GRHIFinal();
            try {
                if (!instance->Initialize()) {
                    delete instance;
                    return nullptr;
                }
            } catch (...) {
                delete instance;
                throw;
            }
            _Instance = instance;
        }
        return _Instance;
    }

    const std::vector<AdapterInfo> & GRHIFinal::GetAdapterList() const {
        return _Adapters;
    }

    bool GRHIFinal::Initialize() {
        // Balance only our subsystem initialization; never call global SDL_Quit.
        _VideoInitialized = SDL_InitSubSystem(SDL_INIT_VIDEO);
        if (!_VideoInitialized) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "GRHI initialization failed: %s", SDL_GetError());
            return false;
        }

        if (SDL_GetGPUAdapterExtensionVersion() != SDL_GRHI_GPU_ADAPTERS_VERSION) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "GRHI initialization failed: incompatible SDL adapter extension.");
            return false;
        }
        int count = 0;
        SDL_GPUAdapterInfo * adapters = SDL_GetGPUAdapters(&count);
        if (count < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "GRHI adapter enumeration failed: %s", SDL_GetError());
            SDL_free(adapters);
            return false;
        }
        try {
            _Adapters.reserve(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                if (adapters[i].identifier[0] != '\0') {
                    _Adapters.emplace_back(adapters[i].identifier, adapters[i].name);
                }
            }
        } catch (...) {
            SDL_free(adapters);
            throw;
        }
        SDL_free(adapters);
        if (_Adapters.empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "GRHI initialization failed: no identifiable physical GPU adapters found.");
            return false;
        }
        return true;
    }

    const std::string & GRHIFinal::GetLastError() const {
        return _Error;
    }

    IDevice * GRHIFinal::CreateDevice(const std::string & adapterId) {
        _Error.clear();
        if (!adapterId.empty()) {
            const AdapterInfo * selected = nullptr;
            for (const auto & adapter : _Adapters) {
                if (adapter.Id == adapterId) {
                    selected = &adapter;
                    break;
                }
            }
            if (selected == nullptr) {
                _Error = "Invalid GRHI adapter ID.";
                return nullptr;
            }
        }

        return DeviceSDL::CreateDeviceSDL(adapterId, _Error);
    }
}

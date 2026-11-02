#include "DeviceSDL.h"
#include "SurfaceSDL.h"
#include <SDL3/SDL.h>

namespace GRHI {
    DeviceSDL::~DeviceSDL() { Release(); }

    DeviceSDL * DeviceSDL::CreateDeviceSDL(const std::string &adapterIdentifier, std::string & error) {
        error.clear();
        SDL_GPUDevice * device = nullptr;
        SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_INVALID;
#if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
        constexpr const char* requiredDriver = "metal";
        formats = SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB;
#elif defined(SDL_PLATFORM_WINDOWS)
        constexpr const char* requiredDriver = "direct3d12";
        formats = SDL_GPU_SHADERFORMAT_DXBC | SDL_GPU_SHADERFORMAT_DXIL;
#else
        constexpr const char* requiredDriver = "vulkan";
        formats = SDL_GPU_SHADERFORMAT_SPIRV;
#endif

        // Reject an SDL override before it can create a disallowed backend.
        const char * overrideDriver = SDL_GetHint(SDL_HINT_GPU_DRIVER);
        if (overrideDriver && *overrideDriver && SDL_strcasecmp(overrideDriver, requiredDriver) != 0) {
            error = std::string("GRHI requires the ") + requiredDriver + " backend on this platform; SDL_GPU_DRIVER requests another backend.";
            return nullptr;
        }

#ifdef GRHI_GPU_DEBUG
        constexpr bool debugMode = true;
#else
        constexpr bool debugMode = false;
#endif
        SDL_PropertiesID props = SDL_CreateProperties();
        if (!props) {
            error = SDL_GetError();
            return nullptr;
        }

        try {
            // SDL implements rendering; the host platform fixes the allowed backend.
            const bool configured =
                SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, requiredDriver) &&
                SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, debugMode) &&
                SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, (formats & SDL_GPU_SHADERFORMAT_MSL) != 0) &&
                SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_METALLIB_BOOLEAN, (formats & SDL_GPU_SHADERFORMAT_METALLIB) != 0) &&
                SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXBC_BOOLEAN, (formats & SDL_GPU_SHADERFORMAT_DXBC) != 0) &&
                SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, (formats & SDL_GPU_SHADERFORMAT_DXIL) != 0) &&
                SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, (formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0) &&
                (adapterIdentifier.empty() || SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_ADAPTER_STRING, adapterIdentifier.c_str()));
            if (configured) {
                device = SDL_CreateGPUDeviceWithProperties(props);
            }
            if (!device) {
                error = SDL_GetError();
            }
            SDL_DestroyProperties(props);
            props = 0;
            if (!device) {
                return nullptr;
            }

            const char * actualDriver = SDL_GetGPUDeviceDriver(device);
            if (!actualDriver || SDL_strcasecmp(actualDriver, requiredDriver) != 0) {
                error = std::string("SDL did not create the required ") + requiredDriver + " backend.";
                SDL_DestroyGPUDevice(device);
                return nullptr;
            }
            if (!adapterIdentifier.empty()) {
                const char * actualAdapter = SDL_GetStringProperty(
                    SDL_GetGPUDeviceProperties(device), SDL_PROP_GPU_DEVICE_ADAPTER_STRING, "");
                if (SDL_strcmp(actualAdapter, adapterIdentifier.c_str()) != 0) {
                    error = "SDL did not create the device on the requested physical adapter.";
                    SDL_DestroyGPUDevice(device);
                    return nullptr;
                }
            }

            // Ownership transfers only after allocation and construction succeed.
            return new DeviceSDL(device);
        } catch (...) {
            if (props) {
                SDL_DestroyProperties(props);
            }
            if (device) {
                SDL_DestroyGPUDevice(device);
            }
            throw;
        }
    }

    void DeviceSDL::Release() {
        if (_Device != nullptr) {
            if (!_Resources.empty() || !_Commands.empty()) { Fail("Release resources and submit/cancel commands before releasing the device."); return; }
            SDL_WaitForGPUIdle(_Device);
            SDL_DestroyGPUDevice(_Device);
            _Device = nullptr;
        }
    }

    ISurface * DeviceSDL::CreateSurface(GRHIWindowHandle const handle) {
        ClearError();
        if (!_Device) {
            Fail("Cannot create a surface: device has been released.");
            return nullptr;
        }

        if (!handle) {
            Fail("Cannot create a surface: window handle is invalid.");
            return nullptr;
        }

        return SurfaceSDL::CreateSurfaceSDL(this, handle);
    }
}

#include "GRHIExport.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
}
int main(int argc,char** argv) {
    GRHI::GRHIApi* api=nullptr;
    GRHI::IDevice* device=nullptr;
    const char* previous=SDL_GetHint(SDL_HINT_GPU_DRIVER);
    const bool hadHint=previous!=nullptr;
    const std::string previousValue=previous?previous:"";
    const auto cleanup=[&] {
        delete device;device=nullptr;delete api;api=nullptr;
        if(hadHint)SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER,previousValue.c_str(),SDL_HINT_OVERRIDE);
        else SDL_ResetHint(SDL_HINT_GPU_DRIVER);
    };
    try {
        Check(argc==2,"Expected runtime directory");
#if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
        const char* allowed="metal";const char* forbidden="vulkan";
#elif defined(SDL_PLATFORM_WINDOWS)
        const char* allowed="direct3d12";const char* forbidden="vulkan";
#else
        const char* allowed="vulkan";const char* forbidden="metal";
#endif
        Check(SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER,forbidden,SDL_HINT_OVERRIDE),"Cannot set test hint");
        api=ExportGRHIApi(argv[1]);Check(api,"Cannot load GRHI");
        Check(!api->GetAdapterList().empty(),"Backend override affected physical GPU enumeration");
        device=api->CreateDevice();Check(!device,"Forbidden backend override was accepted");
        Check(api->GetLastError().find("SDL_GPU_DRIVER")!=std::string::npos,"Missing platform-policy error");
        device=api->CreateDevice(api->GetAdapterList().front().Id);Check(!device,"Explicit adapter bypassed backend policy");
        Check(SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER,allowed,SDL_HINT_OVERRIDE),"Cannot set allowed hint");
        device=api->CreateDevice();Check(device,"Allowed SDL backend could not create the device");
        delete device;device=nullptr;
        SDL_ResetHint(SDL_HINT_GPU_DRIVER);
        device=api->CreateDevice();Check(device,"Default platform backend could not create the device");
        cleanup();std::cout<<"PASS SDL GPU fixed platform backend, default/explicit adapter and forbidden override rejection\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';cleanup();return 1;}
}

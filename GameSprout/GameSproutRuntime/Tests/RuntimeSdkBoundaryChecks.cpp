#include "GameSproutRuntimeApi.h"
#include "RuntimeRenderApi.h"
#include "RuntimeResourceApi.h"
#include "GameSproutRuntimeExport.h"
#include <type_traits>

#if defined(__GRHIApi_h__)
#error The public Runtime SDK must not include GRHI headers.
#endif

static_assert(std::is_same_v<decltype(GameSproutRuntime::NativeWindow::Handle),std::uintptr_t>);
static_assert(std::is_same_v<decltype(GameSproutRuntime::TextureRenderData::Sampler),GameSproutRuntime::SamplerSettings>);
static_assert(std::is_base_of_v<GameSproutRuntime::IRuntimeRenderer,GameSproutRuntime::IRuntimeViewport>);

int main() {
    GameSproutRuntime::NativeWindow window;
    GameSproutRuntime::RenderView view;
    GameSproutRuntime::SamplerSettings sampler;
    return window.Handle==0&&view.Scene==nullptr&&sampler.AddressU==GameSproutRuntime::TextureAddressMode::ClampToEdge?0:1;
}

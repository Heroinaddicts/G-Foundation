#include "TextureRendererSDL.h"
#include "DeviceSDL.h"
#include "Resource/TextureSDL.h"
#include <algorithm>
#include <cmath>

namespace GRHI {
namespace {
constexpr char kImageShader[] = R"(
#include <metal_stdlib>
using namespace metal;
struct ImageVertex { float4 position [[position]]; float2 uv; };
vertex ImageVertex imageVertex(uint id [[vertex_id]]) {
    const float2 positions[3] = {float2(-1, 1), float2(3, 1), float2(-1, -3)};
    const float2 uvs[3] = {float2(0, 0), float2(2, 0), float2(0, 2)};
    ImageVertex result;
    result.position = float4(positions[id], 0, 1);
    result.uv = uvs[id];
    return result;
}
fragment float4 imageFragment(ImageVertex input [[stage_in]],
    texture2d<float> image [[texture(0)]], sampler imageSampler [[sampler(0)]]) {
    return image.sample(imageSampler, input.uv);
}
)";
}
TextureRendererSDL::~TextureRendererSDL() {
    delete _Pipeline;
    delete _FragmentShader;
    delete _VertexShader;
    delete _Sampler;
}
bool TextureRendererSDL::Initialize() {
    if (!_Sampler) {
        _Sampler = _Device->CreateSampler(SamplerDesc{});
        if (!_Sampler) return false;
    }
    ShaderCode code;
    code.Format = ShaderCodeFormat::MSL;
    code.Data = kImageShader;
    code.DataSize = sizeof(kImageShader);
    code.EntryPoint = "imageVertex";
    ShaderDesc desc;
    desc.Codes = &code;
    desc.CodeCount = 1;
    if (!_VertexShader) {
        _VertexShader = _Device->CreateShader(desc);
        if (!_VertexShader) return false;
    }
    if (!_FragmentShader) {
        desc.Stage = ShaderStage::Fragment;
        code.EntryPoint = "imageFragment";
        code.SamplerCount = 1;
        _FragmentShader = _Device->CreateShader(desc);
        if (!_FragmentShader) return false;
    }
    return true;
}

bool TextureRendererSDL::EnsurePipeline(TextureFormat format) {
    if (_Pipeline && _Pipeline->GetColorFormat() == format) return true;
    GraphicsPipelineDesc desc;
    desc.VertexShader = _VertexShader;
    desc.FragmentShader = _FragmentShader;
    desc.ColorFormat = format;
    desc.Blend = BlendMode::Alpha;
    auto* pipeline = _Device->CreateGraphicsPipeline(desc);
    if (!pipeline) return false;
    delete _Pipeline;
    _Pipeline = pipeline;
    return true;
}

bool TextureRendererSDL::Draw(ISurface& surface, ITexture* value, const Color& clearColor) {
    auto* texture = dynamic_cast<TextureSDL*>(value);
    if (!texture || texture->Owner != _Device || !texture->Native ||
        (texture->Desc.Usage & TextureUsage::Sampled) == TextureUsage::None ||
        !std::isfinite(clearColor.R) || !std::isfinite(clearColor.G) ||
        !std::isfinite(clearColor.B) || !std::isfinite(clearColor.A))
        return _Device->Fail("Draw requires a live sampled texture from this device and finite clear colors.");
    if (!Initialize()) return false;
    ICommandBuffer* command = _Device->AcquireCommandBuffer();
    if (!command) return false;
    bool hasFrame = false;
    IRenderPass* pass = nullptr;
    auto submit = [&]() {
        auto* consumed = command;
        command = nullptr;
        return _Device->Submit(consumed);
    };
    try {
        SurfaceFrame frame;
        const auto acquired = surface.AcquireFrame(command, frame);
        if (acquired != AcquireFrameResult::Ready) {
            const auto error = _Device->GetLastError();
            if (!_Device->Cancel(command)) {
                const auto cancelError = _Device->GetLastError();
                submit();
                return _Device->Fail(cancelError);
            }
            command = nullptr;
            return acquired == AcquireFrameResult::Unavailable ? true : _Device->Fail(error);
        }
        hasFrame = true;
        if (!EnsurePipeline(frame.Format)) {
            const auto error = _Device->GetLastError();
            submit(); return _Device->Fail(error);
        }
        RenderPassDesc desc;
        desc.ColorTarget = frame.ColorTarget;
        desc.ClearColor = clearColor;
        pass = command->BeginRenderPass(desc);
        if (!pass) {
            const auto error = _Device->GetLastError();
            submit(); return _Device->Fail(error);
        }
        const float scale = std::min(float(frame.Width) / texture->GetWidth(), float(frame.Height) / texture->GetHeight());
        Viewport viewport;
        viewport.Width = texture->GetWidth() * scale;
        viewport.Height = texture->GetHeight() * scale;
        viewport.X = (frame.Width - viewport.Width) * 0.5f;
        viewport.Y = (frame.Height - viewport.Height) * 0.5f;
        pass->SetPipeline(_Pipeline);
        pass->SetViewport(viewport);
        pass->SetFragmentTextureSampler(0, texture, _Sampler);
        pass->Draw(3);
        command->EndRenderPass(pass); pass = nullptr;
        return submit();
    } catch (...) {
        if (command) {
            if (pass) command->EndRenderPass(pass);
            if (hasFrame) submit();
            else if (!_Device->Cancel(command)) submit();
        }
        throw;
    }
}
}

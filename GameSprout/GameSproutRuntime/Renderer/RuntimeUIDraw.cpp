#include "RuntimeRenderer.h"
#include "BuiltinUIFont.h"
#include "Api/RuntimeUIApi.h"
#include "RuntimeMath.h"
#include "Scene.h"
#include "UI/RuntimeUILayout.h"
#include <algorithm>
#include <cmath>
#include <exception>

namespace GameSproutRuntime {
namespace {
struct UIVertex {Vector2 Position,UV;Color Tint;};
static_assert(sizeof(UIVertex)==32);
constexpr std::size_t MaxUIVertices=6u*131072u;

std::uint32_t DecodeCodepoint(std::string_view value,std::size_t& cursor) {
    // TextComponent validates its UTF-8 on assignment.
    const auto first=static_cast<unsigned char>(value[cursor++]);
    if(first<0x80)return first;
    const unsigned bytes=first<0xE0?1u:(first<0xF0?2u:3u);
    std::uint32_t result=first&((1u<<(6-bytes))-1u);
    for(unsigned index=0;index<bytes;++index)result=(result<<6)|(static_cast<unsigned char>(value[cursor++])&0x3Fu);
    return result;
}

const FontGlyph* FindGlyph(const FontRenderData& font,std::uint32_t codepoint) {
    const auto found=std::lower_bound(font.Glyphs.begin(),font.Glyphs.end(),codepoint,
        [](const FontGlyph& glyph,std::uint32_t value){return glyph.Codepoint<value;});
    return found!=font.Glyphs.end()&&found->Codepoint==codepoint?&*found:nullptr;
}

bool ValidFont(const FontRenderData& font) {
    if(font.Atlas.Guid.empty()||!font.Atlas.LocalID||font.Glyphs.empty()||font.Glyphs.size()>4096||
       !std::isfinite(font.PixelSize)||font.PixelSize<1||font.PixelSize>512||
       !std::isfinite(font.Ascent)||font.Ascent<0||font.Ascent>4096||
       !std::isfinite(font.Descent)||std::fabs(font.Descent)>4096)return false;
    return std::all_of(font.Glyphs.begin(),font.Glyphs.end(),[](const FontGlyph& glyph) {
        return glyph.Codepoint<=0x10FFFF && (glyph.Codepoint<0xD800||glyph.Codepoint>0xDFFF) &&
            std::isfinite(glyph.BearingX)&&std::fabs(glyph.BearingX)<=4096 &&
            std::isfinite(glyph.BearingY)&&std::fabs(glyph.BearingY)<=4096 &&
            std::isfinite(glyph.AdvanceX)&&std::fabs(glyph.AdvanceX)<=4096;
    });
}

Vector2 ToClipPosition(Vector2 pixel,Vector2 viewport) {
    return {pixel.X/viewport.X*2.0f-1.0f,1.0f-pixel.Y/viewport.Y*2.0f};
}

struct UICorner {
    Vector2 Pixel, UV;
};

struct UIPolygon {
    // Clipping a convex quad against four planes adds at most two entries
    // per plane, including coincident intersections at exact boundaries.
    std::array<UICorner,12> Corners{};
    std::size_t Count=0;

    void Clip(bool xAxis,float boundary,float sign) {
        if (!Count) return;
        const auto distance=[=](Vector2 point) {return (double(xAxis?point.X:point.Y)-boundary)*sign;};
        auto previous=Corners[Count-1];
        double previousDistance=distance(previous.Pixel);
        UIPolygon result;
        for (std::size_t index=0;index<Count;++index) {
            const auto current=Corners[index];
            const double currentDistance=distance(current.Pixel);
            if ((previousDistance<0&&currentDistance>0)||(previousDistance>0&&currentDistance<0)) {
                const float amount=float(previousDistance/(previousDistance-currentDistance));
                result.Corners[result.Count++]={previous.Pixel*(1.0f-amount)+current.Pixel*amount,
                                              previous.UV*(1.0f-amount)+current.UV*amount};
            }
            if (currentDistance>=0) result.Corners[result.Count++]=current;
            previous=current;
            previousDistance=currentDistance;
        }
        *this=result;
    }
};

bool AppendQuad(std::vector<UIVertex>& vertices,const Rect& rectangle,const Rect& uv,const Rect& localClip,
    const UILayoutTransform& transform,const Rect& viewportClip,Vector2 viewport,Color tint) {
    if(tint.A<=0||rectangle.Size.X<=0||rectangle.Size.Y<=0)return true;
    const float left=std::max(rectangle.Position.X,localClip.Position.X);
    const float top=std::max(rectangle.Position.Y,localClip.Position.Y);
    const float right=std::min(rectangle.Position.X+rectangle.Size.X,localClip.Position.X+localClip.Size.X);
    const float bottom=std::min(rectangle.Position.Y+rectangle.Size.Y,localClip.Position.Y+localClip.Size.Y);
    if(!(right>left&&bottom>top))return true;

    const float u0=uv.Position.X+((left-rectangle.Position.X)/rectangle.Size.X)*uv.Size.X;
    const float v0=uv.Position.Y+((top-rectangle.Position.Y)/rectangle.Size.Y)*uv.Size.Y;
    const float u1=uv.Position.X+((right-rectangle.Position.X)/rectangle.Size.X)*uv.Size.X;
    const float v1=uv.Position.Y+((bottom-rectangle.Position.Y)/rectangle.Size.Y)*uv.Size.Y;
    UIPolygon polygon{{{
        {transform.Point({left,top}),{u0,v0}}, {transform.Point({right,top}),{u1,v0}},
        {transform.Point({right,bottom}),{u1,v1}}, {transform.Point({left,bottom}),{u0,v1}}
    }},4};
    const auto first=polygon.Corners.begin();
    const auto last=first+polygon.Count;
    if(std::any_of(first,last,[](const UICorner& corner) {
        return !std::isfinite(corner.Pixel.X)||!std::isfinite(corner.Pixel.Y);
    }))return true;
    const auto maximum=viewportClip.Position+viewportClip.Size;
    if(!std::all_of(first,last,[&](const UICorner& corner) {
        return corner.Pixel.X>=viewportClip.Position.X&&corner.Pixel.X<=maximum.X&&
            corner.Pixel.Y>=viewportClip.Position.Y&&corner.Pixel.Y<=maximum.Y;
    })) {
        polygon.Clip(true,viewportClip.Position.X,1.0f);
        polygon.Clip(true,viewportClip.Position.X+viewportClip.Size.X,-1.0f);
        polygon.Clip(false,viewportClip.Position.Y,1.0f);
        polygon.Clip(false,viewportClip.Position.Y+viewportClip.Size.Y,-1.0f);
    }
    if(polygon.Count<3)return true;
    if((polygon.Count-2)*3>MaxUIVertices-vertices.size())return false;
    const auto makeVertex=[&](const UICorner& corner){return UIVertex{ToClipPosition(corner.Pixel,viewport),corner.UV,tint};};
    const auto anchor=makeVertex(polygon.Corners[0]);
    auto previous=makeVertex(polygon.Corners[1]);
    for(std::size_t index=2;index<polygon.Count;++index) {
        const auto next=makeVertex(polygon.Corners[index]);
        vertices.insert(vertices.end(),{anchor,previous,next});
        previous=next;
    }
    return true;
}

template<class EmitGlyph>
void DrawText(const ITextComponent& text,const UIElement& element,const FontRenderData& font,
    GRHI::ITexture& atlas,EmitGlyph emit) {
    const std::string_view value=text.GetText();
    const float scale=(text.GetFontSize()/font.PixelSize)*element.Scale;
    const float ascent=font.Ascent*scale;
    const float lineHeight=std::max(font.PixelSize,font.Ascent+std::fabs(font.Descent))*scale;
    const auto lineCount=1+std::count(value.begin(),value.end(),'\n');
    const Rect& rectangle=element.Rectangle;
    float baseline=rectangle.Position.Y+(rectangle.Size.Y-float(lineCount)*lineHeight)*0.5f+ascent;
    const float atlasWidth=float(atlas.GetWidth()),atlasHeight=float(atlas.GetHeight());
    const FontGlyph* replacement=FindGlyph(font,0xFFFD);
    if(!replacement)replacement=FindGlyph(font,'?');
    std::vector<const FontGlyph*> line;
    for(std::size_t start=0;start<value.size();) {
        const auto end=value.find('\n',start);
        const std::string_view span=value.substr(start,end==std::string_view::npos?end:end-start);
        line.clear();
        float lineWidth=0;
        for(std::size_t cursor=0;cursor<span.size();) {
            const auto codepoint=DecodeCodepoint(span,cursor);
            if(codepoint=='\r')continue;
            const auto* glyph=FindGlyph(font,codepoint=='\t'?' ':codepoint);
            if(!glyph)glyph=replacement;
            const unsigned copies=codepoint=='\t'?4u:1u;
            line.insert(line.end(),copies,glyph);
            lineWidth+=copies*(glyph?glyph->AdvanceX:font.PixelSize*0.5f)*scale;
        }
        float pen=rectangle.Position.X+(rectangle.Size.X-lineWidth)*0.5f;
        for(const auto* glyph:line) {
            if(glyph&&glyph->Width&&glyph->Height&&
               std::uint64_t(glyph->X)+glyph->Width<=atlas.GetWidth()&&std::uint64_t(glyph->Y)+glyph->Height<=atlas.GetHeight()) {
                const Rect quad{{pen+glyph->BearingX*scale,baseline+glyph->BearingY*scale},
                    {glyph->Width*scale,glyph->Height*scale}};
                const Rect uv{{glyph->X/atlasWidth,glyph->Y/atlasHeight},{glyph->Width/atlasWidth,glyph->Height/atlasHeight}};
                if(!emit(quad,uv))return;
            }
            pen+=(glyph?glyph->AdvanceX:font.PixelSize*0.5f)*scale;
        }
        if(end==std::string_view::npos)break;
        start=end+1;
        baseline+=lineHeight;
    }
}
}

bool Renderer::InitializeUIFallback() {
    if(_UIFallbackAtlas)return true;
    using namespace BuiltinUIFont;
    std::vector<std::uint8_t> pixels(std::size_t(Width)*Height*4,255);
    for(std::size_t offset=3;offset<pixels.size();offset+=4)pixels[offset]=0;
    FontRenderData face;
    face.PixelSize=8;
    face.Ascent=7;
    face.Descent=1;
    face.Glyphs.reserve(95);
    for(std::uint32_t glyph=0;glyph<95;++glyph) {
        const std::uint32_t x=(glyph%Columns)*CellWidth+1,y=(glyph/Columns)*CellHeight+1;
        face.Glyphs.push_back({glyph+32,x,y,glyph?5u:0u,glyph?7u:0u,0,-7,6});
        for(std::uint32_t row=0;row<7;++row)
            for(std::uint32_t column=0;column<5;++column)
                if(Rows[glyph][row]&(1u<<(4-column)))
                    pixels[(std::size_t(y+row)*Width+x+column)*4+3]=255;
    }
    GRHI::TextureDesc desc;desc.Width=Width;desc.Height=Height;
    const GRHI::TextureInitialData initial{pixels.data(),pixels.size(),0};
    _UIFallbackAtlas=_Device.CreateTexture(desc,&initial);
    if(!_UIFallbackAtlas)return Fail(DeviceError("Create built-in UI font atlas"));
    _UIFallback=std::move(face);
    return true;
}

const FontRenderData* Renderer::UIFontData(const ResourceReference& reference) {
    auto [position,inserted]=_UIFonts.try_emplace(ResourceKey(reference));
    auto& font=position->second;
    if(inserted) {
        font.ResourceRevision=_Resources.GetResourceRevision(reference);
        bool loaded=false;
        try {loaded=_Resources.LoadFont(reference,font.Data,font.Error);}
        catch(const std::exception& exception) {font.Error=exception.what();}
        catch(...) {font.Error="Resource provider threw while loading font";}
        if(loaded&&ValidFont(font.Data)) {
            auto& glyphs=font.Data.Glyphs;
            std::sort(glyphs.begin(),glyphs.end(),[](const auto& left,const auto& right){return left.Codepoint<right.Codepoint;});
            if(std::adjacent_find(glyphs.begin(),glyphs.end(),[](const auto& left,const auto& right){return left.Codepoint==right.Codepoint;})==glyphs.end())
                return &font.Data;
        }
        font.Data={};
        if(font.Error.empty())font.Error="Invalid prepared font metrics";
    }
    if(!font.Data.Glyphs.empty())return &font.Data;
    Diagnostic("Font "+reference.Guid+"_"+std::to_string(reference.LocalID)+": "+font.Error);
    return nullptr;
}

void Renderer::ClearUIDraws() {
    delete _UIBuffer;
    _UIBuffer=nullptr;
    _UIBatches.clear();
    _UISceneID=0;
    _UIDiagnostic.clear();
}

bool Renderer::PrepareUI(IScene& scene,std::uint32_t width,std::uint32_t height,std::uint64_t previewObjectID,
    Vector2 gameViewportSize) {
    const auto sceneID=scene.GetInstanceID();
    const auto revision=static_cast<Scene&>(scene).GetUIRevision();
    const auto resources=_Resources.GetRevision();
    if(sceneID==_UISceneID&&revision==_UISceneRevision&&resources==_UIResourceRevision&&width==_UIWidth&&height==_UIHeight&&
       previewObjectID==_UIPreviewObjectID && gameViewportSize.X==_UIPreviewGameSize.X && gameViewportSize.Y==_UIPreviewGameSize.Y) {
        Diagnostic(_UIDiagnostic);
        return true;
    }
    if(resources!=_UIResourceRevision)
        std::erase_if(_UIFonts,[this](const auto& entry) {
            return _Resources.GetResourceRevision({entry.first.Guid,entry.first.LocalID})!=entry.second.ResourceRevision;
        });
    ClearUIDraws();
    const Vector2 size{float(width),float(height)};
    const auto preview=previewObjectID?BuildUIPreview(_Runtime,scene,previewObjectID,size,gameViewportSize):UIPreviewFrame{};
    const bool framed=preview.Canvas.Size.X>0&&preview.Canvas.Size.Y>0;
    const auto gameplay=previewObjectID?std::vector<UIElement>{}:BuildUIFrame(_Runtime,scene,size);
    const auto& elements=previewObjectID?preview.Elements:gameplay;
    const Rect viewport{{},size};
    const Rect uiClip=framed?preview.Canvas:viewport;
    const auto diagnosticBefore=_Error.size();
    if((framed||!elements.empty())&&!InitializeSamples())return false;
    std::vector<UIVertex> vertices;
    bool full=false;
    const auto emitQuad=[&](const Rect& quad,const Rect& uv,const Rect& clip,const UILayoutTransform& transform,
                            Color tint,GRHI::ITexture* texture,GRHI::ISampler* sampler) {
        const auto first=static_cast<std::uint32_t>(vertices.size());
        if(!AppendQuad(vertices,quad,uv,clip,transform,uiClip,size,tint)) {full=true;return false;}
        const auto count=static_cast<std::uint32_t>(vertices.size())-first;
        if(count) {
            if(_UIBatches.empty()||_UIBatches.back().Texture!=texture||_UIBatches.back().Sampler!=sampler)
                _UIBatches.push_back({texture,sampler,first,count});
            else _UIBatches.back().VertexCount+=count;
        }
        return true;
    };
    const Rect solidUV{{},{1,1}};
    const auto solid=[&](const Rect& rect,Color color) {
        return emitQuad(rect,solidUV,rect,{},color,_White,_DefaultSampler);
    };
    if(framed)solid(preview.Canvas,{.02f,.035f,.06f,.55f});
    for(const auto& element:elements) {
        const auto emit=[&](const Rect& quad,const Rect& uv,Color tint,GRHI::ITexture* texture,GRHI::ISampler* sampler) {
            return emitQuad(quad,uv,element.Rectangle,element.Transform,tint,texture,sampler);
        };
        if(auto* image=element.Object->GetComponent<IImageComponent>()) {
            const auto reference=image->GetTexture();
            auto* texture=reference.Guid.empty()?nullptr:Texture(reference);
            Color tint=image->GetColor();
            if(auto* button=element.Object->GetComponent<IButtonComponent>()) {
                const float multiplier=!button->GetInteractable()?0.5f:(button->IsPressed()?0.75f:1.0f);
                tint.R*=multiplier;tint.G*=multiplier;tint.B*=multiplier;
            }
            if(!emit(element.Rectangle,image->GetUVRect(),tint,texture?texture->Texture:_White,texture?texture->Sampler:_DefaultSampler))break;
        }
        if(auto* text=element.Object->GetComponent<ITextComponent>();text&&!text->GetText().empty()&&text->GetColor().A>0) {
            const auto& reference=text->GetFont();
            const auto* font=reference.Guid.empty()?nullptr:UIFontData(reference);
            auto* texture=font?Texture(font->Atlas):nullptr;
            if(!texture) {
                if(!InitializeUIFallback())return false;
                font=&_UIFallback;
            }
            auto* atlas=texture?texture->Texture:_UIFallbackAtlas;
            auto* sampler=texture?texture->Sampler:_DefaultSampler;
            DrawText(*text,element,*font,*atlas,[&](const Rect& quad,const Rect& uv) {
                return emit(quad,uv,text->GetColor(),atlas,sampler);
            });
            if(full)break;
        }
    }
    if(framed) {
        const auto outline=[&](const Rect& rect,Color color) {
            const float thickness=std::min({2.0f,rect.Size.X*0.5f,rect.Size.Y*0.5f});
            const auto [x,y]=rect.Position;
            const auto [w,h]=rect.Size;
            solid({{x,y},{w,thickness}},color);
            solid({{x,y+h-thickness},{w,thickness}},color);
            solid({{x,y},{thickness,h}},color);
            solid({{x+w-thickness,y},{thickness,h}},color);
        };
        outline(preview.Canvas,preview.IsCanvasSelected?Color{.22f,.66f,1.f,1.f}:Color{.4f,.62f,.8f,.85f});
    }
    if(full)Diagnostic("UI geometry exceeds the viewport vertex limit");
    if(!vertices.empty()) {
        GRHI::BufferDesc desc;desc.Size=vertices.size()*sizeof(UIVertex);
        GRHI::BufferInitialData initial{vertices.data(),desc.Size};
        _UIBuffer=_Device.CreateBuffer(desc,&initial);
        if(!_UIBuffer)return Fail(DeviceError("Upload screen-space UI geometry"));
    }
    _UIDiagnostic=_Error.substr(diagnosticBefore);
    if(!_UIDiagnostic.empty()&&_UIDiagnostic.front()=='\n')_UIDiagnostic.erase(0,1);
    _UISceneID=sceneID;_UISceneRevision=revision;_UIResourceRevision=resources;
    _UIPreviewObjectID=previewObjectID;
    _UIPreviewGameSize=gameViewportSize;
    _UIWidth=width;_UIHeight=height;
    return true;
}

GRHI::IGraphicsPipeline* Renderer::UIPipeline(GRHI::TextureFormat color,GRHI::TextureFormat depth) {
    const auto key=std::make_pair(color,depth);
    if(const auto found=_UIPipelines.find(key);found!=_UIPipelines.end())return found->second;
    if(!_UIVertex)_UIVertex=CreatePreviewShader(_Device,PreviewShader::UIVertex);
    if(!_UIFragment)_UIFragment=CreatePreviewShader(_Device,PreviewShader::UIFragment);
    if(!_UIVertex||!_UIFragment) {Fail(DeviceError("Create Runtime UI shaders"));return nullptr;}
    const GRHI::VertexBindingDesc binding{0,sizeof(UIVertex)};
    const GRHI::VertexAttributeDesc attributes[]{
        {0,0,GRHI::VertexFormat::Float2,offsetof(UIVertex,Position)},
        {1,0,GRHI::VertexFormat::Float2,offsetof(UIVertex,UV)},
        {2,0,GRHI::VertexFormat::Float4,offsetof(UIVertex,Tint)}
    };
    GRHI::GraphicsPipelineDesc desc;
    desc.VertexShader=_UIVertex;desc.FragmentShader=_UIFragment;
    desc.ColorFormat=color;desc.DepthFormat=depth;desc.Blend=GRHI::BlendMode::Alpha;
    desc.VertexBindings=&binding;desc.VertexBindingCount=1;
    desc.VertexAttributes=attributes;desc.VertexAttributeCount=3;
    auto [position,inserted]=_UIPipelines.emplace(key,nullptr);
    position->second=_Device.CreateGraphicsPipeline(desc);
    if(!position->second) {_UIPipelines.erase(position);Fail(DeviceError("Create Runtime UI pipeline"));return nullptr;}
    return position->second;
}

bool Renderer::DrawUI(GRHI::IRenderPass& pass,GRHI::TextureFormat color,GRHI::TextureFormat depth) {
    if(!_UIBuffer)return true;
    auto* pipeline=UIPipeline(color,depth);
    if(!pipeline)return false;
    pass.SetViewport({0,0,float(_UIWidth),float(_UIHeight)});
    pass.SetPipeline(pipeline);
    pass.SetVertexBuffer(0,_UIBuffer);
    for(const auto& batch:_UIBatches) {
        pass.SetFragmentTextureSampler(0,batch.Texture,batch.Sampler);
        pass.Draw(batch.VertexCount,1,batch.FirstVertex);
        ++_Statistics.UIDrawCalls;
        _Statistics.UITriangles+=batch.VertexCount/3;
    }
    _Statistics.DrawCalls+=_Statistics.UIDrawCalls;
    _Statistics.Triangles+=_Statistics.UITriangles;
    return true;
}

bool Renderer::DrawUIScreenDebug(GRHI::IRenderPass& pass,GRHI::TextureFormat color,GRHI::TextureFormat depth,
    Vector2 viewport,const UIDebugDrawList& overlay,std::vector<GRHI::IBuffer*>& transientBuffers) {
    if(overlay.Triangles.empty())return true;
    if(overlay.Triangles.size()>MaxUIVertices/3)return Fail("Screen debug geometry exceeds the viewport vertex limit");
    if(!InitializeSamples())return false;
    auto* pipeline=UIPipeline(color,depth);
    if(!pipeline)return false;
    std::vector<UIVertex> vertices;
    vertices.reserve(overlay.Triangles.size()*3);
    for(const auto& triangle:overlay.Triangles) {
        if(!RuntimeMath::IsFinite(triangle.Tint))return Fail("Screen debug geometry has a nonfinite color");
        for(const auto pixel:{triangle.A,triangle.B,triangle.C}) {
            const auto position=ToClipPosition(pixel,viewport);
            if(!std::isfinite(position.X)||!std::isfinite(position.Y))return Fail("Screen debug geometry has a nonfinite position");
            vertices.push_back({position,{},triangle.Tint});
        }
    }
    GRHI::BufferDesc desc;desc.Size=vertices.size()*sizeof(UIVertex);
    const GRHI::BufferInitialData initial{vertices.data(),desc.Size};
    // The transient buffer is released only after the recording is submitted.
    auto& buffer=transientBuffers.emplace_back(nullptr);
    buffer=_Device.CreateBuffer(desc,&initial);
    if(!buffer)return Fail(DeviceError("Upload screen debug geometry"));
    pass.SetViewport({0,0,viewport.X,viewport.Y});
    pass.SetPipeline(pipeline);
    pass.SetVertexBuffer(0,buffer);
    pass.SetFragmentTextureSampler(0,_White,_DefaultSampler);
    pass.Draw(static_cast<std::uint32_t>(vertices.size()),1,0);
    ++_Statistics.DrawCalls;
    _Statistics.Triangles+=static_cast<std::uint32_t>(overlay.Triangles.size());
    return true;
}
}

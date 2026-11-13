#include "RuntimeRenderer.h"
#include "RuntimeShadowsMath.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace GameSproutRuntime {
namespace {
struct ClipRange {
    double Minimum,Maximum;
};
struct PixelRect {
    std::uint32_t Left,Top,Right,Bottom;
    std::uint64_t Area() const {return std::uint64_t(Right-Left)*(Bottom-Top);}
};
constexpr std::size_t MaxRegionsPerCascade=8;
constexpr std::size_t MaxRegionCandidates=32;

ClipRange ProjectBounds(const Mat4& matrix,std::size_t row,const Bounds& bounds) {
    const auto& center=bounds.Center;
    const auto& extent=bounds.Extents;
    const double x=matrix(row,0),y=matrix(row,1),z=matrix(row,2);
    const double midpoint=x*center.X+y*center.Y+z*center.Z+matrix(row,3);
    const double radius=std::abs(x)*extent.X+std::abs(y)*extent.Y+std::abs(z)*extent.Z;
    return {midpoint-radius,midpoint+radius};
}

PixelRect Union(const PixelRect& a,const PixelRect& b) {
    return {std::min(a.Left,b.Left),std::min(a.Top,b.Top),std::max(a.Right,b.Right),std::max(a.Bottom,b.Bottom)};
}

void InsertRegion(std::vector<PixelRect>& regions,PixelRect candidate) {
    for(std::size_t index=0;index<regions.size();) {
        const auto& other=regions[index];
        if(candidate.Right<other.Left||candidate.Left>other.Right||candidate.Bottom<other.Top||candidate.Top>other.Bottom) {
            ++index;
            continue;
        }
        candidate=Union(candidate,other);
        regions[index]=regions.back();
        regions.pop_back();
        // The enlarged rectangle may now touch one we already visited.
        index=0;
    }
    regions.push_back(candidate);
}

void MergeLeastArea(std::vector<PixelRect>& regions) {
    auto minimum=std::numeric_limits<std::uint64_t>::max();
    std::size_t first=0,second=1;
    for(std::size_t left=0;left<regions.size();++left)for(std::size_t right=left+1;right<regions.size();++right) {
        const auto& a=regions[left];const auto& b=regions[right];
        const auto extra=Union(a,b).Area()-a.Area()-b.Area();
        if(extra<minimum){minimum=extra;first=left;second=right;}
    }
    const auto merged=Union(regions[first],regions[second]);
    regions[second]=regions.back();regions.pop_back();
    regions[first]=regions.back();regions.pop_back();
    InsertRegion(regions,merged);
}
}

void Renderer::AppendShadowRegion(std::vector<ShadowRegion>& regions,const Mat4& matrix,std::uint32_t cascade,
    std::uint32_t tile,std::uint32_t columns,std::uint32_t left,std::uint32_t top,std::uint32_t width,std::uint32_t height,
    std::uint32_t physicalX,std::uint32_t physicalY) {
    const auto x=(cascade%columns)*tile+physicalX,y=(cascade/columns)*tile+physicalY;
    ShadowRegion region{cascade,{float(x),float(y),float(width),float(height),0,1},matrix,left,top};
    // Clip to the logical subrectangle and rasterize it in its physical slot.
    for(std::size_t column=0;column<4;++column) {
        region.Projection(0,column)=float((tile*double(matrix(0,column))+
            (double(tile)-2.0*left-width)*matrix(3,column))/width);
        region.Projection(1,column)=float((tile*double(matrix(1,column))+
            (2.0*top+height-tile)*matrix(3,column))/height);
    }
    regions.push_back(region);
}

void Renderer::AppendWrappedShadowRegions(std::vector<ShadowRegion>& regions,const Mat4& matrix,
    std::uint32_t cascade,std::uint32_t tile,std::uint32_t columns,int left,int top,int right,int bottom,
    int offsetX,int offsetY) {
    const auto size=static_cast<int>(tile);
    // A logical rectangle can cross the physical wrap on either axis.
    // Split at those boundaries so no viewport touches an adjacent tile.
    for(int y=top;y<bottom;) {
        const int physicalY=RuntimeShadowMath::WrapShadowTexel(y+offsetY,size);
        const int height=std::min(bottom-y,size-physicalY);
        for(int x=left;x<right;) {
            const int physicalX=RuntimeShadowMath::WrapShadowTexel(x+offsetX,size);
            const int width=std::min(right-x,size-physicalX);
            AppendShadowRegion(regions,matrix,cascade,tile,columns,x,y,width,height,physicalX,physicalY);
            x+=width;
        }
        y+=height;
    }
}

bool Renderer::AppendShadowScrollRegions(std::vector<ShadowRegion>& regions,std::uint32_t cascade,std::uint32_t tile,
    std::uint32_t columns,const Mat4& previous,const Mat4& current,Vector4& scroll) {
    int dx,dy;
    if(!RuntimeShadowMath::TryShadowScroll(previous,current,tile,dx,dy))return false;
    const auto size=static_cast<int>(tile);
    const int offsetX=RuntimeShadowMath::WrapShadowTexel(static_cast<int>(std::lround(scroll.X*tile))-dx,size);
    const int offsetY=RuntimeShadowMath::WrapShadowTexel(static_cast<int>(std::lround(scroll.Y*tile))-dy,size);
    scroll={float(offsetX)/tile,float(offsetY)/tile,0,0};
    const auto append=[&](int left,int top,int right,int bottom) {
        AppendWrappedShadowRegions(regions,current,cascade,tile,columns,left,top,right,bottom,offsetX,offsetY);
    };
    const int overlapLeft=std::max(0,dx),overlapRight=size+std::min(0,dx);
    const int overlapTop=std::max(0,dy),overlapBottom=size+std::min(0,dy);
    if(dx>0)append(0,0,overlapLeft,size);
    if(dx<0)append(overlapRight,0,size,size);
    if(dy>0)append(overlapLeft,0,overlapRight,overlapTop);
    if(dy<0)append(overlapLeft,overlapBottom,overlapRight,size);
    return true;
}

bool Renderer::BuildShadowRepairRegions(const Mat4* matrices,const std::vector<Bounds>& removedBounds,
    const std::vector<Bounds>& addedBounds,const Vector4* scroll,std::uint32_t tile,std::uint32_t columns,
    std::uint32_t count,std::vector<ShadowRegion>& regions) {
    regions.clear();
    std::vector<PixelRect> candidates;
    candidates.reserve(MaxRegionCandidates);
    // Expand by one pixel for float rasterization error and keep the repair
    // inside the cascade tile. Viewport Y grows down the texture.
    const auto low=[](double pixel) {return std::uint32_t(std::max(0.0,std::floor(pixel)-1));};
    const auto high=[tile](double pixel) {return std::uint32_t(std::min(double(tile),std::ceil(pixel)+1));};
    for(std::uint32_t cascade=0;cascade<count;++cascade) {
        const auto& matrix=matrices[cascade];
        // Directional cascades are orthographic. The extent calculation relies on w=1.
        if(matrix(3,0)!=0||matrix(3,1)!=0||matrix(3,2)!=0||matrix(3,3)!=1)return false;
        candidates.clear();
        const auto include=[&](const std::vector<Bounds>& boundsList,bool adding) {
            for(const auto& bounds:boundsList) {
                const auto x=ProjectBounds(matrix,0,bounds),y=ProjectBounds(matrix,1,bounds);
                const auto z=ProjectBounds(matrix,2,bounds);
                if(!std::isfinite(x.Minimum)||!std::isfinite(x.Maximum)||!std::isfinite(y.Minimum)||
                   !std::isfinite(y.Maximum)||!std::isfinite(z.Minimum)||!std::isfinite(z.Maximum)||
                   x.Minimum>x.Maximum||y.Minimum>y.Maximum||z.Minimum>z.Maximum)return false;
                if(x.Maximum<-1||x.Minimum>1||y.Maximum<-1||y.Minimum>1)continue;
                // An added caster can require a different light depth fit even
                // though its XY footprint intersects this unchanged cascade.
                if(adding&&(z.Minimum<0||z.Maximum>1))return false;
                if(z.Maximum<0||z.Minimum>1)continue;
                const PixelRect rect{low((x.Minimum+1)*.5*tile),low((1-y.Maximum)*.5*tile),
                    high((x.Maximum+1)*.5*tile),high((1-y.Minimum)*.5*tile)};
                if(rect.Right<=rect.Left||rect.Bottom<=rect.Top)continue;
                InsertRegion(candidates,rect);
                // A large edited subtree can contain thousands of separated
                // bounds. Bound the pair search before collecting more of them.
                if(candidates.size()==MaxRegionCandidates)MergeLeastArea(candidates);
            }
            return true;
        };
        if(!include(removedBounds,false)||!include(addedBounds,true))return false;
        while(candidates.size()>MaxRegionsPerCascade)MergeLeastArea(candidates);
        const auto offsetX=static_cast<int>(std::lround(scroll[cascade].X*tile));
        const auto offsetY=static_cast<int>(std::lround(scroll[cascade].Y*tile));
        for(const auto& rect:candidates) {
            AppendWrappedShadowRegions(regions,matrix,cascade,tile,columns,
                rect.Left,rect.Top,rect.Right,rect.Bottom,offsetX,offsetY);
        }
    }
    return true;
}

GRHI::IGraphicsPipeline* Renderer::PrepareShadowRepair() {
    if(!_ShadowRepairVertices) {
        struct Vertex { Vector3 Position; Color Tint; };
        static_assert(sizeof(Vertex)==28);
        const Vertex triangle[]={{{-1,-1,1},{}},{{3,-1,1},{}},{{-1,3,1},{}}};
        GRHI::BufferDesc description;
        description.Size=sizeof(triangle);description.Usage=GRHI::BufferUsage::Vertex;
        const GRHI::BufferInitialData initial{triangle,sizeof(triangle)};
        _ShadowRepairVertices=_Device.CreateBuffer(description,&initial);
        if(!_ShadowRepairVertices) {
            Fail(DeviceError("Create directional shadow repair triangle"));
            return nullptr;
        }
    }
    PipelineKey key;
    key.Grid=true;key.DebugTriangles=true;key.Shadow=true;
    return Pipeline(key);
}

bool Renderer::ClearShadowRegion(GRHI::IRenderPass& pass) {
    auto* pipeline=PrepareShadowRepair();
    if(!pipeline)return false;
    const DrawUniform uniform;
    pass.SetPipeline(pipeline);
    pass.SetVertexBuffer(0,_ShadowRepairVertices,0);
    pass.SetVertexUniform(0,&uniform,sizeof(uniform));
    pass.Draw(3);
    return true;
}
}

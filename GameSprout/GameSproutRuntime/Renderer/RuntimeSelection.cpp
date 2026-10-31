#include "RuntimeRenderer.h"
#include "RuntimePickingMath.h"
#include "GameSproutRuntime.h"
#include <algorithm>
#include <exception>
#include <set>

namespace {
struct QueryScope {
    bool& Busy;
    explicit QueryScope(bool& busy):Busy(busy) {Busy=true;}
    ~QueryScope() {Busy=false;}
};
}
namespace GameSproutRuntime {
bool Renderer::GetWorldBounds(IScene* scene,std::uint64_t objectID,Bounds& bounds) {
    bounds={};if(!BeginQuery(scene)) return false;
    QueryScope query(_Rendering);RuntimeOperation operation(_Runtime);
    try {
        SyncRevision();IGameObject* object=nullptr;MeshGPU* mesh=nullptr;Mat4 model;
        if(!QueryObject(scene,objectID,object,mesh,model)) return false;
        std::vector<Vector3> positions;
        if(!PosedPositions(*object,*mesh,positions)) return false;
        PickingMath::Vector localMinimum,localMaximum;bool first=true;
        // 先求被section引用顶点的本地AABB，不信任源产物的bounds。
        // 再变换盒子而非每个三角形顶点，使Center始终为M*localCenter，旋转非对称网格时不漂移。
        for(const auto& section:mesh->Data.Sections) for(std::uint32_t index=0;index<section.IndexCount;++index) {
            const PickingMath::Vector point(positions[mesh->Indices[section.FirstIndex+index]]);
            if(first) {localMinimum=localMaximum=point;first=false;}
            else {
                localMinimum={std::min(localMinimum.X,point.X),std::min(localMinimum.Y,point.Y),std::min(localMinimum.Z,point.Z)};
                localMaximum={std::max(localMaximum.X,point.X),std::max(localMaximum.Y,point.Y),std::max(localMaximum.Z,point.Z)};
            }
        }
        if(first) return Fail("World mesh bounds are empty");
        PickingMath::Vector minimum,maximum;
        for(int corner=0;corner<8;++corner) {
            const PickingMath::Vector local{corner&1?localMaximum.X:localMinimum.X,
                corner&2?localMaximum.Y:localMinimum.Y,corner&4?localMaximum.Z:localMinimum.Z};
            const auto point=PickingMath::Transform(model,local,1);
            if(corner==0) minimum=maximum=point;
            else {
                minimum={std::min(minimum.X,point.X),std::min(minimum.Y,point.Y),std::min(minimum.Z,point.Z)};
                maximum={std::max(maximum.X,point.X),std::max(maximum.Y,point.Y),std::max(maximum.Z,point.Z)};
            }
        }
        Bounds candidate;
        Vector3 worldMinimum,worldMaximum;
        if(!PickingMath::ToVector3(minimum,worldMinimum) || !PickingMath::ToVector3(maximum,worldMaximum) ||
           !PickingMath::ToVector3((minimum+maximum)*0.5,candidate.Center) ||
           !PickingMath::ToVector3((maximum-minimum)*0.5,candidate.Extents)) return Fail("World mesh bounds are empty or outside finite float range");
        bounds=candidate;return true;
    } catch(const std::exception& exception) {return Fail(std::string("Runtime bounds: ")+exception.what());}
    catch(...) {return Fail("Unexpected Runtime bounds failure");}
}
bool Renderer::AppendSelectionOutline(IScene* scene,std::uint64_t objectID,DebugDrawList& draw,const Color& tint,bool depthTest) {
    if(!BeginQuery(scene)) return false;
    if(!RuntimeMath::IsFinite(tint)||tint.R<0||tint.G<0||tint.B<0||tint.A<0||tint.A>1) return Fail("Invalid selection outline color");
    QueryScope query(_Rendering);RuntimeOperation operation(_Runtime);
    try {
        SyncRevision();IGameObject* object=nullptr;MeshGPU* mesh=nullptr;Mat4 model;
        if(!QueryObject(scene,objectID,object,mesh,model)) return false;
        std::vector<Vector3> positions;
        if(!PosedPositions(*object,*mesh,positions)) return false;
        constexpr std::size_t limit=1048576;
        if(draw.Lines.size()>limit/2 || draw.Triangles.size()>limit/3 || draw.Lines.size()*2+draw.Triangles.size()*3>limit)
            return Fail("Existing debug draw exceeds vertex limit");
        const std::size_t available=(limit-draw.Lines.size()*2-draw.Triangles.size()*3)/2;
        std::set<std::pair<std::uint32_t,std::uint32_t>> edges;
        std::vector<DebugLine> lines;
        for(const auto& section:mesh->Data.Sections) for(std::uint32_t offset=0;offset<section.IndexCount;offset+=3) {
            const std::uint32_t indices[]={mesh->Indices[section.FirstIndex+offset],mesh->Indices[section.FirstIndex+offset+1],mesh->Indices[section.FirstIndex+offset+2]};
            for(int edge=0;edge<3;++edge) {
                const auto a=std::min(indices[edge],indices[(edge+1)%3]),b=std::max(indices[edge],indices[(edge+1)%3]);
                if(a==b || !edges.emplace(a,b).second) continue;
                if(lines.size()>=available) return Fail("Selection outline exceeds debug vertex limit");
                Vector3 from,to;
                if(!PickingMath::ToVector3(PickingMath::Transform(model,PickingMath::Vector(positions[a]),1),from) ||
                   !PickingMath::ToVector3(PickingMath::Transform(model,PickingMath::Vector(positions[b]),1),to)) return Fail("Selection outline has nonfinite world positions");
                lines.push_back({from,to,tint,depthTest});
            }
        }
        // reserve/insert只会复制无抛出的值类型；分配失败时已有列表保持不变。
        draw.Lines.reserve(draw.Lines.size()+lines.size());draw.Lines.insert(draw.Lines.end(),lines.begin(),lines.end());return true;
    } catch(const std::exception& exception) {return Fail(std::string("Runtime selection outline: ")+exception.what());}
    catch(...) {return Fail("Unexpected Runtime selection outline failure");}
}
}

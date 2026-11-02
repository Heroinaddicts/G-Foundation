#include "Resources/BuiltinMeshes.h"
#include <bit>
#include <cmath>
#include <iostream>
#include <stdexcept>
static void Check(bool value,const char* error){if(!value)throw std::runtime_error(error);}
static std::uint32_t Word(const std::uint8_t* bytes){return std::uint32_t(bytes[0])|std::uint32_t(bytes[1])<<8|std::uint32_t(bytes[2])<<16|std::uint32_t(bytes[3])<<24;}
static GameSproutRuntime::Vector3 Vector(const GameSproutRuntime::MeshRenderData& mesh,std::uint32_t vertex,unsigned offset){
    const auto* bytes=mesh.Bytes.data()+vertex*mesh.VertexStride+offset;
    return {std::bit_cast<float>(Word(bytes)),std::bit_cast<float>(Word(bytes+4)),std::bit_cast<float>(Word(bytes+8))};
}
int main(){try{
    for(unsigned id=1;id<=6;++id){
        const GameSproutRuntime::ResourceReference reference{GameSproutRuntime::BuiltinMeshGuid,id};
        Check(GameSproutRuntime::GetBuiltinMeshName(reference),"Unknown primitive identity");
        GameSproutRuntime::MeshRenderData mesh;std::string error;Check(GameSproutRuntime::BuiltinMeshes::Load(reference,mesh,error),error.c_str());
        Check(mesh.VertexCount&&mesh.IndexCount&&mesh.IndexCount%3==0&&mesh.IndexOffset+mesh.IndexCount*4==mesh.Bytes.size(),"Invalid prepared buffer layout");
        for(unsigned vertex=0;vertex<mesh.VertexCount;++vertex){
            const auto point=Vector(mesh,vertex,mesh.PositionOffset),normal=Vector(mesh,vertex,mesh.NormalOffset);
            Check(std::isfinite(point.X)&&std::isfinite(point.Y)&&std::isfinite(point.Z)&&std::fabs(normal.Dot(normal)-1)<1e-4f,"Nonfinite position or nonunit normal");
            Check(std::fabs(point.X)<=mesh.LocalBounds.Extents.X+1e-5f&&std::fabs(point.Y)<=mesh.LocalBounds.Extents.Y+1e-5f&&std::fabs(point.Z)<=mesh.LocalBounds.Extents.Z+1e-5f,"Vertex outside built-in bounds");
            for(unsigned component=0;component<2;++component){const auto value=std::bit_cast<float>(Word(mesh.Bytes.data()+vertex*mesh.VertexStride+mesh.UVOffset+component*4));Check(std::isfinite(value)&&value>=0&&value<=1,"Invalid UV");}
        }
        for(unsigned index=0;index<mesh.IndexCount;index+=3){
            const auto a=Word(mesh.Bytes.data()+mesh.IndexOffset+index*4),b=Word(mesh.Bytes.data()+mesh.IndexOffset+(index+1)*4),c=Word(mesh.Bytes.data()+mesh.IndexOffset+(index+2)*4);
            Check(a<mesh.VertexCount&&b<mesh.VertexCount&&c<mesh.VertexCount,"Index outside vertex buffer");
            const auto edge1=Vector(mesh,b,0)-Vector(mesh,a,0),edge2=Vector(mesh,c,0)-Vector(mesh,a,0);
            const auto cross=edge1.Cross(edge2);
            const auto normal=Vector(mesh,a,12)+Vector(mesh,b,12)+Vector(mesh,c,12);
            Check(cross.Dot(cross)>1e-12f&&cross.Dot(normal)>0,"Degenerate triangle or reversed outward winding");
        }
    }
    GameSproutRuntime::MeshRenderData empty;std::string error;
    Check(!GameSproutRuntime::BuiltinMeshes::Load({GameSproutRuntime::BuiltinMeshGuid,99},empty,error)&&empty.Bytes.empty(),"Unknown built-in ID accepted");
    Check(!GameSproutRuntime::GetBuiltinMeshName({"abcdefabcdefabcdefabcdefabcdefab",1}),"A project resource was mistaken for built-in geometry");
    std::cout<<"PASS six primitive byte layouts, normals, UVs, bounds, nondegenerate outward triangles and namespace validation\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

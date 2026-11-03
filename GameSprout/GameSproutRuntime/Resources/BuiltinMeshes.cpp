#include "BuiltinMeshes.h"
#include <bit>
#include <cmath>

namespace GameSproutRuntime::BuiltinMeshes {
namespace {
constexpr float Pi=3.14159265358979323846f;
constexpr std::uint32_t Segments=32;
struct Vertex {Vector3 Position,Normal;Vector2 UV;};
struct Geometry {
    std::vector<Vertex> Vertices;
    std::vector<std::uint32_t> Indices;
    void Triangle(std::uint32_t a,std::uint32_t b,std::uint32_t c) {Indices.insert(Indices.end(),{a,b,c});}
    void Quad(Vector3 center,Vector3 horizontal,Vector3 vertical,Vector3 normal) {
        const auto first=static_cast<std::uint32_t>(Vertices.size());
        Vertices.push_back({center-horizontal*.5f-vertical*.5f,normal,{0,0}});
        Vertices.push_back({center+horizontal*.5f-vertical*.5f,normal,{1,0}});
        Vertices.push_back({center+horizontal*.5f+vertical*.5f,normal,{1,1}});
        Vertices.push_back({center-horizontal*.5f+vertical*.5f,normal,{0,1}});
        Triangle(first,first+1,first+2);Triangle(first,first+2,first+3);
    }
};
void Cube(Geometry& mesh) {
    mesh.Quad({.5f,0,0},{0,0,-1},{0,1,0},{1,0,0});
    mesh.Quad({-.5f,0,0},{0,0,1},{0,1,0},{-1,0,0});
    mesh.Quad({0,0,.5f},{1,0,0},{0,1,0},{0,0,1});
    mesh.Quad({0,0,-.5f},{-1,0,0},{0,1,0},{0,0,-1});
    mesh.Quad({0,.5f,0},{1,0,0},{0,0,-1},{0,1,0});
    mesh.Quad({0,-.5f,0},{1,0,0},{0,0,1},{0,-1,0});
}
void Round(Geometry& mesh,bool capsule) {
    constexpr std::uint32_t HemisphereRings=8;
    const std::uint32_t rows=HemisphereRings*2+(capsule?1:0);
    for(std::uint32_t row=0;row<=rows;++row) {
        float angle,center=0;
        if(capsule) {
            const bool upper=row>HemisphereRings;
            angle=upper?float(row-HemisphereRings-1)/HemisphereRings*(Pi*.5f):
                -Pi*.5f+float(row)/HemisphereRings*(Pi*.5f);
            center=upper?.5f:-.5f;
        } else angle=-Pi*.5f+float(row)/rows*Pi;
        const float radial=(row==0||row==rows)?0:std::cos(angle);
        const float height=std::sin(angle);
        for(std::uint32_t column=0;column<=Segments;++column) {
            const float theta=2*Pi*float(column)/Segments;
            const Vector3 normal{radial*std::cos(theta),height,radial*std::sin(theta)};
            const Vector3 position{normal.X*.5f,normal.Y*.5f+center,normal.Z*.5f};
            mesh.Vertices.push_back({position,normal,{float(column)/Segments,(position.Y+(capsule?1:.5f))/(capsule?2:1)}});
        }
    }
    for(std::uint32_t row=0;row<rows;++row)for(std::uint32_t column=0;column<Segments;++column) {
        const auto a=row*(Segments+1)+column,b=a+Segments+1,c=b+1,d=a+1;
        if(row+1<rows)mesh.Triangle(a,b,c);
        if(row>0)mesh.Triangle(a,c,d);
    }
}
void Cylinder(Geometry& mesh) {
    for(std::uint32_t row=0;row<2;++row)for(std::uint32_t column=0;column<=Segments;++column) {
        const float theta=2*Pi*float(column)/Segments;
        const Vector3 normal{std::cos(theta),0,std::sin(theta)};
        mesh.Vertices.push_back({{normal.X*.5f,row?1.f:-1.f,normal.Z*.5f},normal,{float(column)/Segments,float(row)}});
    }
    for(std::uint32_t column=0;column<Segments;++column) {
        const auto a=column,b=a+Segments+1;
        mesh.Triangle(a,b,b+1);mesh.Triangle(a,b+1,a+1);
    }
    for(bool top:{false,true}) {
        const float height=top?1.f:-1.f;const Vector3 normal{0,height,0};
        const auto center=static_cast<std::uint32_t>(mesh.Vertices.size());
        mesh.Vertices.push_back({{0,height,0},normal,{.5f,.5f}});
        for(std::uint32_t column=0;column<=Segments;++column) {
            const float theta=2*Pi*float(column)/Segments,x=std::cos(theta)*.5f,z=std::sin(theta)*.5f;
            mesh.Vertices.push_back({{x,height,z},normal,{x+.5f,z+.5f}});
        }
        for(std::uint32_t column=0;column<Segments;++column) {
            if(top)mesh.Triangle(center,center+column+2,center+column+1);
            else mesh.Triangle(center,center+column+1,center+column+2);
        }
    }
}
void Word(std::vector<std::uint8_t>& bytes,std::uint32_t value) {
    for(unsigned shift=0;shift<32;shift+=8)bytes.push_back(static_cast<std::uint8_t>(value>>shift));
}
void Float(std::vector<std::uint8_t>& bytes,float value) {Word(bytes,std::bit_cast<std::uint32_t>(value));}
}
bool Load(const ResourceReference& reference,MeshRenderData& output,std::string& error) {
    output={};error.clear();
    if(!GetBuiltinMeshName(reference)){error="Unknown built-in mesh identity";return false;}
    const auto type=static_cast<PrimitiveType>(reference.LocalID-1);Geometry geometry;
    switch(type) {
    case PrimitiveType::Cube:Cube(geometry);break;
    case PrimitiveType::Sphere:Round(geometry,false);break;
    case PrimitiveType::Capsule:Round(geometry,true);break;
    case PrimitiveType::Cylinder:Cylinder(geometry);break;
    case PrimitiveType::Plane:geometry.Quad({},{10,0,0},{0,0,-10},{0,1,0});break;
    case PrimitiveType::Quad:
        geometry.Quad({}, {1,0,0},{0,1,0},{0,0,-1});
        for(std::size_t triangle=0;triangle<geometry.Indices.size();triangle+=3)
            std::swap(geometry.Indices[triangle+1],geometry.Indices[triangle+2]);
        break;
    }
    MeshRenderData mesh;
    mesh.VertexCount=static_cast<std::uint32_t>(geometry.Vertices.size());mesh.VertexStride=32;mesh.UVOffset=24;
    mesh.IndexCount=static_cast<std::uint32_t>(geometry.Indices.size());mesh.IndexOffset=std::uint64_t(mesh.VertexCount)*mesh.VertexStride;
    mesh.Bytes.reserve(static_cast<std::size_t>(mesh.IndexOffset)+geometry.Indices.size()*4);
    for(const auto& vertex:geometry.Vertices) {
        for(const auto value:{vertex.Position.X,vertex.Position.Y,vertex.Position.Z,vertex.Normal.X,vertex.Normal.Y,vertex.Normal.Z,vertex.UV.X,vertex.UV.Y})Float(mesh.Bytes,value);
    }
    for(const auto index:geometry.Indices)Word(mesh.Bytes,index);
    mesh.Sections.push_back({0,mesh.IndexCount,0});
    mesh.LocalBounds.Extents=type==PrimitiveType::Plane?Vector3{5,0,5}:type==PrimitiveType::Quad?Vector3{.5f,.5f,0}:
        (type==PrimitiveType::Capsule||type==PrimitiveType::Cylinder)?Vector3{.5f,1,.5f}:Vector3{.5f,.5f,.5f};
    output=std::move(mesh);return true;
}
}

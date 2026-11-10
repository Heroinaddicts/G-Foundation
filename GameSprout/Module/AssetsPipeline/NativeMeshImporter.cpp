#include "AssetImportJob.h"
#include "../Shared/NativeMeshDefinition.h"
#include <QtEndian>
#include <QStringDecoder>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace {
struct Vector {double X=0,Y=0,Z=0;};
Vector Subtract(Vector a,Vector b) {return {a.X-b.X,a.Y-b.Y,a.Z-b.Z};}
Vector Cross(Vector a,Vector b) {return {a.Y*b.Z-a.Z*b.Y,a.Z*b.X-a.X*b.Z,a.X*b.Y-a.Y*b.X};}
Vector Normalized(Vector value) {
    const double length=std::sqrt(value.X*value.X+value.Y*value.Y+value.Z*value.Z);
    return length>0?Vector{value.X/length,value.Y/length,value.Z/length}:Vector{0,1,0};
}
std::uint32_t Read32(const QByteArray& bytes,std::uint64_t offset) {
    return qFromLittleEndian<std::uint32_t>(reinterpret_cast<const uchar*>(bytes.constData()+offset));
}
float ReadFloat(const QByteArray& bytes,std::uint64_t offset) {return std::bit_cast<float>(Read32(bytes,offset));}
void Write32(QByteArray& bytes,std::uint32_t value) {
    char storage[4];qToLittleEndian(value,reinterpret_cast<uchar*>(storage));bytes.append(storage,4);
}
void WriteFloat(QByteArray& bytes,float value) {Write32(bytes,std::bit_cast<std::uint32_t>(value));}
struct Section {std::uint32_t First=0,Count=0,Material=0;};
}

bool ImportNativeMesh(AssetImportJob& job) {
    const QByteArray& source=job.SourceBytes;
    const auto fail=[&](const std::string& message){return job.Fail("Native .mesh: "+message);};
    if(source.size()<static_cast<qsizetype>(NativeMeshDefinition::HeaderBytes)||
       std::uint64_t(source.size())>NativeMeshDefinition::MaximumSourceBytes||
       std::memcmp(source.constData(),NativeMeshDefinition::Magic.data(),8)!=0)
        return fail("invalid GSMESH02 header or source size");
    const auto version=Read32(source,8),flags=Read32(source,12),vertexCount=Read32(source,16),
        indexCount=Read32(source,20),sectionCount=Read32(source,24),nameBytes=Read32(source,28);
    if(version!=NativeMeshDefinition::Version||(flags&~NativeMeshDefinition::ValidFlags)||vertexCount<3||
       indexCount<3||indexCount%3||sectionCount==0||sectionCount>1024||
       nameBytes==0||nameBytes>NativeMeshDefinition::MaximumNameBytes)
        return fail("unsupported version/flags or invalid vertex/index/section/name counts");
    const std::uint64_t vertexStart=(NativeMeshDefinition::HeaderBytes+std::uint64_t(nameBytes)+3)&~std::uint64_t{3};
    const std::uint32_t sourceFloats=3+((flags&NativeMeshDefinition::HasNormals)?3:0)+
        ((flags&NativeMeshDefinition::HasTangents)?4:0)+((flags&NativeMeshDefinition::HasUV0)?2:0)+
        ((flags&NativeMeshDefinition::HasColors)?4:0);
    const std::uint64_t indexStart=vertexStart+std::uint64_t(vertexCount)*sourceFloats*4;
    const std::uint64_t sectionStart=indexStart+std::uint64_t(indexCount)*4;
    const std::uint64_t expected=sectionStart+std::uint64_t(sectionCount)*12;
    if(expected!=std::uint64_t(source.size()))return fail("truncated data, inconsistent array sizes or trailing bytes");
    const QByteArray encodedName=source.mid(NativeMeshDefinition::HeaderBytes,nameBytes);
    QStringDecoder decoder(QStringDecoder::Utf8);const QString decodedName=decoder(encodedName);
    if(decoder.hasError()||encodedName.contains('\0')||decodedName.trimmed().isEmpty())return fail("name is not nonempty UTF8 without NUL");
    for(std::uint64_t offset=NativeMeshDefinition::HeaderBytes+nameBytes;offset<vertexStart;++offset)
        if(source[offset]!=0)return fail("name padding must be zero");
    // Native files contain the already exported mesh. Applying another unit or
    // axis conversion here would also move collision geometry away from visuals.
    if(job.Settings.get("ScaleFactor",1).asDouble()!=1||job.Settings.get("MeshCompression","Off")!="Off"||
       job.Settings.get("NormalsMode","Import")!="Import"||
       job.Settings.get("GenerateLightmapUVs",false).asBool())
        return fail("native geometry requires ScaleFactor=1, NormalsMode=Import, MeshCompression=Off and no generated lightmap UVs");

    std::uint64_t offset=vertexStart;
    const auto positionsOffset=offset;offset+=std::uint64_t(vertexCount)*12;
    const auto normalsOffset=offset;if(flags&NativeMeshDefinition::HasNormals)offset+=std::uint64_t(vertexCount)*12;
    const auto tangentsOffset=offset;if(flags&NativeMeshDefinition::HasTangents)offset+=std::uint64_t(vertexCount)*16;
    const auto uvOffset=offset;if(flags&NativeMeshDefinition::HasUV0)offset+=std::uint64_t(vertexCount)*8;
    const auto colorsOffset=offset;
    for(std::uint64_t at=vertexStart;at<indexStart;at+=4) {
        if((at&0xffffu)==0&&job.Cancelled())return false;
        if(!std::isfinite(ReadFloat(source,at)))return fail("vertex attributes contain a nonfinite float");
    }
    std::vector<Vector> positions(vertexCount),normalSums(vertexCount);
    Vector minimum{std::numeric_limits<double>::max(),std::numeric_limits<double>::max(),std::numeric_limits<double>::max()};
    Vector maximum{-minimum.X,-minimum.Y,-minimum.Z};
    for(std::uint32_t vertex=0;vertex<vertexCount;++vertex) {
        if(vertex%4096==0&&job.Cancelled())return false;
        const auto at=positionsOffset+std::uint64_t(vertex)*12;
        auto& point=positions[vertex];point={ReadFloat(source,at),ReadFloat(source,at+4),ReadFloat(source,at+8)};
        minimum={std::min(minimum.X,point.X),std::min(minimum.Y,point.Y),std::min(minimum.Z,point.Z)};
        maximum={std::max(maximum.X,point.X),std::max(maximum.Y,point.Y),std::max(maximum.Z,point.Z)};
    }
    std::vector<Section> sections;sections.reserve(sectionCount);
    std::vector<std::uint32_t> indices;indices.reserve(indexCount);
    std::uint32_t consumed=0;std::uint64_t degenerate=0;
    for(std::uint32_t section=0;section<sectionCount;++section) {
        if(job.Cancelled())return false;
        const auto at=sectionStart+std::uint64_t(section)*12;
        const auto first=Read32(source,at),count=Read32(source,at+4),material=Read32(source,at+8);
        if(first!=consumed||count%3||first>indexCount||count>indexCount-first||material>=1024)
            return fail("submeshes must partition triangle indices in order with material slots below 1024");
        consumed+=count;Section output{static_cast<std::uint32_t>(indices.size()),0,material};
        for(std::uint32_t triangle=first;triangle<first+count;triangle+=3) {
            if(triangle%4095==0&&job.Cancelled())return false;
            const auto a=Read32(source,indexStart+std::uint64_t(triangle)*4),
                b=Read32(source,indexStart+std::uint64_t(triangle+1)*4),c=Read32(source,indexStart+std::uint64_t(triangle+2)*4);
            if(a>=vertexCount||b>=vertexCount||c>=vertexCount)return fail("triangle index exceeds vertex count");
            const auto cross=Cross(Subtract(positions[b],positions[a]),Subtract(positions[c],positions[a]));
            if(a==b||b==c||a==c||(cross.X==0&&cross.Y==0&&cross.Z==0)){++degenerate;continue;}
            indices.push_back(a);indices.push_back(b);indices.push_back(c);output.Count+=3;
            for(auto index:{a,b,c}) {
                auto& sum=normalSums[index];sum.X+=cross.X;sum.Y+=cross.Y;sum.Z+=cross.Z;
            }
        }
        if(output.Count)sections.push_back(output);
    }
    if(consumed!=indexCount)return fail("submesh index ranges do not cover the index array");
    if(indices.empty())return fail("mesh contains no nondegenerate triangles");
    if(degenerate)job.Result.Diagnostics+="Native mesh removed "+std::to_string(degenerate)+" degenerate triangles\n";

    Json::Value metadata;metadata["SourceFormat"]="GSMESH02";metadata["VertexCount"]=vertexCount;
    metadata["VertexAttributes"]=Json::Value(Json::arrayValue);
    std::uint32_t stride=24;
    const auto attribute=[&](const char* semantic,const char* format,std::uint32_t at) {
        Json::Value item;item["Semantic"]=semantic;item["Format"]=format;item["Offset"]=at;item["Channel"]=0;
        metadata["VertexAttributes"].append(item);
    };
    attribute("Position","Float32x3",0);attribute("Normal","Float32x3",12);
    if(flags&NativeMeshDefinition::HasTangents){attribute("Tangent","Float32x4",stride);stride+=16;}
    if(flags&NativeMeshDefinition::HasUV0){attribute("TexCoord","Float32x2",stride);stride+=8;}
    if(flags&NativeMeshDefinition::HasColors){attribute("Color","Float32x4",stride);stride+=16;}
    const std::uint64_t payloadSize=std::uint64_t(vertexCount)*stride+std::uint64_t(indices.size())*4;
    if(payloadSize>NativeMeshDefinition::MaximumSourceBytes)return fail("prepared vertex/index buffer exceeds 512 MiB");
    QByteArray payload;payload.reserve(static_cast<qsizetype>(payloadSize));
    for(std::uint32_t vertex=0;vertex<vertexCount;++vertex) {
        if(vertex%4096==0&&job.Cancelled())return false;
        const auto& point=positions[vertex];WriteFloat(payload,float(point.X));WriteFloat(payload,float(point.Y));WriteFloat(payload,float(point.Z));
        Vector normal=normalSums[vertex];
        if(flags&NativeMeshDefinition::HasNormals) {
            const auto at=normalsOffset+std::uint64_t(vertex)*12;
            const Vector authored{ReadFloat(source,at),ReadFloat(source,at+4),ReadFloat(source,at+8)};
            normal=authored;
        } else {
            normal=Normalized(normal);
        }
        WriteFloat(payload,float(normal.X));WriteFloat(payload,float(normal.Y));WriteFloat(payload,float(normal.Z));
        if(flags&NativeMeshDefinition::HasTangents)payload.append(source.constData()+tangentsOffset+std::uint64_t(vertex)*16,16);
        if(flags&NativeMeshDefinition::HasUV0)payload.append(source.constData()+uvOffset+std::uint64_t(vertex)*8,8);
        if(flags&NativeMeshDefinition::HasColors)payload.append(source.constData()+colorsOffset+std::uint64_t(vertex)*16,16);
    }
    metadata["VertexStride"]=stride;metadata["UVChannels"]=(flags&NativeMeshDefinition::HasUV0)?1:0;
    metadata["IndexFormat"]="UInt32";metadata["IndexOffset"]=Json::UInt64(payload.size());
    metadata["IndexCount"]=Json::UInt64(indices.size());metadata["SubMeshes"]=Json::Value(Json::arrayValue);
    for(const auto& section:sections) {
        Json::Value item;item["FirstIndex"]=section.First;item["IndexCount"]=section.Count;item["MaterialSlot"]=section.Material;
        metadata["SubMeshes"].append(item);
    }
    for(auto index:indices)Write32(payload,index);
    const auto vector=[](Vector value){Json::Value result(Json::arrayValue);result.append(value.X);result.append(value.Y);result.append(value.Z);return result;};
    metadata["Bounds"]["Center"]=vector({minimum.X*.5+maximum.X*.5,minimum.Y*.5+maximum.Y*.5,minimum.Z*.5+maximum.Z*.5});
    metadata["Bounds"]["Extents"]=vector({maximum.X*.5-minimum.X*.5,maximum.Y*.5-minimum.Y*.5,maximum.Z*.5-minimum.Z*.5});
    metadata["Bones"]=Json::Value(Json::arrayValue);metadata["BlendShapes"]=Json::Value(Json::arrayValue);
    metadata["Settings"]=job.Settings;
    return job.Emit(eAssetType::Mesh,"main",decodedName.toStdString(),std::move(metadata),std::move(payload));
}

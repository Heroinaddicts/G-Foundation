#include "../../Module/Shared/AssetArtifactIO.h"
#include "../../Module/Shared/AssetValueBinary.h"
#include <iostream>
#include <limits>
#include <stdexcept>

static void Check(bool value,const char * message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        Json::Value input;
        input["Signed"]=Json::Int64(std::numeric_limits<int64_t>::min());
        input["Unsigned"]=Json::UInt64(std::numeric_limits<uint64_t>::max());
        input["Float"]=1.25;
        input["Name"]="蓝色小熊";
        input["Enabled"]=true;input["Disabled"]=false;input["Empty"]=Json::Value();
        input["Children"]=Json::Value(Json::arrayValue);
        input["Children"].append("Mesh");input["Children"].append(Json::Int64(-7));
        QByteArray bytes;Json::Value decoded;
        Check(AssetValueBinary::Encode(input,bytes),"Cannot encode structure");
        Check(static_cast<uint8_t>(bytes[0])==8&&!bytes.startsWith("GSPROUT"),"Unexpected JSON/container header");
        Check(AssetValueBinary::Decode(bytes,decoded)&&decoded==input,"Binary structure lost resource data");
        for (qsizetype length=0;length<bytes.size();++length)
            Check(!AssetValueBinary::Decode(bytes.left(length),decoded),"Truncated structure accepted");
        Check(!AssetValueBinary::Decode(bytes+QByteArray(1,'x'),decoded),"Trailing bytes accepted");
        Check(!AssetValueBinary::Decode(QByteArray(1,char(255)),decoded),"Invalid type tag accepted");
        QByteArray oversized(1,char(7));AssetValueBinary::U32(oversized,std::numeric_limits<uint32_t>::max());
        Check(!AssetValueBinary::Decode(oversized,decoded),"Invalid array count accepted");
        QByteArray duplicate(1,char(8));AssetValueBinary::U32(duplicate,2);
        AssetValueBinary::String(duplicate,"key");duplicate.append(char(0));
        AssetValueBinary::String(duplicate,"key");duplicate.append(char(0));
        Check(!AssetValueBinary::Decode(duplicate,decoded),"Duplicate structural field accepted");
        Json::Value nested;
        for (int i=0;i<66;++i) { Json::Value parent(Json::arrayValue);parent.append(std::move(nested));nested=std::move(parent); }
        Check(!AssetValueBinary::Encode(nested,bytes),"Excessively deep structure encoded");

        AssetImportManifest manifest;
        manifest.GuidString=Guid(1,2,3,4).GetGuidString();manifest.SourcePath="Bear.png";manifest.Directory="Artifact";
        AssetArtifact asset{17,eAssetType::Texture,"Bear","main",manifest.GuidString+"_17.gsasset","hash"};
        asset.DataVersion=2;asset.DataSize=64;asset.DescriptorJson="{\"Encoding\":\"RawBytes\",\"Width\":4,\"Height\":4,\"Format\":\"RGBA8\"}";
        manifest.Assets.push_back(asset);
        const auto database=AssetArtifactIO::ToJson(manifest);
        AssetImportManifest restored;
        Check(AssetArtifactIO::FromJson(database,restored),"Database descriptor did not round trip");
        Check(restored.Assets[0].DataSize==64&&restored.Assets[0].DescriptorJson==AssetArtifactIO::JsonText(database["Assets"][0]["Descriptor"]),"Database lost data layout");
        auto invalid=database;invalid["Assets"][0]["Descriptor"]["Encoding"]="Unknown";
        Check(!AssetArtifactIO::FromJson(invalid,restored),"Unknown descriptor encoding accepted");
        std::cout<<"PASS binary resource structure, bounds checks and database descriptors\n";
        return 0;
    } catch (const std::exception & error) { std::cerr<<"FAIL "<<error.what()<<"\n";return 1; }
}

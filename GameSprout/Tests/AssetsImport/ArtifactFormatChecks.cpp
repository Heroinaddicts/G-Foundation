#include "../../Module/Shared/AssetArtifactIO.h"
#include "../../Module/Shared/AssetValueBinary.h"
#include "../../Module/AssetsManager/AssetSourceRecord.h"
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
        manifest.Generation="current-generation";
        AssetArtifact asset{17,eAssetType::Texture,"Bear","main",manifest.GuidString+"_17.gsasset","hash"};
        asset.DataVersion=2;asset.DataSize=64;asset.DescriptorJson="{\"Encoding\":\"RawBytes\",\"Width\":4,\"Height\":4,\"Format\":\"RGBA8\"}";
        manifest.Assets.push_back(asset);
        const auto database=AssetArtifactIO::ToJson(manifest);
        AssetImportManifest restored;
        Check(AssetArtifactIO::FromJson(database,restored),"Database descriptor did not round trip");
        Check(restored.Assets[0].DataSize==64&&restored.Assets[0].DescriptorJson==AssetArtifactIO::JsonText(database["Assets"][0]["Descriptor"]),"Database lost data layout");
        auto invalid=database;invalid["Assets"][0]["Descriptor"]["Encoding"]="Unknown";
        Check(!AssetArtifactIO::FromJson(invalid,restored),"Unknown descriptor encoding accepted");
        for (const char* field:{"Generation","TextureSearches","Diagnostics"}) {
            invalid=database;invalid.removeMember(field);
            Check(!AssetArtifactIO::FromJson(invalid,restored),"Incomplete published manifest accepted");
        }
        invalid=database;invalid["Generation"]="";
        Check(!AssetArtifactIO::FromJson(invalid,restored),"Empty published Generation accepted");
        invalid=database;invalid["Version"]=1;
        Check(!AssetArtifactIO::FromJson(invalid,restored),"Removed manifest version accepted");
        invalid=database;invalid["Assets"][0]["DataVersion"]=1;
        Check(!AssetArtifactIO::FromJson(invalid,restored),"Removed artifact data version accepted");
        invalid=database;invalid["Assets"][0]["Path"]="nested/Bear.gsasset";
        Check(!AssetArtifactIO::FromJson(invalid,restored),"Noncurrent artifact path accepted");

        AssetSourceRecord source;
        source.GuidString=manifest.GuidString;source.Path="Bear.png";source.Importer=eAssetImporterType::Texture;
        AssetSourceRecord sourceRead;
        const auto sourceJson=source.ToJson();
        Check(AssetSourceRecord::FromJson(sourceJson,sourceRead) && sourceRead.SourceSize==-1
            && sourceRead.MetaJson.empty(),"Current pending source defaults rejected");
        for (const char* field:{"SourceSize","SourceModifyTime","MetaModifyTime"}) {
            invalid=sourceJson;invalid.removeMember(field);
            Check(!AssetSourceRecord::FromJson(invalid,sourceRead),"Incomplete persisted source record accepted");
        }
        Json::Value meta;
        meta["Version"]=1;meta["Guid"]=source.GuidString;meta["Path"]=source.Path;meta["ImporterType"]="Texture";
        meta["ImportSettings"]=Json::Value(Json::objectValue);meta["SubAssets"]=Json::Value(Json::arrayValue);
        invalid=sourceJson;invalid["MetaJson"]=AssetArtifactIO::JsonText(meta);
        Check(AssetSourceRecord::FromJson(invalid,sourceRead),"Current source Meta snapshot rejected");
        meta["AssetType"]="Texture";meta.removeMember("ImporterType");
        invalid["MetaJson"]=AssetArtifactIO::JsonText(meta);
        Check(!AssetSourceRecord::FromJson(invalid,sourceRead),"Old Meta snapshot accepted from database");
        std::cout<<"PASS current binary/manifest/source formats, old-format rejection and pending defaults\n";
        return 0;
    } catch (const std::exception & error) { std::cerr<<"FAIL "<<error.what()<<"\n";return 1; }
}

#include "AssetImportJob.h"
#include "PrefabDocument.h"
#include "../../Module/Shared/AssetMetaFormat.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value,const std::string& error){if(!value)throw std::runtime_error(error);}
const std::string PrefabGuid="0123456789abcdef0123456789abcdef";
const std::string MeshGuid="1234567890abcdef1234567890abcdef";
const std::string ScriptGuid="234567890abcdef1234567890abcdef1";
const std::string SceneGuid="34567890abcdef1234567890abcdef12";
class Context final:public Foundation::ITaskContext {
public:
    UInt64 GetTaskId()const override{return 1;}
    bool IsCancellationRequested()const override{return false;}
    void* GetContext()const override{return nullptr;}
    bool SetOwnedContext(void*,Foundation::fTaskOwnedContextRelease)override{return false;}
};
Json::Value Meta(const std::string& guid,const std::string& path,const char* importer) {
    Json::Value meta(Json::objectValue);meta["Version"]=1;meta["Guid"]=guid;meta["Path"]=path;
    meta["ImporterType"]=importer;meta["ImportSettings"]=Json::Value(Json::objectValue);meta["SubAssets"]=Json::Value(Json::arrayValue);return meta;
}
Json::Value Reference(const std::string& guid,std::uint64_t id) {
    Json::Value value;value["Guid"]=guid;value["LocalID"]=Json::UInt64(id);return value;
}
Json::Value Source() {
    Json::Value source(Json::objectValue);source["Version"]=1;source["Format"]="GameSproutPrefab";
    source["Base"]=Json::Value();source["NestedInstances"]=Json::Value(Json::arrayValue);
    auto& data=source["Data"];data["Version"]=1;data["Name"]="Prefab pipeline fixture";data["NextObjectID"]=Json::UInt64(4);
    data["Objects"]=Json::Value(Json::arrayValue);Json::Value node;
    node["ID"]=Json::UInt64(1);node["ParentID"]=Json::UInt64(0);node["Name"]="Mesh root";node["Components"]=Json::Value(Json::arrayValue);
    Json::Value transform;transform["ID"]=Json::UInt64(2);transform["Type"]="GameSproutRuntime.Transform";node["Components"].append(transform);
    Json::Value mesh;mesh["ID"]=Json::UInt64(3);mesh["Type"]="GameSproutRuntime.Mesh";mesh["Mesh"]=Reference(MeshGuid,41);node["Components"].append(mesh);
    data["Objects"].append(node);return source;
}
void Write(const QString& path,const QByteArray& bytes) {
    Check(QDir().mkpath(QFileInfo(path).absolutePath()),"Cannot create fixture directory");std::string error;
    Check(AssetArtifactIO::Write(path,bytes,error),error);
}
struct Outcome {bool Success=false;AssetImportResult Result;std::vector<std::uint8_t> Bytes;};
class Fixture {
public:
    QTemporaryDir Temporary;
    QDir Root;
    Context Task;
    Json::Value PrefabMeta=Meta(PrefabGuid,"Mesh.prefab","Prefab");
    Fixture():Root(Temporary.path()) {
        Check(Temporary.isValid()&&Root.mkpath("Assets")&&Root.mkpath("Library"),"Cannot create temporary project");
        Write(Root.filePath("Assets/Shape.mesh"),"mesh-dependency-snapshot");
        auto meshMeta=Meta(MeshGuid,"Shape.mesh","Mesh");Json::Value identity;
        identity["LocalID"]=Json::UInt64(41);identity["Type"]=uint16_t(eAssetType::Mesh);identity["SourceKey"]="main";identity["Name"]="Shape";
        meshMeta["SubAssets"].append(identity);Write(Root.filePath("Assets/Shape.mesh.meta"),QByteArray::fromStdString(AssetArtifactIO::JsonText(meshMeta)));
        Write(Root.filePath("Assets/Probe.hpp"),"// script-dependency-snapshot\n");
    }
    Outcome Import(const Json::Value& source,bool scene=false) {
        const auto path=scene?"World.scene":"Mesh.prefab";
        Write(Root.filePath(QString("Assets/")+path),QByteArray::fromStdString(AssetArtifactIO::JsonText(source)));
        AssetImportRequest request;request.GuidString=scene?SceneGuid:PrefabGuid;request.AssetsRoot=Root.filePath("Assets").toStdString();
        request.LibraryRoot=Root.filePath("Library").toStdString();request.SourcePath=path;request.Importer=scene?eAssetImporterType::Scene:eAssetImporterType::Prefab;
        request.MetaJson=AssetArtifactIO::JsonText(scene?Meta(SceneGuid,path,"Scene"):PrefabMeta);
        request.SourcePaths={{MeshGuid,"Shape.mesh"},{ScriptGuid,"Probe.hpp"},{PrefabGuid,"Mesh.prefab"}};
        request.Force=true;
        AssetImportJob job(request);Outcome outcome;outcome.Success=RunAssetImport(job,Task);outcome.Result=job.Result;
        if(outcome.Success) {
            Check(job.Result.Manifest.Assets.size()==1,"Prefab importer emitted more than one main asset");
            QFile file(QDir(QString::fromStdString(job.Stage)).filePath(QString::fromStdString(job.Result.Manifest.Assets[0].Path)));
            Check(file.open(QIODevice::ReadOnly),"Cannot open emitted Prefab artifact");const auto bytes=file.readAll();
            outcome.Bytes.assign(reinterpret_cast<const std::uint8_t*>(bytes.constData()),reinterpret_cast<const std::uint8_t*>(bytes.constData())+bytes.size());
        }
        return outcome;
    }
};
Json::Value Numbers(std::initializer_list<float> values) {
    Json::Value result(Json::arrayValue);for(const auto value:values)result.append(value);return result;
}
Json::Value SceneNode(std::uint64_t id,std::uint64_t parent,const char* name,std::uint64_t transformID) {
    Json::Value node;node["ID"]=Json::UInt64(id);node["ParentID"]=Json::UInt64(parent);node["Name"]=name;node["Components"]=Json::Value(Json::arrayValue);
    Json::Value transform;transform["Type"]="GameSproutRuntime.Transform";transform["ID"]=Json::UInt64(transformID);
    transform["Position"]=Numbers({0,0,0});transform["Rotation"]=Numbers({0,0,0,1});transform["Scale"]=Numbers({1,1,1});node["Components"].append(transform);return node;
}
const Json::Value& NamedNode(const Json::Value& data,const char* name) {
    for(const auto& node:data["Objects"])if(node["Name"]==name)return node;
    throw std::runtime_error(std::string("Missing imported scene node: ")+name);
}
void CheckSceneRecomposition(Fixture& fixture,std::uint64_t prefabLocalID) {
    auto oldSource=Source();auto& baseline=oldSource["Data"];
    auto& root=baseline["Objects"][0];root["Components"][0]["Position"]=Numbers({0,0,0});
    root["Components"][0]["Rotation"]=Numbers({0,0,0,1});root["Components"][0]["Scale"]=Numbers({1,1,1});
    Json::Value image;image["Type"]="GameSproutRuntime.Image";image["ID"]=Json::UInt64(4);image["Color"]=Numbers({0.2f,0.3f,0.4f,1});root["Components"].append(image);
    Json::Value script;script["ID"]=Json::UInt64(5);script["Type"]="GameSproutRuntime.ScriptComponent";script["InstanceID"]=Json::UInt64(1);
    script["ScriptTypeID"]="scene-prefab-probe";script["Source"]=Reference(ScriptGuid,1);script["Enabled"]=true;
    script["Fields"]["target"]["Kind"]="SceneObject";script["Fields"]["target"]["Value"]["ObjectFileID"]=Json::UInt64(6);
    script["Fields"]["external"]["Kind"]="SceneObject";script["Fields"]["external"]["Value"]=Json::Value();root["Components"].append(script);
    auto child=SceneNode(6,1,"Existing child",7);child["Components"][0]["Position"]=Numbers({0,1,0});baseline["Objects"].append(child);baseline["NextObjectID"]=Json::UInt64(8);
    auto expanded=baseline;std::map<std::uint64_t,std::uint64_t> ids{{1,101},{2,102},{3,103},{4,104},{5,105},{6,106},{7,107}};std::string error;
    Check(PrefabDocument::Remap(expanded,ids,error),error);expanded["NextObjectID"]=Json::UInt64(500);
    expanded["Objects"][0]["Name"]="Placed instance";expanded["Objects"][0]["ParentID"]=Json::UInt64(50);
    expanded["Objects"][0]["Components"][0]["Position"]=Numbers({8,9,10});
    expanded["Objects"][0]["Components"][2]["Color"][0]=0.9f;
    expanded["Objects"][0]["Components"][3]["Fields"]["external"]["Value"]["ObjectFileID"]=Json::UInt64(60);
    Json::Value record;record["Version"]=1;record["RootFileID"]=Json::UInt64(101);record["Source"]=Reference(PrefabGuid,prefabLocalID);
    record["Baseline"]=baseline;record["SourcePath"]="Mesh.prefab";record["SourceHash"]="stale-baseline";
    record["Mappings"]=Json::Value(Json::arrayValue);record["Overrides"]=Json::Value(Json::arrayValue);
    for(const auto& [source,scene]:ids) {
        Json::Value mapping;mapping["SourceID"]=Json::UInt64(source);mapping["SceneID"]=Json::UInt64(scene);mapping["Component"]=source!=1&&source!=6;record["Mappings"].append(mapping);
    }
    Json::Value scene;scene["Version"]=1;scene["Name"]="Stale authoring scene";scene["NextObjectID"]=Json::UInt64(500);scene["Objects"]=Json::Value(Json::arrayValue);
    scene["Objects"].append(SceneNode(50,0,"Placement parent",51));scene["Objects"].append(SceneNode(60,0,"External target",61));
    for(const auto& node:expanded["Objects"])scene["Objects"].append(node);
    scene["Annotations"]["PrefabInstances"]=Json::Value(Json::arrayValue);scene["Annotations"]["PrefabInstances"].append(record);
    Check(SceneDocument::Validate(scene,error),error);
    auto latest=oldSource;latest["Data"]["Objects"][0]["Components"][2]["Color"][0]=0.4f;
    latest["Data"]["Objects"][0]["Components"][2]["Color"][1]=0.75f;
    latest["Data"]["Objects"][1]["Components"][0]["Position"][1]=5;
    latest["Data"]["Objects"].append(SceneNode(8,1,"New source child",9));latest["Data"]["NextObjectID"]=Json::UInt64(10);
    Write(fixture.Root.filePath("Assets/Mesh.prefab"),QByteArray::fromStdString(AssetArtifactIO::JsonText(latest)));
    Write(fixture.Root.filePath("Assets/Mesh.prefab.meta"),QByteArray::fromStdString(AssetArtifactIO::JsonText(fixture.PrefabMeta)));
    const auto authorBytes=QByteArray::fromStdString(AssetArtifactIO::JsonText(scene));
    const auto imported=fixture.Import(scene,true);Check(imported.Success,"Scene Prefab recomposition failed: "+imported.Result.Error);
    Json::Value wrapper;Check(ObjectDocumentBinary::Decode(imported.Bytes,wrapper)&&wrapper["Format"]=="GameSproutDocument","Scene artifact is not the current binary document");
    const auto& runtimeScene=wrapper["Data"];Check(!runtimeScene.isMember("Annotations")&&SceneDocument::Validate(runtimeScene,error),"Scene artifact retained annotations or is invalid: "+error);
    const auto& placed=NamedNode(runtimeScene,"Placed instance");const auto& existing=NamedNode(runtimeScene,"Existing child");const auto& added=NamedNode(runtimeScene,"New source child");
    Check(placed["ID"].asUInt64()==101&&placed["ParentID"].asUInt64()==50&&placed["Components"][0]["Position"][0].asFloat()==8,"Scene recomposition changed instance root identity, parent or placement");
    Check(placed["Components"][2]["Color"][0].asFloat()==0.9f&&placed["Components"][2]["Color"][1].asFloat()==0.75f&&existing["Components"][0]["Position"][1].asFloat()==5,"Scene recomposition lost overrides or used stale inherited Prefab values");
    Check(existing["ID"].asUInt64()==106&&placed["Components"][3]["Fields"]["target"]["Value"]["ObjectFileID"].asUInt64()==106&&
          placed["Components"][3]["Fields"]["external"]["Value"]["ObjectFileID"].asUInt64()==60,"Scene recomposition changed internal mapping or external scene reference");
    Check(added["ID"].asUInt64()>=500&&added["ParentID"].asUInt64()==101&&added["Components"][0]["ID"].asUInt64()>added["ID"].asUInt64()&&runtimeScene["NextObjectID"].asUInt64()>added["Components"][0]["ID"].asUInt64(),"New Prefab source objects/components have invalid scene identities");
    bool hasPrefabDependency=false;for(const auto& dependency:imported.Result.Manifest.Dependencies)if(dependency.Path=="Mesh.prefab")hasPrefabDependency=true;
    Check(hasPrefabDependency,"Scene manifest has no Prefab source dependency edge");
    QFile author(fixture.Root.filePath("Assets/World.scene"));Check(author.open(QIODevice::ReadOnly)&&author.readAll()==authorBytes,"Scene import wrote resolved Prefab values into the authoring scene file");
    author.close();
    auto sparse=scene;
    sparse["Objects"][2]["Components"][0].removeMember("Position");sparse["Objects"][2]["Components"][0].removeMember("Rotation");
    auto& sparseBaseline=sparse["Annotations"]["PrefabInstances"][0]["Baseline"]["Objects"][0]["Components"][0];
    sparseBaseline.removeMember("Position");sparseBaseline.removeMember("Rotation");
    const auto sparseImported=fixture.Import(sparse,true);Check(sparseImported.Success,"Scene Prefab with omitted optional root TRS fields failed: "+sparseImported.Result.Error);
    Check(ObjectDocumentBinary::Decode(sparseImported.Bytes,wrapper)&&SceneDocument::Validate(wrapper["Data"],error),"Scene Prefab merge produced an invalid optional root transform: "+error);
    const auto& sparseTransform=NamedNode(wrapper["Data"],"Placed instance")["Components"][0];
    Check(!sparseTransform.isMember("Position")&&!sparseTransform.isMember("Rotation"),"Scene Prefab merge replaced omitted transform defaults with authored values/null");
}
}
int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    try {
        Fixture fixture;std::string error;
        Check(AssetMetaFormat::Validate(fixture.PrefabMeta,error),"Importer 255 Prefab Meta rejected: "+error);
        auto source=Source();const auto first=fixture.Import(source);Check(first.Success,first.Result.Error);
        const auto& artifact=first.Result.Manifest.Assets[0];
        Check(first.Result.Manifest.GuidString==PrefabGuid&&artifact.Type==eAssetType::Prefab&&artifact.SourceKey=="main"&&artifact.LocalID!=0,"Prefab GUID/main LocalID/type identity is wrong");
        Json::Value descriptor;Check(AssetArtifactIO::Parse(artifact.DescriptorJson,descriptor)&&descriptor["Encoding"]=="BinaryValueLE1"&&descriptor["Format"]=="GameSproutPrefab","Prefab descriptor is not the current structural encoding");
        Json::Value decoded;Check(PrefabDocument::Decode(first.Bytes,decoded,error),"Production prefab bytes cannot be decoded by Runtime codec: "+error);
        Check(AssetArtifactIO::JsonText(decoded)==AssetArtifactIO::JsonText(source["Data"])&&decoded["Objects"][0]["Components"][1]["Mesh"]["LocalID"].asUInt64()==41,"Prefab import changed component stable IDs or Mesh reference");
        Check(first.Result.Manifest.Dependencies.size()==1&&first.Result.Manifest.Dependencies[0].Path=="Shape.mesh"&&first.Result.Manifest.Dependencies[0].Hash.size()==64,"Prefab did not retain the Mesh source dependency edge");
        Json::Value registry;Check(AssetArtifactIO::Parse(first.Result.Manifest.SubAssetsJson,registry),"Cannot parse imported identity registry");
        fixture.PrefabMeta["SubAssets"]=registry;source["Data"]["Objects"][0]["Name"]="Renamed instance template";
        const auto second=fixture.Import(source);Check(second.Success,second.Result.Error);
        Check(second.Result.Manifest.GuidString==PrefabGuid&&second.Result.Manifest.Assets[0].LocalID==artifact.LocalID,"Reimport changed stable Prefab GUID or main LocalID");
        auto invalid=source;invalid["Format"]="Other";auto failed=fixture.Import(invalid);
        Check(!failed.Success&&!failed.Result.Error.empty()&&failed.Result.Manifest.Assets.empty(),"Prefab importer accepted a bad current format");
        invalid=source;Json::Value script;script["ID"]=Json::UInt64(4);script["Type"]="GameSproutRuntime.ScriptComponent";
        script["InstanceID"]=Json::UInt64(1);script["ScriptTypeID"]="external-reference-probe";script["Source"]=Reference(ScriptGuid,1);script["Enabled"]=true;
        script["Fields"]["target"]["Kind"]="SceneObject";script["Fields"]["target"]["Value"]["ObjectFileID"]=Json::UInt64(999);
        invalid["Data"]["Objects"][0]["Components"].append(script);invalid["Data"]["NextObjectID"]=Json::UInt64(5);
        failed=fixture.Import(invalid);Check(!failed.Success&&failed.Result.Error.find("outside")!=std::string::npos&&failed.Result.Manifest.Assets.empty(),"Prefab importer accepted an external scene reference or rejected for another reason: "+failed.Result.Error);
        invalid=source;invalid["NestedInstances"].append(Reference(PrefabGuid,artifact.LocalID));failed=fixture.Import(invalid);
        Check(!failed.Success&&failed.Result.Error.find("reserved")!=std::string::npos&&failed.Result.Manifest.Assets.empty(),"Prefab importer accepted reserved Nested composition");
        invalid=source;invalid["Base"]=Reference(PrefabGuid,artifact.LocalID);failed=fixture.Import(invalid);
        Check(!failed.Success&&failed.Result.Error.find("reserved")!=std::string::npos,"Prefab importer accepted reserved Variant base");
        invalid=source;invalid["Data"]["Objects"][0]["Components"][0].removeMember("ID");failed=fixture.Import(invalid);
        Check(!failed.Success&&failed.Result.Error.find("stable IDs")!=std::string::npos,"Prefab source accepted a component without its stable source ID");
        CheckSceneRecomposition(fixture,artifact.LocalID);
        std::cout<<"Prefab production import, Runtime binary codec, stable resource identity, dependency and rejection checks passed\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';return 1;}
}

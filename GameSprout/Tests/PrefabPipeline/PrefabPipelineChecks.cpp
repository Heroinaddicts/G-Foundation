#include "AssetImportJob.h"
#include "PrefabDocument.h"
#include "../../Module/Shared/AssetMetaFormat.h"
#include "../../Module/Shared/AssetValueBinary.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <algorithm>
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
    bool Cancel=false;
    UInt64 GetTaskId()const override{return 1;}
    bool IsCancellationRequested()const override{return Cancel;}
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
bool SameReference(const Json::Value& value,const std::string& guid,std::uint64_t id) {
    return value.isObject()&&value["Guid"]==guid&&value["LocalID"].isUInt64()&&value["LocalID"].asUInt64()==id;
}
Json::Value Source() {
    Json::Value source(Json::objectValue);source["Version"]=1;source["Format"]="GameSproutPrefab";
    source["Base"]=Json::Value();source["NestedInstances"]=Json::Value(Json::arrayValue);
    auto& data=source["Data"];data["Version"]=GameSproutRuntime::SceneDocumentVersion;data["Name"]="Prefab pipeline fixture";data["NextObjectID"]=Json::UInt64(4);
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
    std::map<std::string,std::string> SourcePaths{{MeshGuid,"Shape.mesh"},{ScriptGuid,"Probe.hpp"},{PrefabGuid,"Mesh.prefab"}};
    Fixture():Root(Temporary.path()) {
        Check(Temporary.isValid()&&Root.mkpath("Assets")&&Root.mkpath("Library"),"Cannot create temporary project");
        Write(Root.filePath("Assets/Shape.mesh"),"mesh-dependency-snapshot");
        auto meshMeta=Meta(MeshGuid,"Shape.mesh","Mesh");Json::Value identity;
        identity["LocalID"]=Json::UInt64(41);identity["Type"]=uint16_t(eAssetType::Mesh);identity["SourceKey"]="main";identity["Name"]="Shape";
        meshMeta["SubAssets"].append(identity);Write(Root.filePath("Assets/Shape.mesh.meta"),QByteArray::fromStdString(AssetArtifactIO::JsonText(meshMeta)));
        Write(Root.filePath("Assets/Probe.hpp"),"// script-dependency-snapshot\n");
    }
    Outcome Import(const Json::Value& source,bool scene=false,const AssetImportManifest* previous=nullptr,bool force=true) {
        const auto path=scene?"World.scene":"Mesh.prefab";
        Write(Root.filePath(QString("Assets/")+path),QByteArray::fromStdString(AssetArtifactIO::JsonText(source)));
        AssetImportRequest request;request.GuidString=scene?SceneGuid:PrefabGuid;request.AssetsRoot=Root.filePath("Assets").toStdString();
        request.LibraryRoot=Root.filePath("Library").toStdString();request.SourcePath=path;request.Importer=scene?eAssetImporterType::Scene:eAssetImporterType::Prefab;
        request.MetaJson=AssetArtifactIO::JsonText(scene?Meta(SceneGuid,path,"Scene"):PrefabMeta);
        request.SourcePaths=SourcePaths;
        request.HasPrevious=previous!=nullptr;if(previous)request.Previous=*previous;
        request.Force=force;
        AssetImportJob job(request);Outcome outcome;outcome.Success=RunAssetImport(job,Task);outcome.Result=job.Result;
        if(outcome.Success) {
            Check(job.Result.Manifest.Assets.size()==1,"Prefab importer emitted more than one main asset");
            const auto directory=job.Result.Cached?Root.filePath("Library/"+QString::fromStdString(job.Result.Manifest.Directory)):QString::fromStdString(job.Stage);
            QFile file(QDir(directory).filePath(QString::fromStdString(job.Result.Manifest.Assets[0].Path)));
            Check(file.open(QIODevice::ReadOnly),"Cannot open emitted Prefab artifact");const auto bytes=file.readAll();
            outcome.Bytes.assign(reinterpret_cast<const std::uint8_t*>(bytes.constData()),reinterpret_cast<const std::uint8_t*>(bytes.constData())+bytes.size());
        }
        return outcome;
    }
    AssetImportManifest Publish(const Outcome& outcome) {
        auto manifest=outcome.Result.Manifest;manifest.Generation="isolated-prefab-publication";
        Check(manifest.Assets.size()==1,"Expected one resource for isolated publication");
        auto& asset=manifest.Assets[0];asset.Path=manifest.GuidString+"_"+std::to_string(asset.LocalID)+".gsasset";
        Write(Root.filePath("Library/Artifact/"+QString::fromStdString(asset.Path)),
            QByteArray(reinterpret_cast<const char*>(outcome.Bytes.data()),static_cast<qsizetype>(outcome.Bytes.size())));
        Json::Value identities;Check(AssetArtifactIO::Parse(manifest.SubAssetsJson,identities),"Cannot retain Prefab identities");
        PrefabMeta["SubAssets"]=identities;return manifest;
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
void CheckDependencyMemoization() {
    Fixture fixture;AssetFileFingerprintCache cache;cache.Open();
    AssetImportRequest request;request.AssetsRoot=fixture.Root.filePath("Assets").toStdString();
    request.GuidString=SceneGuid;request.Fingerprints=&cache;
    AssetImportJob job(request);job.Context=&fixture.Task;
    Check(job.AddDependency("Shape.mesh"),job.Result.Error);
    const auto first=cache.GetStatistics();
    for(unsigned index=0;index<2048;++index)Check(job.AddDependency("Shape.mesh"),job.Result.Error);
    Check(cache.GetStatistics().Requests==first.Requests&&job.Result.Manifest.Dependencies.size()==1,
          "Repeated dependency reopened or reparsed its captured source/Meta");
    QByteArray bytes;
    Check(job.AddDependency("Shape.mesh",&bytes)&&bytes=="mesh-dependency-snapshot",
          "Memoized dependency did not supply requested source bytes");
    const auto hash=job.Result.Manifest.Dependencies[0].Hash;
    Check(job.AddDependency("Shape.mesh",nullptr,true)&&job.Result.Manifest.Dependencies.size()==1&&
          job.Result.Manifest.Dependencies[0].Hash==hash,"Final dependency revalidation changed a stable manifest");
    fixture.Task.Cancel=true;
    Check(!job.AddDependency("Shape.mesh")&&job.Result.Error=="Import cancelled","Memoized dependency ignored cancellation");
    fixture.Task.Cancel=false;

    AssetImportJob changedSource(request);Check(changedSource.AddDependency("Shape.mesh"),changedSource.Result.Error);
    Write(fixture.Root.filePath("Assets/Shape.mesh"),"other-source-snapshot");
    Check(!changedSource.AddDependency("Shape.mesh")&&changedSource.Result.Superseded&&changedSource.Result.Retry,
          "Dependency memo hid a source replacement");

    AssetImportJob changedMeta(request);Check(changedMeta.AddDependency("Shape.mesh"),changedMeta.Result.Error);
    Write(fixture.Root.filePath("Assets/Shape.mesh.meta"),"{}\n");
    Check(!changedMeta.AddDependency("Shape.mesh",nullptr,true)&&changedMeta.Result.Superseded&&changedMeta.Result.Retry,
          "Final dependency validation hid a Meta change");
}
void CheckRepeatedSceneReferences() {
    Fixture fixture;
    const auto import=[&](unsigned count) {
        AssetFileFingerprintCache cache;cache.Open();
        auto source=Source()["Data"];source["Objects"]=Json::Value(Json::arrayValue);
        source["NextObjectID"]=Json::UInt64(std::uint64_t(count)*3+1);
        source["Annotations"]["PrefabInstances"]=Json::Value(Json::arrayValue);
        for(unsigned index=0;index<count;++index) {
            const auto identity=std::uint64_t(index)*3+1;
            auto node=SceneNode(identity,0,"Repeated Mesh",identity+1);
            Json::Value mesh;mesh["ID"]=Json::UInt64(identity+2);mesh["Type"]="GameSproutRuntime.Mesh";
            mesh["Mesh"]=Reference(MeshGuid,41);node["Components"].append(std::move(mesh));
            source["Objects"].append(std::move(node));
        }
        Write(fixture.Root.filePath("Assets/World.scene"),QByteArray::fromStdString(AssetArtifactIO::JsonText(source)));
        AssetImportRequest request;request.Fingerprints=&cache;request.GuidString=SceneGuid;
        request.AssetsRoot=fixture.Root.filePath("Assets").toStdString();request.LibraryRoot=fixture.Root.filePath("Library").toStdString();
        request.SourcePath="World.scene";request.Importer=eAssetImporterType::Scene;
        request.MetaJson=AssetArtifactIO::JsonText(Meta(SceneGuid,"World.scene","Scene"));request.SourcePaths=fixture.SourcePaths;
        AssetImportJob job(request);Check(RunAssetImport(job,fixture.Task),job.Result.Error);
        Check(job.Result.Manifest.Dependencies.size()==1&&job.Result.Manifest.ReferencedGuids.size()==1,
              "Repeated scene references duplicated manifest edges");
        QFile file(QDir(QString::fromStdString(job.Stage)).filePath(QString::fromStdString(job.Result.Manifest.Assets[0].Path)));
        Check(file.open(QIODevice::ReadOnly),"Cannot read repeated-reference scene artifact");
        Json::Value wrapper;Check(AssetValueBinary::Decode(file.readAll(),wrapper),"Invalid repeated-reference binary");
        source.removeMember("Annotations");
        Check(AssetArtifactIO::JsonText(wrapper["Data"])==AssetArtifactIO::JsonText(source),
              "Scene fast path changed Runtime graph or retained author-only annotations");
        return cache.GetStatistics().Requests;
    };
    Check(import(1)==import(2048),"Dependency content reads grew with repeated scene references instead of unique resources");
}
void CheckMissingReferences() {
    Fixture fixture;std::string error;auto source=Source();
    AssetImportRequest racedRequest;racedRequest.AssetsRoot=fixture.Root.filePath("Assets").toStdString();
    racedRequest.SourcePaths=fixture.SourcePaths;racedRequest.GuidString=PrefabGuid;
    AssetImportJob raced(racedRequest);
    Check(raced.TrackGuidReference(MeshGuid)=="Shape.mesh","Cannot capture live GUID dependency fixture");
    Check(QFile::remove(fixture.Root.filePath("Assets/Shape.mesh"))&&
          QFile::remove(fixture.Root.filePath("Assets/Shape.mesh.meta")),"Cannot remove isolated Mesh source");
    Check(!raced.AddDependency("Shape.mesh")&&raced.Result.Superseded&&raced.Result.Retry,
          "A dependency deleted between GUID resolution and reading did not request a fresh Missing snapshot");
    const auto stalePath=fixture.Import(source);
    Check(stalePath.Success&&stalePath.Result.Manifest.GuidSourcePaths.count(MeshGuid)&&
          stalePath.Result.Manifest.GuidSourcePaths.at(MeshGuid).empty(),
          "A stale source-path snapshot made a deleted Mesh fatal: "+stalePath.Result.Error);
    fixture.SourcePaths.erase(MeshGuid);
    const auto missing=fixture.Import(source);Check(missing.Success,"Missing Mesh should not invalidate Prefab: "+missing.Result.Error);
    Json::Value decoded;Check(PrefabDocument::Decode(missing.Bytes,decoded,error),error);
    Check(SameReference(decoded["Objects"][0]["Components"][1]["Mesh"],MeshGuid,41),"Missing Mesh reference was cleared or rewritten");
    const auto& manifest=missing.Result.Manifest;
    Check(std::find(manifest.ReferencedGuids.begin(),manifest.ReferencedGuids.end(),MeshGuid)!=manifest.ReferencedGuids.end()&&
          manifest.GuidSourcePaths.count(MeshGuid)&&manifest.GuidSourcePaths.at(MeshGuid).empty(),"Missing Mesh lost its GUID availability dependency");
    Check(!manifest.Diagnostics.empty(),"Missing Mesh should retain an import diagnostic");
    auto published=fixture.Publish(missing);AssetImportManifest roundtrip;
    const auto encoded=AssetArtifactIO::ToJson(published);
    Check(encoded["Version"]==3&&AssetArtifactIO::FromJson(encoded,roundtrip)&&
          roundtrip.ReferencedGuids==published.ReferencedGuids&&roundtrip.GuidSourcePaths==published.GuidSourcePaths,
          "Current Manifest format lost reference GUIDs or missing-source availability");
    auto malformedManifest=encoded;malformedManifest["ReferencedGuids"].append("not-a-guid");
    Check(!AssetArtifactIO::FromJson(malformedManifest,roundtrip),"Manifest accepted a malformed reference GUID");
    const auto unchanged=fixture.Import(source,false,&published,false);
    Check(unchanged.Success&&unchanged.Result.Cached,"Unchanged Missing reference did not retain normal cache reuse: "+unchanged.Result.Error);

    Write(fixture.Root.filePath("Assets/Restored/Shape.mesh"),"restored-mesh-source");
    auto metadata=Meta(MeshGuid,"Restored/Shape.mesh","Mesh");Json::Value identity;
    identity["LocalID"]=Json::UInt64(41);identity["Type"]=uint16_t(eAssetType::Mesh);identity["SourceKey"]="main";identity["Name"]="Restored Shape";
    metadata["SubAssets"].append(identity);
    Write(fixture.Root.filePath("Assets/Restored/Shape.mesh.meta"),QByteArray::fromStdString(AssetArtifactIO::JsonText(metadata)));
    fixture.SourcePaths[MeshGuid]="Restored/Shape.mesh";
    const auto restored=fixture.Import(source,false,&published,false);
    Check(restored.Success&&!restored.Result.Cached,"Restoring the same GUID at a new path reused stale Missing data: "+restored.Result.Error);
    Check(restored.Result.Manifest.GuidSourcePaths.at(MeshGuid)=="Restored/Shape.mesh"&&
          restored.Result.Manifest.Dependencies.size()==1&&restored.Result.Manifest.Dependencies[0].Path=="Restored/Shape.mesh",
          "Restored Mesh failed to replace the missing availability/path dependency");
    Check(PrefabDocument::Decode(restored.Bytes,decoded,error)&&SameReference(decoded["Objects"][0]["Components"][1]["Mesh"],MeshGuid,41),
          "Restored Mesh changed its stable resource reference");

    auto invalid=source;invalid["Data"]["Objects"][0]["Components"][1]["Mesh"]["Guid"]="bad-guid";
    auto rejected=fixture.Import(invalid);Check(!rejected.Success,"Missing-reference support accepted malformed GUID data");
    invalid=source;invalid["Data"]["Objects"][0]["Components"][1]["Mesh"]["LocalID"]=Json::UInt64(0);
    rejected=fixture.Import(invalid);Check(!rejected.Success,"Missing-reference support accepted a zero LocalID");
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
    Json::Value scene;scene["Version"]=GameSproutRuntime::SceneDocumentVersion;scene["Name"]="Stale authoring scene";scene["NextObjectID"]=Json::UInt64(500);scene["Objects"]=Json::Value(Json::arrayValue);
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

    Check(QFile::remove(fixture.Root.filePath("Assets/Mesh.prefab"))&&QFile::remove(fixture.Root.filePath("Assets/Mesh.prefab.meta")),
          "Cannot remove isolated Prefab source");
    fixture.SourcePaths.erase(PrefabGuid);
    const auto missingSource=fixture.Import(scene,true);
    Check(missingSource.Success,"Missing Prefab source invalidated the existing scene instance: "+missingSource.Result.Error);
    Check(ObjectDocumentBinary::Decode(missingSource.Bytes,wrapper)&&SceneDocument::Validate(wrapper["Data"],error),
          "Missing Prefab source produced an invalid runtime scene: "+error);
    const auto& retained=NamedNode(wrapper["Data"],"Placed instance");
    Check(retained["ID"].asUInt64()==101&&retained["ParentID"].asUInt64()==50&&
          retained["Components"][0]["Position"][0].asFloat()==8&&retained["Components"][2]["Color"][0].asFloat()==0.9f,
          "Missing Prefab source removed its materialized instance, placement or Override");
    Check(missingSource.Result.Manifest.GuidSourcePaths.count(PrefabGuid)&&
          missingSource.Result.Manifest.GuidSourcePaths.at(PrefabGuid).empty()&&!missingSource.Result.Diagnostics.empty(),
          "Missing Prefab source lost its recoverable GUID dependency/diagnostic");
    auto currentWithDeletedMesh=scene;Check(QFile::remove(fixture.Root.filePath("Assets/Shape.mesh"))&&
          QFile::remove(fixture.Root.filePath("Assets/Shape.mesh.meta")),"Cannot remove Mesh from scene fixture");
    fixture.SourcePaths.erase(MeshGuid);
    const auto missingBoth=fixture.Import(currentWithDeletedMesh,true);
    Check(missingBoth.Success,"Scene with missing Mesh and Prefab source failed: "+missingBoth.Result.Error);
    Check(ObjectDocumentBinary::Decode(missingBoth.Bytes,wrapper),"Cannot decode scene with missing sources");
    const auto& missingMesh=NamedNode(wrapper["Data"],"Placed instance")["Components"][1]["Mesh"];
    Check(SameReference(missingMesh,MeshGuid,41),
          "Scene with missing Mesh and Prefab source lost the original resource reference: "+AssetArtifactIO::JsonText(missingMesh));
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
        CheckMissingReferences();
        CheckDependencyMemoization();
        CheckRepeatedSceneReferences();
        std::cout<<"Prefab production import, Runtime binary codec, stable resource identity, dependency and rejection checks passed\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';return 1;}
}

#include "PrefabManager.h"
#include "../Shared/AssetSourceIO.h"
#include "PrefabRecords.h"
#include "PrefabScriptFields.h"
#include <QSaveFile>

namespace {
void PreserveSourcePlacement(const Json::Value& source,Json::Value& target) {
    const auto sourceNodes=PrefabDefinition::Objects(source["Objects"]);const auto root=PrefabDefinition::RootID(source);
    if(!sourceNodes.count(root))throw std::runtime_error("Prefab source root is missing");
    const auto& original=sourceNodes.at(root);const auto components=PrefabDefinition::Objects(original["Components"]);
    for(auto& node:target["Objects"])if(node["ID"].asUInt64()==root) {
        node["Name"]=original["Name"];
        for(auto& component:node["Components"])if(components.count(component["ID"].asUInt64())) {
            const auto& old=components.at(component["ID"].asUInt64());
            if(component["Type"]=="GameSproutRuntime.Transform"){component["Position"]=old["Position"];component["Rotation"]=old["Rotation"];}
            if(component["Type"]=="GameSproutRuntime.RectTransform")component["AnchoredPosition"]=old["AnchoredPosition"];
        }
    }
}
}
bool PrefabManager::ApplyOverrides(uint64_t objectID,std::string& error) {
    if(!Ready(error))return false;Json::Value record;if(!FindRecord(objectID,record,error))return false;
    const GameSproutRuntime::ResourceReference reference{record["Source"]["Guid"].asString(),record["Source"]["LocalID"].asUInt64()};
    Json::Value source;std::string path,hash;
    if(!Load(reference,source,path,hash,false,error))return false;
    SceneSubtreeDocument snapshot;Json::Value current;if(!Current(record,snapshot,current,error))return false;
    _Busy=true;struct Reset{bool& value;~Reset(){value=false;}} reset{_Busy};
    try {
        const auto schemas=_Runtime->GetScriptTypes();
        if(!PrefabScriptFields::Align(record,current,&source,schemas,error))return false;
        const auto patches=PrefabRecords::EffectivePatches(record,current);
        auto changed=PrefabDefinition::Merge(record["Baseline"],current,source,&patches);
        PreserveSourcePlacement(source,changed);
        std::map<uint64_t,uint64_t> canonicalIDs;
        changed=PrefabDefinition::Canonicalize(std::move(changed),true,&canonicalIDs);
        const auto document=PrefabDefinition::Source(changed);Json::Value verified;
        // Reject external scene refs before touching either source or instance.
        if(!PrefabDefinition::Validate(document,verified,error))return false;
        const QString full=QDir(QString::fromStdString(_Project->GetProjectRoot()+"/Assets")).filePath(QString::fromStdString(path));
        QFile old(full);if(!old.open(QIODevice::ReadOnly)){error="无法读取Apply目标。";return false;}
        const auto before=old.readAll();
        if(old.error()!=QFileDevice::NoError){error="读取Apply目标失败。";return false;}
        old.close();
        if(AssetArtifactIO::Hash(before)!=hash){error="Prefab源文件被外部修改，请刷新后重试。";return false;}
        const auto bytes=QByteArray::fromStdString(AssetSourceIO::JsonText(document));
        if(!AssetArtifactIO::Write(full,bytes,error))return false;
        Json::Value mappings(Json::arrayValue);
        for(const auto& [from,to]:canonicalIDs){Json::Value item;item["Old"]=Json::UInt64(from);item["New"]=Json::UInt64(to);mappings.append(item);}
        if(!Replace(record,changed,true,AssetArtifactIO::Hash(bytes),error,&mappings,&schemas)) {
            std::string rollback;if(!AssetArtifactIO::Write(full,before,rollback))error+="；恢复源文件失败："+rollback;
            return false;
        }
        _Busy=false;
        std::string refresh;
        if(!RefreshSource(reference.Guid,refresh)&&_Console)
            _Console->ConsoleLog(__FILE__,__LINE__,eConsoleLog::Warning,"Prefab已保存，部分实例需要处理更新冲突："+refresh);
        error.clear();QueueNotify();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}

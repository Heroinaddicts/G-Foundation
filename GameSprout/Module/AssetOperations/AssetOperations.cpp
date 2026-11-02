#include "AssetOperations.h"
#include "IAssetsManager.h"
#include "ISceneManager.h"
#include "IPrefabStage.h"
#include "IPlayMode.h"
#include "IRuntimeHost.h"
#include "ISelectionManager.h"
#include <QThread>
#include <algorithm>
#include <set>

namespace {
bool Contains(const AssetDeleteTarget& parent,const std::string& path) {
    return path==parent.Path||(parent.Directory&&path.starts_with(parent.Path+'/'));
}
}
AssetOperations::~AssetOperations(){Release(nullptr);}
void AssetOperations::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api) {
    auto* editor=api?api->_EditorApi:nullptr;
    _Assets=editor?editor->GetGameSproutModule<IAssetsManager>("AssetsManager"):nullptr;
    _Scenes=editor?editor->GetGameSproutModule<ISceneManager>("SceneManager"):nullptr;
    _Stage=editor?editor->GetGameSproutModule<IPrefabStage>("PrefabStage"):nullptr;
    _Play=editor?editor->GetGameSproutModule<IPlayMode>("PlayMode"):nullptr;
    _Host=editor?editor->GetGameSproutModule<IRuntimeHost>("RuntimeHost"):nullptr;
    _Selection=editor?editor->GetGameSproutModule<ISelectionManager>("SelectionManager"):nullptr;
    _Runtime=editor?editor->_Runtime:nullptr;
}
bool AssetOperations::Initialize(GameSproutEditor::GameSproutModuleApi*) {
    _OwnerThread=QThread::currentThread();return _Assets&&_Scenes&&_Stage&&_Play&&_Host&&_Runtime;
}
bool AssetOperations::Launch(GameSproutEditor::GameSproutModuleApi*) {
    if(!_OwnerThread)return false;
    _AssetSubscribed=_Assets->RegisterAssetListener(this);
    _SceneSubscribed=_Scenes->RegisterSceneListener(this);
    return _AssetSubscribed&&_SceneSubscribed;
}
void AssetOperations::LaunchFinished(GameSproutEditor::GameSproutModuleApi*) {RefreshProtectedSource();}
void AssetOperations::Update(GameSproutEditor::GameSproutModuleApi*) {}
void AssetOperations::Release(GameSproutEditor::GameSproutModuleApi*) {
    if(_AssetSubscribed&&_Assets)_Assets->UnregisterAssetListener(this);
    if(_SceneSubscribed&&_Scenes)_Scenes->UnregisterSceneListener(this);
    _AssetSubscribed=_SceneSubscribed=false;_ProtectedSceneGuid.clear();_ProtectedScenePath.clear();
    _Assets=nullptr;_Scenes=nullptr;_Stage=nullptr;_Play=nullptr;_Host=nullptr;_Runtime=nullptr;
    _Selection=nullptr;_OwnerThread=nullptr;_Busy=false;
}
void AssetOperations::RefreshProtectedSource() const {
    if(!_Scenes||!_Assets)return;
    const auto path=_Scenes->GetMainScenePath();
    if(path!=_ProtectedScenePath){_ProtectedScenePath=path;_ProtectedSceneGuid.clear();}
    if(path.empty()||!_ProtectedSceneGuid.empty())return;
    // Initial snapshot / Save As discovery only. Subsequent moves follow GUIDs,
    // because the open document's saved path can legitimately lag a disk rename.
    for(const auto* meta:_Assets->GetAssetMetas())if(meta&&meta->GetPath()==path) {
        _ProtectedSceneGuid=meta->_Guid.GetGuidString();break;
    }
}
void AssetOperations::OnAssetCreate(const AssetEvent& event) {
    RefreshProtectedSource();
    if(_ProtectedSceneGuid.empty()&&!_ProtectedScenePath.empty()&&
       (event.Path==_ProtectedScenePath||event.OldPath==_ProtectedScenePath))
        _ProtectedSceneGuid=event.GuidValue.GetGuidString();
}
void AssetOperations::OnAssetModify(const AssetEvent& event) {OnAssetCreate(event);}
bool AssetOperations::Ready(std::string& error) const {
    error.clear();
    if(!_OwnerThread||QThread::currentThread()!=_OwnerThread||!_Assets||!_Scenes||!_Stage||!_Play||!_Host||!_Runtime) {
        error="资源操作服务未就绪，或不在所属主线程。";return false;
    }
    if(_Busy||_Runtime->IsExecuting()||_Host->IsFrameActive()||_Play->GetState().State!=EditorPlayState::Edit||_Scenes->HasTransformEdit()) {
        error="请先停止运行并结束当前编辑操作，再删除资源。";return false;
    }
    return true;
}
bool AssetOperations::PrepareDelete(const std::vector<std::string>& guids,AssetDeletePlan& plan,std::string& error) const {
    plan={};if(!Ready(error))return false;
    if(guids.empty()){error="请选择要删除的源资源。";return false;}
    std::set<std::string> unique;
    for(const auto& guid:guids) {
        if(!unique.insert(guid).second)continue;
        auto* meta=_Assets->QueryAsset(Guid(guid));
        if(!meta){error="所选资源已删除，请重新选择。";return false;}
        const auto path=meta->GetPath();
        if(path.empty()||path=="."||path==".."||path.starts_with('/')||path.starts_with("../")) {
            error="不能删除 Assets 根目录。";return false;
        }
        plan.Sources.push_back({guid,path,meta->_ImporterType==eAssetImporterType::Folder});
    }
    std::sort(plan.Sources.begin(),plan.Sources.end(),[](const auto& a,const auto& b){return a.Path<b.Path;});
    std::vector<AssetDeleteTarget> roots;
    for(const auto& target:plan.Sources)
        if(std::none_of(roots.begin(),roots.end(),[&](const auto& parent){return Contains(parent,target.Path);}))roots.push_back(target);
    plan.Sources.swap(roots);
    RefreshProtectedSource();
    const auto stage=_Stage->GetState();
    const auto scenePath=_Scenes->GetMainScenePath();
    for(const auto& target:plan.Sources) {
        const auto protects=[&](const std::string& guid) {
            if(guid.empty())return false;
            if(target.Guid==guid)return true;
            const auto* source=_Assets->QueryAsset(Guid(guid));
            return source&&Contains(target,source->GetPath());
        };
        if((!scenePath.empty()&&Contains(target,scenePath))||protects(_ProtectedSceneGuid)||
           (stage.Active&&(Contains(target,stage.Path)||protects(stage.Guid)))) {
            error="所选资源包含当前打开的场景或 Prefab。请先关闭该文档或另存到其他位置，再删除。";
            plan={};return false;
        }
    }
    return !plan.Sources.empty();
}
bool AssetOperations::Delete(const AssetDeletePlan& plan,std::string& error) {
    std::vector<std::string> guids;
    for(const auto& source:plan.Sources)guids.push_back(source.Guid);
    AssetDeletePlan current;
    if(!PrepareDelete(guids,current,error))return false;
    if(current.Sources!=plan.Sources){error="确认期间资源已移动或变化，未执行删除。请重新选择。";return false;}
    struct Scope{bool& Busy;explicit Scope(bool& busy):Busy(busy){Busy=true;}~Scope(){Busy=false;}} scope(_Busy);
    std::size_t completed=0;
    for(const auto& target:current.Sources) {
        if(!_Assets->DeleteSourceAsset(Guid(target.Guid),target.Path,error)) {
            if(completed) {
                if(_Selection)_Selection->SetAssetSelection({});
                error="已有 "+std::to_string(completed)+" 项移入废纸篓；其余未删除。 "+error;
            }
            return false;
        }
        ++completed;
    }
    if(_Selection)_Selection->SetAssetSelection({});
    return true;
}

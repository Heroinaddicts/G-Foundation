#include "StaticAuthoring.h"
#include "ISceneManager.h"
#include "IPlayMode.h"

StaticAuthoring::~StaticAuthoring(){Release(nullptr);}
void StaticAuthoring::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api) {
    auto* editor=api?api->_EditorApi:nullptr;
    _Scenes=editor?editor->GetGameSproutModule<ISceneManager>("SceneManager"):nullptr;
    _Play=editor?editor->GetGameSproutModule<IPlayMode>("PlayMode"):nullptr;
    _Runtime=editor?editor->_Runtime:nullptr;
}
bool StaticAuthoring::Initialize(GameSproutEditor::GameSproutModuleApi*) {return _Scenes&&_Runtime;}
void StaticAuthoring::Release(GameSproutEditor::GameSproutModuleApi*) {
    _Scenes=nullptr;_Play=nullptr;_Runtime=nullptr;_Busy=false;
}
GameSproutRuntime::StaticFlags StaticAuthoring::GetSupportedFlags() const {
    return GameSproutRuntime::StaticFlags::ShadowCasterStatic;
}
std::vector<StaticFlagDefinition> StaticAuthoring::GetFlagDefinitions() const {
    using GameSproutRuntime::StaticFlags;
    return {
        {StaticFlags::ContributeGI,"Contribute GI","参与全局光照预计算；当前尚未实现光照烘焙。",false},
        {StaticFlags::OccluderStatic,"Occluder Static","作为静态遮挡物参与预计算；当前尚未实现遮挡烘焙。",false},
        {StaticFlags::BatchingStatic,"Batching Static","允许参与静态合批；当前尚未实现静态合批。",false},
        {StaticFlags::NavigationStatic,"Navigation Static","参与导航网格烘焙；当前尚未实现导航烘焙。",false},
        {StaticFlags::OccludeeStatic,"Occludee Static","作为可被遮挡的静态对象参与预计算；当前尚未实现遮挡烘焙。",false},
        {StaticFlags::OffMeshLinkGeneration,"Off Mesh Link Generation","允许生成导航跨越连接；当前尚未实现导航烘焙。",false},
        {StaticFlags::ReflectionProbeStatic,"Reflection Probe Static","参与反射探针烘焙；当前尚未实现反射探针烘焙。",false},
        {StaticFlags::ShadowCasterStatic,"Shadow Caster Static","适用于建筑、地形等静止投影物。方向光开启缓存静态阴影后，可跨帧复用其阴影。",true}
    };
}
bool StaticAuthoring::SetFlags(std::uint64_t objectID,GameSproutRuntime::StaticFlags values,
    GameSproutRuntime::StaticFlags changedFlags,bool includeChildren,std::string& error) {
    error.clear();
    if(!_Scenes||!_Runtime||_Busy||_Runtime->IsExecuting()) {error="静态标志只能在空闲编辑阶段修改。";return false;}
    if((_Play&&_Play->GetState().State!=EditorPlayState::Edit)||_Scenes->IsPlaySessionActive()) {error="运行期间不能修改静态标志。";return false;}
    if(!GameSproutRuntime::IsValidStaticFlags(values)||!GameSproutRuntime::IsValidStaticFlags(changedFlags)) {error="StaticFlags 包含未知标志。";return false;}
    auto* scene=_Scenes->GetActiveScene();auto* object=_Runtime->QueryGameObject(objectID);
    if(!object||object==scene||object->GetScene()!=scene) {error="静态标志需要当前文档中的普通节点。";return false;}
    const auto requested=static_cast<std::uint32_t>(values)&static_cast<std::uint32_t>(changedFlags);
    const auto supported=static_cast<std::uint32_t>(GetSupportedFlags());
    std::vector<GameSproutRuntime::IGameObject*> pending{object};
    while(!pending.empty()) {
        auto* target=pending.back();pending.pop_back();
        if(requested&~static_cast<std::uint32_t>(target->GetStaticFlags())&~supported) {
            error="对应的静态功能尚未实现，不能启用此标志；已有标志可以清除。";return false;
        }
        if(includeChildren)for(std::size_t index=0;index<target->GetChildCount();++index)pending.push_back(target->GetChild(index));
    }
    _Busy=true;struct Reset{bool& Value;~Reset(){Value=false;}} reset{_Busy};
    return _Scenes->SetObjectStaticFlags(objectID,values,changedFlags,includeChildren,error);
}

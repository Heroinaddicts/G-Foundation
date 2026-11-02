#include "GameSproutRuntime.h"
#include "Scene.h"
#include "Api/GameSproutRuntimeExport.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace GameSproutRuntime {
Runtime::~Runtime() {
    ResetPhysics();
    _ShuttingDown=true;
    _Listeners.clear(); _Commands.clear(); _Events.clear(); _PendingInitializations.clear();
    while (!_Scenes.empty()) DestroyNow(_Scenes.back()->State());
    // 包括尚未挂载的创建请求，保证异常/关闭时没有遗留对象。
    while (!_Nodes.empty()) DestroyNow(*_Nodes.begin()->second);
    ReleaseGraphics();
}
std::uint64_t Runtime::NextID() {
    if (_NextID==std::numeric_limits<std::uint64_t>::max()) return 0;
    return ++_NextID;
}
bool Runtime::RegisterNode(GameObjectState& state) {
    _Nodes.emplace(state.ID,&state);
    try { _ByPointer.emplace(state.Object,&state);_TaggedObjects[state.TagID].insert(state.ID); }
    catch (...) { ForgetObjectTag(state);_ByPointer.erase(state.Object);_Nodes.erase(state.ID); throw; }
    return true;
}
GameObjectState* Runtime::Find(std::uint64_t id) const {
    const auto found=_Nodes.find(id);return found==_Nodes.end()?nullptr:found->second;
}
GameObjectState* Runtime::Find(IGameObject* object) const {
    const auto found=_ByPointer.find(object);return found==_ByPointer.end()?nullptr:found->second;
}
IGameObject* Runtime::QueryGameObject(std::uint64_t id) const {
    GameObjectState* state=Find(id);return state?state->Object:nullptr;
}
IScene* Runtime::GetScene(std::size_t index) const { return index<_Scenes.size()?_Scenes[index]:nullptr; }
bool Runtime::IsUsable(const GameObjectState* object) const {
    if (_ShuttingDown || !object || object->Destroying) return false;
    for (const GameObjectState* node=object;node;node=node->Parent) if(node->PendingDestroy) return false;
    return !object->SceneOwner || !object->SceneOwner->State().PendingDestroy;
}
bool Runtime::IsInScene(const GameObjectState* object) const {
    if(!IsUsable(object) || !object->SceneOwner) return false;
    const GameObjectState* root=object;
    for(;;) {
        if(root->PendingAttach || root->Destroying || root->PendingDestroy) return false;
        if(!root->Parent) break;
        root=root->Parent;
    }
    return root->Object==object->SceneOwner && Find(root->Object)==root;
}
IScene* Runtime::CreateScene(const std::string& name) {
    if (_ShuttingDown) return nullptr;
    RuntimeOperation operation(*this);
    const auto id=NextID(); if(!id) return nullptr;
    Scene* scene=nullptr;
    bool registered=false;
    try {
        scene=new Scene(*this,name,id);
        RegisterNode(scene->State());registered=true;
        if(!scene->State().InitializeTransform()) throw std::runtime_error("Cannot create scene Transform");
        _Scenes.push_back(scene);
        scene->State().Initializing=false;
        Notify(scene->State(),SceneNodeEventType::Create);
        return scene;
    } catch (...) {
        if (registered) { ForgetObjectTag(scene->State());_Nodes.erase(id);_ByPointer.erase(scene); }
        delete scene;
        return nullptr;
    }
}
IGameObject* Runtime::CreateGameObject(const std::string& name,IGameObject* parent) {
    if(!parent) return CreateObject(nullptr,name,nullptr);
    GameObjectState* parentState=Find(parent);
    return IsUsable(parentState)?CreateObject(parentState->SceneOwner,name,parent):nullptr;
}
IGameObject* Runtime::CreateObject(Scene* scene,const std::string& name,IGameObject* parent) {
    GameObjectState* parentState=Find(parent);
    if(_ShuttingDown || (parent && !IsUsable(parentState)) ||
       (scene && (!IsUsable(Find(scene)) || !parentState || parentState->SceneOwner!=scene))) return nullptr;
    const bool deferred=_Depth!=0 || _Flushing;
    RuntimeOperation operation(*this);
    const auto id=NextID();if(!id)return nullptr;
    GameObject* object=nullptr;
    bool registered=false;
    try {
        object=new GameObject(*this,scene,name,id);
        RegisterNode(object->State());registered=true;
        _DetachedObjects.insert(id);
        if(!object->State().InitializeTransform()) throw std::runtime_error("Cannot create object Transform");
        object->State().Initializing=false;
        if(parent && deferred) {
            object->State().PendingAttach=true;
            _Commands.push_back({CommandType::Attach,id,parentState->ID,false});
        } else if(parent && !ReparentNow(object->State(),parentState,false))
            throw std::runtime_error("Cannot attach created object");
        return object;
    } catch (...) {
        if(registered) { ForgetObjectTag(object->State());_DetachedObjects.erase(id);_Nodes.erase(id);_ByPointer.erase(object); }
        delete object;
        return nullptr;
    }
}
bool Runtime::DestroyScene(IScene* scene) {
    RuntimeOperation operation(*this);
    GameObjectState* node=Find(scene);
    return node && node->SceneOwner==scene && RequestDestroy(node,true);
}
void Runtime::Update(float deltaSeconds) {
    TickComponents(deltaSeconds,false);
}
void Runtime::FixedUpdate(float fixedDeltaSeconds) {
    if(fixedDeltaSeconds<=0) return;
    TickComponents(fixedDeltaSeconds,true);
}
void Runtime::TickComponents(float deltaSeconds,bool fixed) {
    if (_ShuttingDown || _Updating || _Depth || _ConstructionDepth || _InitializingComponents || !std::isfinite(deltaSeconds) || deltaSeconds<0.0f) return;
    _Updating=true;
    try {
        std::string initializationError;
        InitializePendingComponents(initializationError,deltaSeconds>0);
        if(!_ConstructionDepth) {
            RuntimeOperation operation(*this);
            std::vector<std::uint64_t> objects;
            objects.reserve(_Nodes.size());
            for(const auto& entry:_Nodes) objects.push_back(entry.first);
            for(auto id:objects) {
                GameObjectState* node=Find(id);
                if(!IsInScene(node) || !node->IsActiveInHierarchy() || !node->SceneOwner->IsSimulationEnabled()) continue;
                const auto components=node->Components;
                for(const auto& component:components) {
                    if(!IsUsable(node) || !node->SceneOwner->IsSimulationEnabled()) break;
                    const auto current=std::find_if(node->Components.begin(),node->Components.end(),[&](const auto& item){return item.Serial==component.Serial;});
                    if(current==node->Components.end() || current->PendingRemove || !current->Initialized || current->InitializationFailed) continue;
                    try {
                        if(fixed) component.Pointer->FixedUpdate(this,deltaSeconds);
                        else component.Pointer->Update(this,deltaSeconds);
                    } catch (...) {}
                }
            }
        }
        InitializePendingComponents(initializationError,deltaSeconds>0);
        if(fixed)StepPhysics(deltaSeconds);
        DispatchAnimationEvents();
        DispatchEvents();
    } catch (...) { _Updating=false;throw; }
    _Updating=false;
}
void Runtime::Render() {
    if (_ShuttingDown || _Depth || _ConstructionDepth || _InitializingComponents) return;
    RuntimeOperation operation(*this);
    RenderWorld(); RenderUI();
}
void Runtime::RenderWorld() {
    // 显式 View 的 3D 绘制已由 CreateRenderer/IRuntimeRenderer::Render 提供。
    // 此旧入口没有目标或相机，不能自行替换 Editor 观察相机或推测 Surface。
}
void Runtime::RenderUI() {
    // TODO GRHI：完成 Canvas 布局、裁剪与 UI 绘制提交；不创建 Qt 窗口或 Editor 控件。
}
}
extern "C" GAMESPROUT_RUNTIME_API GameSproutRuntime::IGameSproutRuntime*
GAMESPROUT_RUNTIME_CALL CreateGameSproutRuntime() noexcept {
    try { return new GameSproutRuntime::Runtime(); }
    catch (...) { return nullptr; }
}

#include "GameSproutRuntime.h"
#include "Scene.h"
#include "Api/GameSproutRuntimeExport.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace GameSproutRuntime {
Runtime::~Runtime() {
    _ShuttingDown=true;
    _Listeners.clear(); _Commands.clear(); _Events.clear();
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
    try { _ByPointer.emplace(state.Object,&state); }
    catch (...) { _Nodes.erase(state.ID); throw; }
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
        if (registered) { _Nodes.erase(id);_ByPointer.erase(scene); }
        delete scene;
        return nullptr;
    }
}
IGameObject* Runtime::CreateObject(Scene* scene,const std::string& name,IGameObject* parent) {
    GameObjectState* parentState=Find(parent);
    if (!scene || !IsUsable(parentState) || parentState->SceneOwner!=scene || !IsUsable(Find(scene))) return nullptr;
    const bool deferred=_Depth!=0 || _Flushing;
    RuntimeOperation operation(*this);
    const auto id=NextID();if(!id)return nullptr;
    GameObject* object=nullptr;
    bool registered=false;
    try {
        object=new GameObject(*this,scene,name,id);
        RegisterNode(object->State());registered=true;
        if(!object->State().InitializeTransform()) throw std::runtime_error("Cannot create object Transform");
        if(deferred) {
            object->State().PendingAttach=true;
            _Commands.push_back({CommandType::Attach,id,parentState->ID,false});
        } else {
            // 返回前完成可失败的容器分配，不能让屏障析构在返回指针后销毁创建结果。
            parentState->Children.push_back(&object->State());
            object->State().Parent=parentState;
            object->State().Initializing=false;
            Notify(object->State(),SceneNodeEventType::Create);
        }
        return object;
    } catch (...) {
        if(registered) { _Nodes.erase(id);_ByPointer.erase(object); }
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
    if (_ShuttingDown || _Updating || _Depth || !std::isfinite(deltaSeconds) || deltaSeconds<0.0f) return;
    _Updating=true;
    try {
        {
            RuntimeOperation operation(*this);
            std::vector<std::uint64_t> objects;
            objects.reserve(_Nodes.size());
            for(const auto& entry:_Nodes) objects.push_back(entry.first);
            for(auto id:objects) {
                GameObjectState* node=Find(id);
                if(!IsUsable(node) || node->PendingAttach || !node->IsActiveInHierarchy()) continue;
                const auto components=node->Components;
                for(const auto& component:components) {
                    if(!IsUsable(node)) break;
                    const auto current=std::find_if(node->Components.begin(),node->Components.end(),[&](const auto& item){return item.Serial==component.Serial;});
                    if(current==node->Components.end() || current->PendingRemove) continue;
                    try { component.Pointer->Update(this,deltaSeconds); } catch (...) {}
                }
            }
        }
        DispatchAnimationEvents();
        DispatchEvents();
    } catch (...) { _Updating=false;throw; }
    _Updating=false;
}
void Runtime::Render() {
    if (_ShuttingDown || _Depth) return;
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

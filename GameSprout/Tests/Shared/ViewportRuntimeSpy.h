#pragma once
#include "RuntimeRenderApi.h"
#include <stdexcept>
#include <algorithm>

// Observe the native-window boundary without exposing a GRHI device to View tests.
class ViewportRuntimeSpy final : public GameSproutRuntime::IGameSproutRuntime {
public:
    GameSproutRuntime::IGameSproutRuntime* Real=nullptr;
    int NativeCalls=0;
    bool RejectNativeCalls=false;
    std::vector<GameSproutRuntime::IRuntimeListener*> Listeners;
    GameSproutRuntime::IRuntimeViewport* CreateViewport(const GameSproutRuntime::NativeWindow& window,
        GameSproutRuntime::IRuntimeResourceProvider* resources,std::string& error) override {
        ++NativeCalls;
        if(RejectNativeCalls)throw std::runtime_error("Headless View passed a fake native window to Runtime");
        return Real?Real->CreateViewport(window,resources,error):nullptr;
    }
    GameSproutRuntime::IRuntimeViewport* CreateOffscreenRenderer(std::uint32_t width,std::uint32_t height,
        GameSproutRuntime::IRuntimeResourceProvider* resources,std::string& error) override {
        return Real?Real->CreateOffscreenRenderer(width,height,resources,error):nullptr;
    }
    GameSproutRuntime::IScene* CreateScene(const std::string& name) override{return Real?Real->CreateScene(name):nullptr;}
    bool DestroyScene(GameSproutRuntime::IScene* scene) override{return Real&&Real->DestroyScene(scene);}
    std::size_t GetSceneCount() const override{return Real?Real->GetSceneCount():0;}
    GameSproutRuntime::IScene* GetScene(std::size_t index) const override{return Real?Real->GetScene(index):nullptr;}
    GameSproutRuntime::IGameObject* QueryGameObject(std::uint64_t id) const override{return Real?Real->QueryGameObject(id):nullptr;}
    bool RegisterListener(GameSproutRuntime::IRuntimeListener* listener) override{
        if(Real)return Real->RegisterListener(listener);
        if(!listener||std::find(Listeners.begin(),Listeners.end(),listener)!=Listeners.end())return false;
        Listeners.push_back(listener);return true;
    }
    void UnregisterListener(GameSproutRuntime::IRuntimeListener* listener) override{if(Real)Real->UnregisterListener(listener);else std::erase(Listeners,listener);}
    bool IsExecuting() const override{return Real&&Real->IsExecuting();}
    void SetResourceProvider(GameSproutRuntime::IRuntimeResourceProvider* resources) override{if(Real)Real->SetResourceProvider(resources);}
    GameSproutRuntime::IRuntimeResourceProvider* GetResourceProvider() const override{return Real?Real->GetResourceProvider():nullptr;}
    GameSproutRuntime::IGameObject* InstantiateModel(GameSproutRuntime::IScene* scene,const GameSproutRuntime::ResourceReference& reference,
        GameSproutRuntime::IGameObject* parent,std::string& error) override{return Real?Real->InstantiateModel(scene,reference,parent,error):nullptr;}
    void Update(float seconds) override{if(Real)Real->Update(seconds);}
    void Render() override{if(Real)Real->Render();}
};

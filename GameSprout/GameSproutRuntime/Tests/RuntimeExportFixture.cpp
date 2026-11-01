#include "GameSproutRuntimeExport.h"

// 仅供加载器测试，明确不实现场景/对象业务，不进入发布动态库。
class ExportFixture final : public GameSproutRuntime::IGameSproutRuntime {
public:
    GameSproutRuntime::IRuntimeViewport* CreateViewport(const GameSproutRuntime::NativeWindow&,GameSproutRuntime::IRuntimeResourceProvider*,std::string&) override {return nullptr;}
    GameSproutRuntime::IRuntimeViewport* CreateOffscreenRenderer(std::uint32_t,std::uint32_t,GameSproutRuntime::IRuntimeResourceProvider*,std::string&) override {return nullptr;}
    GameSproutRuntime::IScene* CreateScene(const std::string&) override { return nullptr; }
    bool DestroyScene(GameSproutRuntime::IScene*) override { return false; }
    std::size_t GetSceneCount() const override { return 0; }
    GameSproutRuntime::IScene* GetScene(std::size_t) const override { return nullptr; }
    GameSproutRuntime::IGameObject* QueryGameObject(std::uint64_t) const override { return nullptr; }
    bool RegisterListener(GameSproutRuntime::IRuntimeListener*) override { return false; }
    void UnregisterListener(GameSproutRuntime::IRuntimeListener*) override {}
    bool IsExecuting() const override { return false; }
    void Update(float) override {}
    void Render() override {}
};

extern "C" GAMESPROUT_RUNTIME_API GameSproutRuntime::IGameSproutRuntime*
GAMESPROUT_RUNTIME_CALL CreateGameSproutRuntime() noexcept {
#if defined(GAMESPROUT_TEST_NULL_FACTORY)
    return nullptr;
#else
    try { return new ExportFixture(); }
    catch (...) { return nullptr; }
#endif
}

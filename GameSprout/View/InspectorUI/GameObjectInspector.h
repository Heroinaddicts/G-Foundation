#pragma once
#include <QWidget>
#include <vector>
#include "GameSproutRuntimeApi.h"
#include "ComponentEditContext.h"
namespace Ui { class GameObjectInspectorForm; }
class ComponentInfoSection;

class GameObjectInspector final : public QWidget {
public:
    explicit GameObjectInspector(QWidget* parent);
    ~GameObjectInspector() override;
    GameObjectInspector(const GameObjectInspector&) = delete;
    GameObjectInspector& operator=(const GameObjectInspector&) = delete;
    void Load(GameSproutRuntime::IGameObject& object);
    void Clear();
    void Bind(const ComponentEditContext& context) { _Context = context; }
private:
    void CommitName();
    void CommitActive(bool active);
    Ui::GameObjectInspectorForm* _Ui = nullptr;
    std::vector<ComponentInfoSection*> _Sections; // Qt 子控件，不保存任何 Runtime 节点指针。
    ComponentEditContext _Context;
    bool _Loading = false;
    std::uint64_t _LoadedID = 0;
};

#pragma once
#include <QWidget>
#include <QPointer>
#include <functional>
#include <vector>
#include "GameSproutRuntimeApi.h"
#include "ComponentEditContext.h"
namespace Ui { class GameObjectInspectorForm; }
class ComponentInfoSection;
class AddComponentPopup;
class QMimeData;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;

class GameObjectInspector final : public QWidget {
public:
    explicit GameObjectInspector(QWidget* parent);
    ~GameObjectInspector() override;
    GameObjectInspector(const GameObjectInspector&) = delete;
    GameObjectInspector& operator=(const GameObjectInspector&) = delete;
    void Load(GameSproutRuntime::IGameObject& object);
    void Clear();
    void SetLocked(bool locked);
    QWidget* HeaderExtensionsHost() const;
    void SetProjectSettings(const GameSproutRuntime::RuntimeProjectSettings&);
    std::function<void()> OpenProjectSettings;
    void SetReadOnly(bool value);
    void SetEditability(bool properties,bool structure);
    void SetDebugMode(bool debug);
    void OnAnimationEvent(const GameSproutRuntime::AnimationNotification&);
    std::function<void(bool)> LockChanged;
    void Bind(const ComponentEditContext& context) { _Context = context; }
private:
    void OpenAddComponent();
    bool AttachDroppedScripts(const QMimeData*, bool commit);
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dropEvent(QDropEvent*) override;
    void CommitName();
    void RefreshClassification();
    void CommitTag(int);
    void CommitLayer(int);
    void CommitActive(bool active);
    Ui::GameObjectInspectorForm* _Ui = nullptr;
    std::vector<ComponentInfoSection*> _Sections; // Qt 子控件，不保存任何 Runtime 节点指针。
    ComponentEditContext _Context;
    GameSproutRuntime::RuntimeProjectSettings _ProjectSettings;
    bool _Loading = false;
    bool _ReadOnly = false;
    bool _StructureReadOnly=false;
    bool _DebugMode = false;
    bool _SceneRoot = false;
    std::uint64_t _LoadedID = 0;
    QPointer<AddComponentPopup> _AddPopup;
};

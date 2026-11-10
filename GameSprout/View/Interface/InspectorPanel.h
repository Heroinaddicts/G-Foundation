#pragma once
#include "GameSproutEditorApi.h"
#include "ISelectionManager.h"
#include <JsonCpp/json.hpp>
#include <QWidget>
#include <functional>
#include <set>
#include <typeinfo>

class IAssetsManager;
class ILibraryManager;
class ISceneManager;
class IAssetMeta;
class IAsset;
class ComponentEditor;
struct ComponentEditContext;

struct InspectorServices {
    GameSproutEditor::GameSproutViewApi* Editor = nullptr;
    IAssetsManager* Assets = nullptr;
    ILibraryManager* Library = nullptr;
    ISelectionManager* Selection = nullptr;
    ISceneManager* Scenes = nullptr;
};

// 只传身份；面板每次刷新重新查询，不跨刷新持有资源或 Runtime 节点。
struct InspectorTarget {
    std::vector<ProjectAssetSelection> Assets;
    SceneObjectSelection Scene;
};

struct InspectorObjectChanges {
    bool Header = false;
    std::set<std::uint64_t> ComponentIDs;
    std::set<std::string> ComponentTypes;
    bool Empty() const {return !Header&&ComponentIDs.empty()&&ComponentTypes.empty();}
};

struct InspectorPresentation {
    QString Name;
    QString Type;
    QString State;
    QString Path;
    QString Guid;
    QString LocalID;
    Json::Value Diagnostics{Json::objectValue};
    bool SceneObject = false;
    bool Editable = false;
    QString ApplyText = QStringLiteral("应用并重新导入");
};

// 具体功能 View 拥有实现；Qt 父控件拥有页面。宿主只调用此协议。
class InspectorPanel : public QWidget {
public:
    explicit InspectorPanel(const InspectorServices& services, QWidget* parent)
        : QWidget(parent), _Services(services) {}
    ~InspectorPanel() override = default;
    virtual bool Load(const InspectorTarget& target, bool changed, std::string& error) = 0;
    virtual InspectorPresentation Describe() const = 0;
    virtual bool HasPendingChanges() const { return false; }
    virtual bool Validate(std::string& error) const { error.clear(); return true; }
    virtual bool Apply(std::string&) { return false; }
    virtual bool Revert(std::string& error) { error.clear(); return true; }
    virtual void Deactivate() {}
    virtual void SetDebugMode(bool) {}
    virtual void SetLocked(bool) {}
    // Optional .ui-defined slot for additive object-header controls.
    virtual QWidget* SceneObjectHeaderHost() const {return nullptr;}
    virtual void OnAnimationEvent(const GameSproutRuntime::AnimationNotification&) {}
    virtual bool RefreshObjectProperties(const InspectorObjectChanges&) {return false;}
    virtual bool TracksAsset(const std::string&) const {return false;}
    std::function<void()> Changed;
    std::function<void(const std::string&)> ReportError;
    std::function<void(const ProjectAssetSelection&)> RequestSelection;
    std::function<void(bool)> RequestLock;
    std::function<bool()> ResolvePendingChanges;
protected:
    InspectorServices _Services;
};

// Additive scene-object UI. Load receives the Inspector's resolved target,
// including its lock; false means the section is inapplicable and should hide.
// Qt parent owns the section. Deactivate clears borrowed target identities.
class InspectorSection : public QWidget {
public:
    explicit InspectorSection(const InspectorServices& services,QWidget* parent)
        : QWidget(parent),_Services(services) {}
    ~InspectorSection() override = default;
    virtual bool Load(const InspectorTarget&,bool changed,std::string& error)=0;
    virtual void Deactivate() {}
    std::function<void(const std::string&)> ReportError;
    std::function<void()> RequestRefresh;
    // Selecting an asset from a section preserves the Inspector lock.
    std::function<void(const ProjectAssetSelection&)> RequestSelection;
protected:
    InspectorServices _Services;
};

enum class InspectorSectionPlacement {BeforeObject,ObjectHeader};

// 注册表只在 InspectorUI 实例中保存。扩展注销后，其页面由宿主销毁。
class IInspectorExtension {
public:
    virtual ~IInspectorExtension() = default;
    virtual int Priority() const { return 0; }
    virtual bool CanInspect(const InspectorServices&, const InspectorTarget&) const { return false; }
    virtual InspectorPanel* CreatePanel(const InspectorServices&, QWidget*) { return nullptr; }
    virtual ComponentEditor* CreateComponentEditor(const std::type_info&, QWidget*) { return nullptr; }
    // Does not compete with CanInspect/CreatePanel; component pages stay active.
    virtual InspectorSection* CreateSceneObjectSection(const InspectorServices&,QWidget*) { return nullptr; }
    virtual InspectorSectionPlacement SceneObjectSectionPlacement() const {return InspectorSectionPlacement::BeforeObject;}
};

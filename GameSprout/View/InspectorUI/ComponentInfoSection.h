#pragma once
#include <QWidget>
#include "GameSproutRuntimeApi.h"
#include "ComponentEditContext.h"
namespace Ui { class ComponentInfoSectionForm; }
class QTreeWidgetItem;
class ComponentEditor;

class ComponentInfoSection final : public QWidget {
public:
    explicit ComponentInfoSection(QWidget* parent);
    ~ComponentInfoSection() override;
    ComponentInfoSection(const ComponentInfoSection&) = delete;
    ComponentInfoSection& operator=(const ComponentInfoSection&) = delete;
    void Load(GameSproutRuntime::IComponent& component);
    void Bind(const ComponentEditContext& context) { _Context = context; }
    bool IsExpanded() const;
    void SetExpanded(bool expanded);
private:
    void Add(const QString& key, const QString& label, const QString& value, QTreeWidgetItem* parent = nullptr);
    void Matrix(const QString& key, const QString& label, const GameSproutRuntime::Mat4& matrix);
    void Resource(const QString& key, const QString& label, const GameSproutRuntime::ResourceReference& resource);
    void UpdateHeight();
    Ui::ComponentInfoSectionForm* _Ui = nullptr;
    ComponentEditContext _Context;
    ComponentEditor* _Editor = nullptr;
    std::string _EditorType;
};

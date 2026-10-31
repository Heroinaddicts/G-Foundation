#pragma once
#include "ComponentEditContext.h"
#include "ComponentEditorValues.h"
#include <QWidget>
#include <initializer_list>
class QLineEdit;

class ComponentEditor : public QWidget {
public:
    explicit ComponentEditor(QWidget* parent) : QWidget(parent) {}
    virtual ~ComponentEditor() = default;
    void Bind(const ComponentEditContext& context);
    void Load(GameSproutRuntime::IComponent& component);
    void Reload();
    virtual void OnAnimationEvent(const GameSproutRuntime::AnimationNotification&) {}
protected:
    virtual void ReadValues(GameSproutRuntime::IComponent& component) = 0;
    virtual void ContextChanged() {}
    bool ReadNumber(QLineEdit* edit, float& output) const;
    static void ShowNumber(QLineEdit* edit, float value);
    bool ReadNumbers(std::initializer_list<QLineEdit*> edits, std::vector<float>& output) const;
    static void ShowNumbers(std::initializer_list<QLineEdit*> edits, std::initializer_list<float> values);
    void Fail(const std::string& error);
    template<typename Interface, typename Value>
    void Commit(Value value, const std::string& label) {
        if (_Loading) return;
        auto* object = _Context.Resolve();
        auto* component = object ? object->GetComponent<Interface>() : nullptr;
        if (!component) { Fail("组件已经移除或节点已失效。"); return; }
        const Value previous = InspectorValues::Read(*component);
        if (InspectorValues::Equal(previous, value)) { if (_Context.Report) _Context.Report(""); Reload(); return; }
        std::string error;
        if (!InspectorValues::Validate(value, error)) { Fail(error); return; }
        if (InspectorValues::Equal(previous, value)) { if (_Context.Report) _Context.Report(""); Reload(); return; }
        // 历史命令只捕获值。每次执行重新从传入对象查找组件，不借用面板或运行对象。
        const auto operation = [](Value desired) {
            return [desired](GameSproutRuntime::IGameObject& target, std::string& failure) {
                auto* current = target.GetComponent<Interface>();
                if (!current) { failure = "组件已经移除。"; return false; }
                const Value old = InspectorValues::Read(*current);
                if (InspectorValues::Write(*current, desired, failure)) return true;
                std::string ignored;
                InspectorValues::Write(*current, old, ignored);
                return false;
            };
        };
        _Context.Execute(label, operation(value), operation(previous));
        Reload();
    }
    ComponentEditContext _Context;
    bool _Loading = false;
private:
    const std::type_info* _Type = nullptr;
    bool _Force = false;
};
ComponentEditor* CreateComponentEditor(const std::type_info& type, QWidget* parent);

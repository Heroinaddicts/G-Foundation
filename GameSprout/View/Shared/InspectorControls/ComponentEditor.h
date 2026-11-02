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
    void SetEditability(bool properties,bool structure);
    virtual void OnAnimationEvent(const GameSproutRuntime::AnimationNotification&) {}
protected:
    virtual void ReadValues(GameSproutRuntime::IComponent& component) = 0;
    virtual void ContextChanged() {}
    virtual void EditabilityChanged() {}
    bool ReadNumber(QLineEdit* edit, float& output) const;
    static void ShowNumber(QLineEdit* edit, float value);
    bool ReadNumbers(std::initializer_list<QLineEdit*> edits, std::vector<float>& output) const;
    static void ShowNumbers(std::initializer_list<QLineEdit*> edits, std::initializer_list<float> values);
    void Fail(const std::string& error);
    void FinishInput();
    template<typename Interface, typename Value>
    void Commit(Value value, const std::string& label) {
        if (_Loading||!_PropertiesEditable) return;
        auto* object = _Context.Resolve();
        auto* component = _Context.ComponentID ? dynamic_cast<Interface*>(_Context.ResolveComponent()) : object ? object->GetComponent<Interface>() : nullptr;
        if (!component) { Fail("组件已经移除或节点已失效。"); return; }
        const Value previous = InspectorValues::Read(*component);
        if (InspectorValues::Equal(previous, value)) { if (_Context.Report) _Context.Report(""); FinishInput();Reload(); return; }
        std::string error;
        if (!InspectorValues::Validate(value, error)) { Fail(error); return; }
        if (InspectorValues::Equal(previous, value)) { if (_Context.Report) _Context.Report(""); FinishInput();Reload(); return; }
        // 历史命令只捕获值。每次执行重新从传入对象查找组件，不借用面板或运行对象。
        auto* scenes=_Context.Scenes;
        const auto instanceID=_Context.ComponentID;
        const auto fileID=scenes&&_Context.ComponentID?scenes->GetComponentFileID(_Context.ObjectID,_Context.ComponentID):0;
        const auto operation = [scenes,fileID,instanceID](Value desired) {
            return [desired,scenes,fileID,instanceID](GameSproutRuntime::IGameObject& target, std::string& failure) {
                Interface* current=nullptr;
                if(fileID&&scenes)current=dynamic_cast<Interface*>(scenes->QueryComponentByFileID(target.GetInstanceID(),fileID));
                else if(instanceID)for(std::size_t index=0;index<target.GetComponentCount();++index) {
                    auto* candidate=target.GetComponentAt(index);
                    if(candidate&&candidate->GetComponentInstanceID()==instanceID){current=dynamic_cast<Interface*>(candidate);break;}
                }
                else current=target.GetComponent<Interface>();
                if (!current) { failure = "组件已经移除。"; return false; }
                const Value old = InspectorValues::Read(*current);
                if (InspectorValues::Write(*current, desired, failure)) return true;
                std::string ignored;
                InspectorValues::Write(*current, old, ignored);
                return false;
            };
        };
        FinishInput();
        _Context.Execute(label, operation(value), operation(previous));
        Reload();
    }
    ComponentEditContext _Context;
    bool _Loading = false;
    bool _PropertiesEditable=true,_StructureEditable=true;
private:
    const std::type_info* _Type = nullptr;
    bool _Force = false;
};

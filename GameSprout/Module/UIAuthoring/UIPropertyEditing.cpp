#include "UIAuthoring.h"
#include "IObjectEditing.h"
#include <cmath>

namespace {
using GameSproutRuntime::IGameObject;
using GameSproutRuntime::ITextComponent;
using GameSproutRuntime::IButtonComponent;

template<typename Interface>
Interface* FindInstance(IGameObject& object,std::uint64_t id) {
    if(!id)return object.GetComponent<Interface>();
    for(std::size_t index=0;index<object.GetComponentCount();++index) {
        auto* component=object.GetComponentAt(index);
        if(component&&component->GetComponentInstanceID()==id)return dynamic_cast<Interface*>(component);
    }
    return nullptr;
}

template<typename Interface,typename Value,typename Read,typename Write,typename Equal>
bool EditUI(GameSproutRuntime::IGameSproutRuntime& runtime,ISceneManager& scenes,IObjectEditing& editing,
    const ObjectEditTarget& target,const std::string& label,const Value& value,
    Read read,Write write,Equal equal,std::string& error) {
    if(!editing.CanEdit(target,error))return false;
    auto* object=runtime.QueryGameObject(target.ObjectID);
    auto* component=object?FindInstance<Interface>(*object,target.ComponentID):nullptr;
    if(!component){error="UI 组件已移除或目标类型不匹配。";return false;}
    const Value previous=read(*component);
    if(equal(previous,value))return true;
    const auto instanceID=component->GetComponentInstanceID();
    const auto fileID=scenes.GetComponentFileID(target.ObjectID,instanceID);
    auto operation=[scenes=&scenes,fileID,instanceID,write](Value desired) {
        return [scenes,fileID,instanceID,write,desired=std::move(desired)](IGameObject& current,std::string& failure) {
            auto* component=fileID?
                dynamic_cast<Interface*>(scenes->QueryComponentByFileID(current.GetInstanceID(),fileID)):
                FindInstance<Interface>(current,instanceID);
            if(component&&write(*component,desired))return true;
            failure="UI 参数无效或组件已移除。";
            return false;
        };
    };
    return editing.EditObject({target.SceneID,target.ObjectID,instanceID},label,
        operation(value),operation(previous),error);
}

bool SameColor(GameSproutRuntime::Color a,GameSproutRuntime::Color b) {
    return a.R==b.R&&a.G==b.G&&a.B==b.B&&a.A==b.A;
}
}

bool UIAuthoring::SetText(const ObjectEditTarget& target,const UITextAuthoringValues& value,std::string& error) {
    if(!_Runtime||!_Scenes||!_Editing){error="UI 编辑服务未就绪。";return false;}
    const auto color=value.Color;
    if(!std::isfinite(color.R)||!std::isfinite(color.G)||!std::isfinite(color.B)||!std::isfinite(color.A)||
        color.R<0||color.G<0||color.B<0||color.A<0||color.A>1) {
        error="颜色 RGB 必须为有限非负数，Alpha 必须在 0 至 1 之间。";
        return false;
    }
    if(!GameSproutRuntime::IsValidUIFontSize(value.FontSize)) {
        error="字号必须大于 0 且不超过 4096。";
        return false;
    }
    if(!GameSproutRuntime::IsValidUIText(value.Text)) {
        error="文字必须是有效 UTF-8，且不能超过 65536 字节。";
        return false;
    }
    return EditUI<ITextComponent>(*_Runtime,*_Scenes,*_Editing,target,"修改 UI 文字",value,
        [](ITextComponent& text){return UITextAuthoringValues{text.GetText(),text.GetFont(),text.GetColor(),text.GetFontSize()};},
        [](ITextComponent& text,const UITextAuthoringValues& desired) {
            if(!text.SetText(desired.Text)||!text.SetFontSize(desired.FontSize))return false;
            text.SetFont(desired.Font);
            text.SetColor(desired.Color);
            return text.GetFont()==desired.Font&&SameColor(text.GetColor(),desired.Color);
        },
        [](const UITextAuthoringValues& a,const UITextAuthoringValues& b) {
            return a.Text==b.Text&&a.Font==b.Font&&a.FontSize==b.FontSize&&SameColor(a.Color,b.Color);
        },error);
}

bool UIAuthoring::SetButtonInteractable(const ObjectEditTarget& target,bool value,std::string& error) {
    if(!_Runtime||!_Scenes||!_Editing){error="UI 编辑服务未就绪。";return false;}
    return EditUI<IButtonComponent>(*_Runtime,*_Scenes,*_Editing,target,"修改按钮交互",value,
        [](IButtonComponent& button){return button.GetInteractable();},
        [](IButtonComponent& button,bool desired){button.SetInteractable(desired);return true;},
        [](bool a,bool b){return a==b;},error);
}

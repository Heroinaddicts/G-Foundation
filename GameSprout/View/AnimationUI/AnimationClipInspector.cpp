#include "AnimationClipInspector.h"
#include "AnimationClipOptionsEditor.h"
#include "ResourceField.h"
#include "ui_AnimationClipInspector.h"
AnimationClipInspector::AnimationClipInspector(QWidget* parent):ImportSettingsInspector(parent),_Ui(new Ui::AnimationClipInspectorForm()) {
    _Ui->setupUi(this);_Ui->sourceAvatarField->Changed=[this](const auto&){if(Changed)Changed();};_Ui->clipOptionsEditor->Changed=[this]{if(Changed)Changed();};
}
AnimationClipInspector::~AnimationClipInspector(){_Ui->sourceAvatarField->Changed={};_Ui->clipOptionsEditor->Changed={};delete _Ui;}
void AnimationClipInspector::BindSourceAvatar(IAssetsManager* assets,ILibraryManager* library,ISelectionManager* selection,const std::string& key){_Ui->sourceAvatarField->Bind(assets,library,selection,eAssetType::Avatar);_Ui->sourceAvatarField->SetContextKey(key);}
void AnimationClipInspector::SetNodePaths(const std::vector<std::string>& paths){_Ui->clipOptionsEditor->SetNodePaths(paths);}
void AnimationClipInspector::Load(const Json::Value& settings){ImportSettingsInspector::Load(settings);_Ui->clipOptionsEditor->Load(settings["ClipOptions"]);
    GameSproutRuntime::ResourceReference reference;const auto& value=settings["SourceAvatar"];if(value.isObject()&&value["Guid"].isString()&&value["LocalID"].isUInt64())reference={value["Guid"].asString(),value["LocalID"].asUInt64()};_Ui->sourceAvatarField->SetValue(reference);}
bool AnimationClipInspector::Read(Json::Value& settings,std::string& error) const {
    if(!ImportSettingsInspector::Read(settings,error))return false;
    Json::Value options;if(!_Ui->clipOptionsEditor->Read(options,error))return false;
    if(settings.isMember("ClipOptions")||!options.empty())settings["ClipOptions"]=std::move(options);
    const auto reference=_Ui->sourceAvatarField->GetValue();if(reference.Guid.empty())settings.removeMember("SourceAvatar");else{Json::Value value;value["Guid"]=reference.Guid;value["LocalID"]=Json::UInt64(reference.LocalID);settings["SourceAvatar"]=std::move(value);}
    return true;
}

#include "MaterialUI.h"
#include "MaterialInspectorPanel.h"
#include "IInspectorUI.h"
#include "IAssetsManager.h"
#include "ILibraryManager.h"

MaterialUI::~MaterialUI(){Release(nullptr);}
void MaterialUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* editor) {
    _Inspector=editor?editor->GetGameSproutView<IInspectorUI>("InspectorUI"):nullptr;
}
bool MaterialUI::Initialize(GameSproutEditor::GameSproutViewApi*) {return _Inspector!=nullptr;}
bool MaterialUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    if(!_Registered&&_Inspector)_Registered=_Inspector->RegisterExtension(this);
    return _Registered;
}
void MaterialUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if(_Registered&&_Inspector)_Inspector->UnregisterExtension(this);
    _Registered=false;_Inspector=nullptr;
}
void MaterialUI::Show(){if(_Inspector)_Inspector->Show();}
bool MaterialUI::CanInspect(const InspectorServices& services,const InspectorTarget& target) const {
    if(target.Scene.ObjectID||target.Assets.size()!=1||!services.Assets||!services.Library)return false;
    const auto& selected=target.Assets.front();
    IAssetMeta* meta=services.Assets->QueryAsset(Guid(selected.GuidString));
    if(!meta)return false;
    if(!selected.IsSubAsset)return meta->_ImporterType==eAssetImporterType::Material;
    IAsset* resource=services.Library->QueryAsset(meta->_Guid,selected.LocalID);
    return resource&&resource->GetAssetType()==eAssetType::Material;
}
InspectorPanel* MaterialUI::CreatePanel(const InspectorServices& services,QWidget* parent) {
    return new MaterialInspectorPanel(services,parent);
}

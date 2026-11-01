#include "AssetImportUI.h"
#include "AssetImportPanel.h"
#include "IInspectorUI.h"
void AssetImportUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* editor) {
    _Inspector = editor ? editor->GetGameSproutView<IInspectorUI>("InspectorUI") : nullptr;
}
bool AssetImportUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    _Registered = _Inspector && _Inspector->RegisterExtension(this); return _Registered;
}
void AssetImportUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if (_Registered && _Inspector) _Inspector->UnregisterExtension(this);
    _Registered = false; _Inspector = nullptr;
}
void AssetImportUI::Show() { if (_Inspector) _Inspector->Show(); }
bool AssetImportUI::CanInspect(const InspectorServices&, const InspectorTarget& target) const { return !target.Scene.ObjectID && target.Assets.size() == 1; }
InspectorPanel* AssetImportUI::CreatePanel(const InspectorServices& services, QWidget* parent) { return new AssetImportPanel(services, parent); }

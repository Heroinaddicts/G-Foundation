#include "StaticUI.h"
#include "StaticFlagsSection.h"
#include "IInspectorUI.h"
#include "IStaticAuthoring.h"
#include <QApplication>
StaticUI::~StaticUI(){Release(nullptr);}
void StaticUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* editor) {
    _Inspector=editor?editor->GetGameSproutView<IInspectorUI>("InspectorUI"):nullptr;
}
bool StaticUI::Initialize(GameSproutEditor::GameSproutViewApi*) {return qobject_cast<QApplication*>(QCoreApplication::instance())!=nullptr;}
bool StaticUI::Launch(GameSproutEditor::GameSproutViewApi* editor) {
    if(!_Inspector||!editor||!editor->GetGameSproutModule<IStaticAuthoring>("StaticAuthoring"))return false;
    _Registered=_Inspector->RegisterExtension(this);return _Registered;
}
void StaticUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if(_Registered&&_Inspector)_Inspector->UnregisterExtension(this);
    _Registered=false;_Inspector=nullptr;
}
void StaticUI::Show(){if(_Inspector)_Inspector->Show();}
InspectorSection* StaticUI::CreateSceneObjectSection(const InspectorServices& services,QWidget* parent) {
    return new StaticFlagsSection(services,parent);
}

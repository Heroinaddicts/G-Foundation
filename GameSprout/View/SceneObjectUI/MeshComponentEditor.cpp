#include "MeshComponentEditor.h"
#include "ResourceField.h"
#include "ui_MeshComponentEditor.h"
MeshComponentEditor::MeshComponentEditor(QWidget* parent) : ComponentEditor(parent), _Ui(new Ui::MeshComponentEditorForm()) {
    _Ui->setupUi(this);
    _Mesh = new ResourceField(_Ui->meshResourceHost);
    _Ui->meshResourceHostLayout->addWidget(_Mesh);
    _Mesh->Changed = [this](const GameSproutRuntime::ResourceReference& resource) {
        if (!_Loading) Commit<GameSproutRuntime::IMeshComponent>(resource,"绑定 Mesh");
    };
}
MeshComponentEditor::~MeshComponentEditor() { _Mesh->Changed = {}; delete _Ui; }
void MeshComponentEditor::ContextChanged() {
    _Mesh->Bind(_Context.Assets,_Context.Library,_Context.Selection,eAssetType::Mesh);
    _Mesh->SetContextKey(_Context.Key("Mesh","Mesh"));
}
void MeshComponentEditor::ReadValues(GameSproutRuntime::IComponent& component) {
    _Mesh->SetValue(static_cast<GameSproutRuntime::IMeshComponent&>(component).GetMesh());
}

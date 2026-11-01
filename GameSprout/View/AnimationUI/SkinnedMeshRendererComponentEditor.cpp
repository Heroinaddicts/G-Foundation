#include "SkinnedMeshRendererComponentEditor.h"
#include "ui_SkinnedMeshRendererComponentEditor.h"

SkinnedMeshRendererComponentEditor::SkinnedMeshRendererComponentEditor(QWidget* parent)
    :ComponentEditor(parent),_Ui(new Ui::SkinnedMeshRendererComponentEditorForm()) {
    _Ui->setupUi(this);
    QObject::connect(_Ui->animatorCombo,qOverload<int>(&QComboBox::activated),this,[this]{CommitAnimator();});
}
SkinnedMeshRendererComponentEditor::~SkinnedMeshRendererComponentEditor(){delete _Ui;}
void SkinnedMeshRendererComponentEditor::ReadValues(GameSproutRuntime::IComponent& component) {
    auto& skin=static_cast<GameSproutRuntime::ISkinnedMeshRendererComponent&>(component);
    _Ui->animatorCombo->clear();_Ui->animatorCombo->addItem(tr("未绑定"),QVariant::fromValue<qulonglong>(0));
    auto* object=_Context.Resolve();if(!object)return;
    std::vector<GameSproutRuntime::IGameObject*> nodes{object->GetScene()};
    int selected=0;
    while(!nodes.empty()) {
        auto* node=nodes.back();nodes.pop_back();
        if(node->GetComponent<GameSproutRuntime::IAnimatorComponent>()) {
            QStringList path;for(auto* current=node;current&&current!=object->GetScene();current=current->Parent())path.prepend(QString::fromStdString(current->GetName()));
            _Ui->animatorCombo->addItem(path.join(" / "),QVariant::fromValue<qulonglong>(node->GetInstanceID()));
            if(node->GetInstanceID()==skin.GetAnimatorObjectID())selected=_Ui->animatorCombo->count()-1;
        }
        for(std::size_t i=node->GetChildCount();i>0;--i)nodes.push_back(node->GetChild(i-1));
    }
    _Ui->animatorCombo->setCurrentIndex(selected);
}
void SkinnedMeshRendererComponentEditor::CommitAnimator() {
    if(_Loading||!_Context.Scenes)return;
    auto* object=_Context.Resolve();auto* skin=object?object->GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>():nullptr;if(!skin)return;
    const auto nextID=_Ui->animatorCombo->currentData().toULongLong();
    auto* scenes=_Context.Scenes;
    const auto operation=[scenes](std::uint64_t fileID){return [scenes,fileID](GameSproutRuntime::IGameObject& target,std::string& error){
        auto* component=target.GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>();
        auto* animator=fileID?scenes->QueryObjectByFileID(fileID):nullptr;
        if(!component||(fileID&&!animator)||!component->SetAnimatorObjectID(animator?animator->GetInstanceID():0)) {error="无法绑定 Animator。";return false;}
        return true;
    };};
    _Context.Execute("绑定蒙皮 Animator",operation(nextID?scenes->GetObjectFileID(nextID):0),
        operation(skin->GetAnimatorObjectID()?scenes->GetObjectFileID(skin->GetAnimatorObjectID()):0));Reload();
}

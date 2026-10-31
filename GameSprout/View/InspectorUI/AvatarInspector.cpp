#include "AvatarInspector.h"
#include "IRuntimeAssets.h"
#include "HumanoidImportSettings.h"
#include "ui_AvatarInspector.h"
AvatarInspector::AvatarInspector(QWidget* parent):QWidget(parent),_Ui(new Ui::AvatarInspectorForm){_Ui->setupUi(this);connect(_Ui->configureButton,&QPushButton::clicked,this,[this]{if(Edit)Edit();});}
AvatarInspector::~AvatarInspector(){delete _Ui;}
void AvatarInspector::Load(IRuntimeAssets* resources,const GameSproutRuntime::ResourceReference& reference){
    ModelRigImportSettings settings;GameSproutRuntime::SkeletonData skeleton;std::string snapshot,error;
    const bool loaded=resources&&resources->LoadModelRigSource(reference,settings,skeleton,snapshot,error);
    _Ui->configureButton->setEnabled(resources!=nullptr);
    _Ui->summaryLabel->setText(loaded?tr("%1 Avatar\n%2 个角色骨映射 · %3 个模型节点\n参考姿势与映射保存在模型导入设置中。")
        .arg(settings.AnimationType==eModelAnimationType::Humanoid?"Humanoid":"Generic").arg(settings.HumanBones.size()).arg(skeleton.Nodes.size()):QString::fromStdString(error));
}

#include "MeshRendererComponentEditor.h"
#include "MaterialSlotEditor.h"
#include "ui_MeshRendererComponentEditor.h"
#include <algorithm>
MeshRendererComponentEditor::MeshRendererComponentEditor(QWidget* parent) : ComponentEditor(parent), _Ui(new Ui::MeshRendererComponentEditorForm()) {
    _Ui->setupUi(this);
    QObject::connect(_Ui->rendererVisibleCheck,&QCheckBox::clicked,this,[this](bool visible){
        Modify([visible](InspectorValues::MeshRenderer& v){v.Visible=visible;},"修改 MeshRenderer 可见状态");
    });
    QObject::connect(_Ui->addMaterialButton,&QPushButton::clicked,this,[this]{
        Modify([](InspectorValues::MeshRenderer& v){v.Materials.push_back({});},"添加材质槽");
    });
}
MeshRendererComponentEditor::~MeshRendererComponentEditor() { for(auto* slot:_Slots) slot->Deactivate(); delete _Ui; }
void MeshRendererComponentEditor::ContextChanged() {
    for(auto* slot:_Slots) {slot->Deactivate();_Ui->materialSlotsLayout->removeWidget(slot);slot->deleteLater();}
    _Slots.clear();
}
void MeshRendererComponentEditor::Modify(const std::function<void(InspectorValues::MeshRenderer&)>& change,const std::string& label) {
    if (_Loading) return;
    auto* object=_Context.Resolve();
    auto* component=object?object->GetComponent<GameSproutRuntime::IMeshRendererComponent>():nullptr;
    if(!component) {Fail("MeshRenderer 组件已失效。");return;}
    auto next=InspectorValues::Read(*component);change(next);
    Commit<GameSproutRuntime::IMeshRendererComponent>(next,label);
}
void MeshRendererComponentEditor::ReadValues(GameSproutRuntime::IComponent& component) {
    const auto v=InspectorValues::Read(static_cast<GameSproutRuntime::IMeshRendererComponent&>(component));
    _Ui->rendererVisibleCheck->setChecked(v.Visible);
    _Ui->addMaterialButton->setEnabled(v.Materials.size()<1024);
    while(_Slots.size()>v.Materials.size()) {
        auto* slot=_Slots.back();_Slots.pop_back();slot->Deactivate();_Ui->materialSlotsLayout->removeWidget(slot);slot->deleteLater();
    }
    for(std::size_t index=0;index<v.Materials.size();++index) {
        if(index==_Slots.size()) {
            auto* slot=new MaterialSlotEditor(_Ui->materialSlotsHost);
            _Ui->materialSlotsLayout->insertWidget(_Ui->materialSlotsLayout->count()-1,slot);
            slot->Changed=[this](std::size_t i,const GameSproutRuntime::ResourceReference& resource){
                Modify([i,resource](InspectorValues::MeshRenderer& state){if(i<state.Materials.size())state.Materials[i]=resource;},"绑定材质槽");
            };
            slot->Remove=[this](std::size_t i){Modify([i](InspectorValues::MeshRenderer& state){if(i<state.Materials.size())state.Materials.erase(state.Materials.begin()+i);},"删除材质槽");};
            slot->Move=[this](std::size_t i,int direction){Modify([i,direction](InspectorValues::MeshRenderer& state){
                if(i>=state.Materials.size() || (direction<0 && i==0) || (direction>0 && i+1>=state.Materials.size()))return;
                std::swap(state.Materials[i],state.Materials[direction<0?i-1:i+1]);
            },"调整材质槽顺序");};
            _Slots.push_back(slot);
        }
        _Slots[index]->Load(_Context,index,v.Materials.size(),v.Materials[index]);
    }
}

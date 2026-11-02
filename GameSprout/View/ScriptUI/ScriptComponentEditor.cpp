#include "ScriptComponentEditor.h"
#include "ScriptPropertyRow.h"
#include "IAssetsManager.h"
#include "ISelectionManager.h"
#include "ICodeEditor.h"
#include "ui_ScriptComponentEditor.h"
#include <QMouseEvent>
#include <exception>
namespace {
GameSproutRuntime::IScriptComponent* FindScript(GameSproutRuntime::IGameObject& object,std::uint64_t instance){
    for(std::size_t i=0;i<object.GetComponentCount();++i){
        auto* script=dynamic_cast<GameSproutRuntime::IScriptComponent*>(object.GetComponentAt(i));
        if(script&&script->GetScriptInstanceID()==instance)return script;
    }return nullptr;
}
}
ScriptComponentEditor::ScriptComponentEditor(QWidget* parent,ICodeEditor* codeEditor):ComponentEditor(parent),_Ui(new Ui::ScriptComponentEditorForm),_CodeEditor(codeEditor){
    _Ui->setupUi(this);
    _Ui->sourceEdit->installEventFilter(this);
    QObject::connect(_Ui->enabledCheck,&QCheckBox::clicked,this,[this](bool enabled){CommitEnabled(enabled);});
    QObject::connect(_Ui->removeButton,&QToolButton::clicked,this,[this]{Remove();});
    QObject::connect(_Ui->locateButton,&QToolButton::clicked,this,[this]{
        auto* script=ResolveScript();if(!script||!_Context.Selection)return;
        const auto source=script->GetScriptSource();if(!source.Guid.empty())_Context.Selection->SetAssetSelection({{source.Guid,0,false}});
    });
    QObject::connect(_Ui->compileButton,&QToolButton::clicked,this,[this]{std::string error;if(!_Scripts||!_Scripts->RequestCompile(error))ShowError(error.empty()?"脚本编译服务尚未就绪。":error);});
}
ScriptComponentEditor::~ScriptComponentEditor(){if(_Scripts)_Scripts->UnregisterScriptListener(this);delete _Ui;}
bool ScriptComponentEditor::eventFilter(QObject* watched,QEvent* event){
    if(watched==_Ui->sourceEdit&&event->type()==QEvent::MouseButtonDblClick&&static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton){
        OpenSource();return true;
    }
    return ComponentEditor::eventFilter(watched,event);
}
void ScriptComponentEditor::OpenSource(){
    auto* script=ResolveScript();
    if(!script||!_Context.Assets){ShowError("脚本源资源已失效。");return;}
    const auto source=script->GetScriptSource();
    const auto* meta=source.Guid.empty()?nullptr:_Context.Assets->QueryAsset(Guid(source.Guid));
    if(!meta||meta->_ImporterType!=eAssetImporterType::Script){ShowError("脚本源资源已失效。");return;}
    const std::string path=meta->GetPath();
    std::string error;
    try {
        if(!_CodeEditor)error="代码编辑服务尚未就绪。";
        else if(_CodeEditor->OpenSource(path,error))return;
    }catch(const std::exception& exception){error=exception.what();}
     catch(...){error="打开游戏代码工程时发生未知错误。";}
    ShowError(error.empty()?"无法打开游戏代码工程。":error);
}
void ScriptComponentEditor::ContextChanged(){
    if(_Scripts)_Scripts->UnregisterScriptListener(this);
    _FieldTarget={};
    _Scripts=_Context.Scripts;if(_Scripts)_Scripts->RegisterScriptListener(this);
}
GameSproutRuntime::IScriptComponent* ScriptComponentEditor::ResolveScript()const{
    auto* object=_Context.Resolve();return object?FindScript(*object,_InstanceID):nullptr;
}
void ScriptComponentEditor::ReadValues(GameSproutRuntime::IComponent& component){
    auto* script=dynamic_cast<GameSproutRuntime::IScriptComponent*>(&component);if(!script)return;
    _InstanceID=script->GetScriptInstanceID();
    _Ui->enabledCheck->setChecked(script->IsEnabled());
    const auto source=script->GetScriptSource();std::string path;
    if(_Context.Assets&&!source.Guid.empty())if(auto* meta=_Context.Assets->QueryAsset(Guid(source.Guid)))path=meta->GetPath();
    const QString hint=_Ui->sourceEdit->property("openProjectHint").toString();
    _Ui->sourceEdit->setText(QString::fromStdString(path));_Ui->sourceEdit->setToolTip(path.empty()?hint:QString::fromStdString(path)+"\n"+hint);_Ui->sourceEdit->setCursorPosition(0);
    _Ui->locateButton->setEnabled(!path.empty());
    GameSproutRuntime::ScriptTypeInfo definition;bool found=false;
    if(_Scripts)for(const auto& type:_Scripts->GetScriptTypes())if(type.Type.TypeID==script->GetScriptTypeID()){definition=type.Type;found=true;break;}
    _Ui->typeLabel->setText(found?QString::fromStdString(definition.Name):tr("Missing Script — 类型尚未编译或已从代码中移除"));
    _Ui->typeLabel->setVisible(!found);
    _Ui->typeLabel->setToolTip(QString::fromStdString(script->GetScriptTypeID()));
    RefreshFields();
    for(auto* row:_Rows)row->setEnabled(found&&script->IsScriptTypeAvailable());
    RefreshBuildState();
}
void ScriptComponentEditor::RefreshFields(){
    for(auto* row:_Rows)delete row;_Rows.clear();
    _FieldTarget={};
    _FieldError.clear();
    if(_Scripts)_Scripts->UnobserveFields(this);
    ScriptPropertySnapshot snapshot;std::string error;
    if(!_Scripts||!_Scripts->ReadFields(_Context.ObjectID,_InstanceID,snapshot,error)) {
        _FieldError=error.empty()?"脚本字段服务尚未就绪。":error;return;
    }
    _FieldTarget=snapshot.Target;
    if(!_Scripts->ObserveFields(_FieldTarget,this)) {
        _FieldTarget={};_FieldError="无法订阅当前脚本字段的变更。";return;
    }
    for(const auto& field:snapshot.Fields){
        auto* row=new ScriptPropertyRow(field,snapshot.Target,_Context,_Ui->fieldsHost);
        row->Changed=[this,id=field.Info.ID](const auto& value){CommitField(id,value);};
        row->Invalid=[this](const std::string& message){ShowError(message);};
        _Ui->fieldsLayout->addWidget(row);_Rows.push_back(row);
    }
}
void ScriptComponentEditor::Refresh(){
    auto* script=ResolveScript();if(!script){setEnabled(false);return;}
    _Loading=true;ReadValues(*script);_Loading=false;
}
void ScriptComponentEditor::RefreshBuildState(){
    const auto state=_Scripts?_Scripts->GetBuildState():ScriptBuildState{};
    _Ui->compileButton->setEnabled(_Scripts&&!state.Compiling&&!state.ReloadBlocked);
    const bool deferred=state.ReloadBlocked&&state.PendingChanges;
    _Ui->buildLabel->setText(state.Compiling?tr("正在编译…"):deferred?tr("代码已修改，停止运行后编译"):state.Error.empty()?QString():tr("编译失败，保留上次成功编译的类型"));
    auto* script=ResolveScript();std::string error=script?script->GetScriptError():std::string();
    const bool needsBuild=state.Compiling || state.PendingChanges || !state.Error.empty() || !script || !script->IsScriptTypeAvailable();
    _Ui->compileButton->setVisible(needsBuild);
    _Ui->buildLabel->setVisible(state.Compiling || deferred || !state.Error.empty());
    if(script&&!script->IsScriptTypeAvailable()&&error.empty())error="脚本类型不可用。请编译项目脚本，或移除此组件；已有字段值仍保留。";
    if(!state.Error.empty()){if(!error.empty())error+="\n";error+=state.Error;}
    if(!_FieldError.empty()){if(!error.empty())error+="\n";error+=_FieldError;}
    _Ui->errorEdit->setVisible(!error.empty());_Ui->errorEdit->setPlainText(QString::fromStdString(error));
}
void ScriptComponentEditor::QueueRefresh(){
    if(_RefreshQueued)return;
    _RefreshQueued=true;
    QMetaObject::invokeMethod(this,[this]{_RefreshQueued=false;Refresh();},Qt::QueuedConnection);
}
void ScriptComponentEditor::OnScriptTypesChanged(const std::vector<EditorScriptType>&){QueueRefresh();}
void ScriptComponentEditor::OnScriptFieldsChanged(const ScriptFieldTarget& target){
    if(target==_FieldTarget)QueueRefresh();
}
void ScriptComponentEditor::OnScriptBuildStateChanged(const ScriptBuildState&){RefreshBuildState();}
void ScriptComponentEditor::ShowError(const std::string& error){_Ui->errorEdit->setVisible(!error.empty());_Ui->errorEdit->setPlainText(QString::fromStdString(error));if(_Context.Report)_Context.Report(error);}
void ScriptComponentEditor::CommitField(const std::string& field,const GameSproutRuntime::ScriptValue& value){
    if(_Loading)return;
    std::string error;
    if(!_Scripts||!_Scripts->SetField(_FieldTarget,field,value,error)) {
        ShowError(error.empty()?"脚本字段编辑服务尚未就绪。":error);return;
    }
    // Event callbacks and editor transactions may run synchronously. Rebuild
    // after the originating control's callback has returned.
    QueueRefresh();
}
void ScriptComponentEditor::CommitEnabled(bool enabled){
    if(_Loading)return;auto* script=ResolveScript();if(!script)return;const bool previous=script->IsEnabled();if(previous==enabled)return;
    const auto operation=[instance=_InstanceID](bool enabled){return [instance,enabled](GameSproutRuntime::IGameObject& object,std::string& error){auto* script=FindScript(object,instance);if(!script){error="脚本组件已移除。";return false;}script->SetEnabled(enabled);return true;};};
    _Context.Execute("修改脚本启用状态",operation(enabled),operation(previous));
}
void ScriptComponentEditor::Remove(){
    if(!_Context.Scenes)return;std::string error;
    if(!_Context.Scenes->RemoveScriptComponent(_Context.ObjectID,_InstanceID,error))ShowError(error);
}

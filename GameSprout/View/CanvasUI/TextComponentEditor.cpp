#include "TextComponentEditor.h"
#include "IUIAuthoring.h"
#include "IObjectEditing.h"
#include "ui_TextComponentEditor.h"
#include <QTextDocument>

TextComponentEditor::TextComponentEditor(QWidget* parent,IUIAuthoring* authoring)
    :ComponentEditor(parent),_Ui(new Ui::TextComponentEditorForm()),_Authoring(authoring) {
    _Ui->setupUi(this);
    QObject::connect(_Ui->applyTextButton,&QPushButton::clicked,this,[this]{CommitField(_Ui->textEdit);});
    for(auto* field:{_Ui->fontSizeEdit,_Ui->redEdit,_Ui->greenEdit,_Ui->blueEdit,_Ui->alphaEdit})
        QObject::connect(field,&QLineEdit::editingFinished,this,[this,field]{CommitField(field);});
    _Ui->fontField->Changed=[this](const GameSproutRuntime::ResourceReference&){CommitField(_Ui->fontField);};
}

TextComponentEditor::~TextComponentEditor(){_Ui->fontField->Changed={};delete _Ui;}

void TextComponentEditor::ContextChanged() {
    _Ui->textEdit->document()->setModified(false);
    _Ui->fontField->Bind(_Context.Assets,_Context.Library,_Context.Selection,eAssetType::Font);
    _Ui->fontField->SetContextKey(_Context.Key("Text","Font"));
}

void TextComponentEditor::ReadValues(GameSproutRuntime::IComponent& component) {
    const auto& text=static_cast<const GameSproutRuntime::ITextComponent&>(component);
    if(!_Ui->textEdit->document()->isModified()) {
        _Ui->textEdit->setPlainText(QString::fromStdString(text.GetText()));
        _Ui->textEdit->document()->setModified(false);
    }
    _Ui->fontField->SetValue(text.GetFont());
    ShowNumber(_Ui->fontSizeEdit,text.GetFontSize());
    const auto color=text.GetColor();
    ShowNumbers({_Ui->redEdit,_Ui->greenEdit,_Ui->blueEdit,_Ui->alphaEdit},{color.R,color.G,color.B,color.A});
}

void TextComponentEditor::CommitField(QWidget* changed) {
    if(_Loading||!_PropertiesEditable||!_Authoring)return;
    auto* text=dynamic_cast<GameSproutRuntime::ITextComponent*>(_Context.ResolveComponent());
    if(!text){Fail("Text 组件已移除。");return;}
    UITextAuthoringValues value{text->GetText(),text->GetFont(),text->GetColor(),text->GetFontSize()};
    if(changed==_Ui->textEdit)value.Text=_Ui->textEdit->toPlainText().toStdString();
    if(changed==_Ui->fontField)value.Font=_Ui->fontField->GetValue();
    const std::pair<QLineEdit*,float*> numbers[]={
        {_Ui->fontSizeEdit,&value.FontSize},{_Ui->redEdit,&value.Color.R},{_Ui->greenEdit,&value.Color.G},
        {_Ui->blueEdit,&value.Color.B},{_Ui->alphaEdit,&value.Color.A}
    };
    for(const auto& [field,number]:numbers) {
        if(changed==field&&!ReadNumber(field,*number)){Fail("请输入有限数值。");return;}
    }
    FinishInput();
    std::string error;
    if(!_Authoring->SetText({_Context.SceneID,_Context.ObjectID,_Context.ComponentID},value,error)) {
        Fail(error);
        return;
    }
    if(changed==_Ui->textEdit)_Ui->textEdit->document()->setModified(false);
    if(_Context.Report)_Context.Report("");
    Reload();
}

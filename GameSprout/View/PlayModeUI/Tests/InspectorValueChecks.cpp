#include "ComponentEditor.h"
#include "TransformComponentEditor.h"
#include "ComponentInfoSection.h"
#include "GameSproutRuntimeExport.h"
#include "IInspectorUI.h"
#include <QApplication>
#include <QLineEdit>
#include <QToolButton>
#include <QTest>
#include <iostream>
#include <stdexcept>
static void Check(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
class NumberDisplay final:public ComponentEditor {
public:
    static void Show(QLineEdit* edit,float value){ShowNumber(edit,value);}
private:
    void ReadValues(GameSproutRuntime::IComponent&)override{}
};
class InspectorFactory final:public IInspectorUI {
public:
    bool HasPendingChanges()const override{return false;}
    bool ApplyChanges()override{return false;}
    void RevertChanges()override{}
    bool RegisterExtension(IInspectorExtension*)override{return true;}
    void UnregisterExtension(IInspectorExtension*)override{}
    ComponentEditor* CreateComponentEditor(const std::type_info& type,QWidget* parent)override{
        return type==typeid(GameSproutRuntime::ITransformComponent)?new TransformComponentEditor(parent):nullptr;
    }
private:
    bool Initialize(GameSproutEditor::GameSproutViewApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutViewApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutViewApi*)override{}
};
int main(int argc,char** argv){
    QApplication app(argc,argv);
    try {
        TransformComponentEditor form(nullptr);form.show();QCoreApplication::processEvents();
        auto* field=form.findChild<QLineEdit*>("transformPositionXEdit");Check(field,"Missing .ui Transform field");
        field->setFixedWidth(46);field->clearFocus();
        const float value=3.099999666f;const QString expected=QString::number(value,'g',9);
        NumberDisplay::Show(field,value);
        Check(field->text()==expected&&field->toolTip()==expected,"Numeric precision or full tooltip changed");
        Check(field->cursorPosition()==0&&field->cursorPositionAt(QPoint(0,field->height()/2))==0,"Narrow idle number scrolls to trailing digits");
        NumberDisplay::Show(field,-12345.678f);Check(field->text().startsWith('-')&&field->cursorPosition()==0,"Negative integer prefix hidden");
        field->setFocus();QCoreApplication::processEvents();field->setCursorPosition(3);
        NumberDisplay::Show(field,-12345.678f);Check(field->cursorPosition()==3,"Refresh moved focused caret");
        field->setSelection(1,3);NumberDisplay::Show(field,-12345.678f);Check(field->selectionStart()==1&&field->selectedText().size()==3,"Refresh lost focused selection");
        field->setSelection(4,-3);NumberDisplay::Show(field,-12345.678f);Check(field->selectionStart()==1&&field->cursorPosition()==1,"Refresh reversed active selection direction");
        field->clearFocus();NumberDisplay::Show(field,value);Check(field->cursorPosition()==0,"Blurred refresh did not return to prefix");
        if(argc>1) {
            auto* runtime=Export(argv[1]);Check(runtime,"Cannot load real Runtime");
            auto* scene=runtime->CreateScene("Readonly");auto* object=scene->CreateGameObject("Target",scene);Check(object,"Cannot create Runtime target");
            object->GetTransform()->SetLocalPosition({value,0,0});InspectorFactory factory;
            ComponentEditContext context;context.Runtime=runtime;context.SceneID=scene->GetInstanceID();context.ObjectID=object->GetInstanceID();context.Inspector=&factory;
            ComponentInfoSection section(nullptr);section.Bind(context);section.Load(*object->GetTransform());section.SetReadOnly(true);section.show();QCoreApplication::processEvents();
            auto* position=section.findChild<QLineEdit*>("transformPositionXEdit");Check(position&&!position->isEnabled(),"Play Transform field is still enabled");
            // ComponentEditor::Load calls setEnabled(true); a disabled editor host
            // must continue protecting its descendants after every live refresh.
            section.Load(*object->GetTransform());Check(!position->isEnabled(),"Load escaped read-only ancestor");
            const QString before=position->text();QTest::mouseClick(position,Qt::LeftButton);QTest::keyClicks(position,"999");Check(position->text()==before,"Read-only play field accepted edits");
            auto* fold=section.findChild<QToolButton*>("componentHeaderButton");Check(fold&&fold->isEnabled(),"Play prevented component foldout browsing");
            auto* image=object->AddComponent<GameSproutRuntime::IImageComponent>();Check(image,"Cannot create removable component");
            ComponentInfoSection imageSection(nullptr);imageSection.Bind(context);imageSection.Load(*image);imageSection.SetReadOnly(true);
            auto* remove=imageSection.findChild<QToolButton*>("removeComponentButton");Check(remove&&!remove->isEnabled(),"Play remove-component action is enabled");
            imageSection.Load(*image);Check(!remove->isEnabled(),"Refresh enabled remove-component action");
            section.SetReadOnly(false);Check(position->isEnabled(),"Stop did not restore value editing");
            delete runtime;
        }
        std::cout<<"PASS narrow numeric prefixes/full precision/tooltips/focused caret and Play read-only refresh protection\n";
        return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';return 1;}
}

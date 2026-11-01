#pragma once
#include <QMainWindow>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>

static void CheckInspectorEulerEditing(ISceneManager* scenes,GameSproutRuntime::IGameObject* character,
    QDockWidget* inspectorDock,const QDir& root,const std::function<void()>& tick) {
    auto* transform=character->GetTransform();
    auto* form=inspectorDock->findChild<QWidget*>("TransformComponentEditorForm");
    Check(form&&!form->findChild<QLineEdit*>("transformRotationWEdit"),"Normal Transform Inspector still exposes raw Quaternion W");
    const auto line=[&](const char* name) {
        auto* edit=form->findChild<QLineEdit*>(name);
        Check(edit&&!edit->isReadOnly(),std::string("Missing editable Transform axis: ")+name);return edit;
    };
    const auto commit=[&](const char* name,const QString& text) {
        auto* edit=line(name);edit->setText(text);
        Check(QMetaObject::invokeMethod(edit,"editingFinished",Qt::DirectConnection),"Cannot commit Euler axis edit");tick();
    };
    const auto same=[](GameSproutRuntime::Quaternion a,GameSproutRuntime::Quaternion b) {
        return a.X==b.X&&a.Y==b.Y&&a.Z==b.Z&&a.W==b.W;
    };
    const auto originalRotation=transform->GetLocalRotation();
    const auto originalPosition=transform->GetLocalPosition();
    const QString scenePath=root.filePath("Assets/Scenes/AnimatorInspector.scene");
    const QByteArray originalBytes=ReadFile(scenePath);
    commit("transformRotationXEdit","30");
    commit("transformRotationYEdit","45");
    const auto beforeZ=transform->GetLocalRotation();
    commit("transformRotationZEdit","60");
    const auto authored=transform->GetLocalRotation();
    const auto matrix=transform->GetLocalMatrix();
    // Independent basis-vector expectation for applying Z(60), X(30), Y(45)
    // in that order. The scene still stores a Quaternion, never Euler fields.
    Check(std::fabs(matrix(0,0)-.6597396f)<.0001f&&std::fabs(matrix(1,0)-.75f)<.0001f
        &&std::fabs(matrix(2,0)+.0473672f)<.0001f&&std::fabs(matrix(0,2)-.6123724f)<.0001f
        &&std::fabs(matrix(1,2)+.5f)<.0001f&&std::fabs(matrix(2,2)-.6123724f)<.0001f,
        "Transform Euler controls did not use the documented Z-X-Y rotation order");
    std::string error;
    Check(scenes->Undo(error)&&same(transform->GetLocalRotation(),beforeZ),"Euler rotation Undo did not restore the previous authored Quaternion");tick();
    Check(std::fabs(line("transformRotationZEdit")->text().toFloat())<.001f,"Euler rotation UI did not follow Undo");
    Check(scenes->Redo(error)&&same(transform->GetLocalRotation(),authored),"Euler rotation Redo changed the authored Quaternion");tick();

    commit("transformPositionXEdit","3.125");
    Check(transform->GetLocalPosition().X==3.125f&&same(transform->GetLocalRotation(),authored),
        "Editing position round-tripped and changed an untouched Quaternion component");
    Check(scenes->Undo(error)&&transform->GetLocalPosition().X==originalPosition.X
        &&same(transform->GetLocalRotation(),authored),"Position Undo changed the untouched Quaternion");tick();
    Check(scenes->SaveScene(error),error);
    Json::Value saved;
    Check(AssetArtifactIO::Parse(ReadFile(scenePath).toStdString(),saved),"Cannot read Euler-edited scene");
    bool found=false;
    for(const auto& object:saved["Objects"])if(object["Name"].asString()==character->GetName())
        for(const auto& component:object["Components"])if(component["Type"]=="GameSproutRuntime.Transform") {
            found=true;const auto& rotation=component["Rotation"];
            Check(rotation.isArray()&&rotation.size()==4&&!component.isMember("EulerAngles")
                &&rotation[0].asFloat()==authored.X&&rotation[1].asFloat()==authored.Y
                &&rotation[2].asFloat()==authored.Z&&rotation[3].asFloat()==authored.W,
                "Scene did not persist the edited Quaternion, or persisted display-only Euler state");
        }
    Check(found,"Saved Euler editing fixture has no Transform");
    // Equivalent angles are a display choice. Repainting or accepting 390 degrees
    // must not rewrite the stored 30-degree rotation or append an Undo command.
    commit("transformRotationXEdit","390");
    Check(same(transform->GetLocalRotation(),authored)&&!scenes->HasUnsavedChanges(),
        "Equivalent Euler input changed the Quaternion or dirtied the saved scene");
    Check(std::fabs(line("transformRotationXEdit")->text().toFloat()-390)<.001f,
        "Transform Inspector discarded the user's equivalent Euler display value");
    for(int index=0;index<3;++index)Check(scenes->Undo(error),error);
    Check(same(transform->GetLocalRotation(),originalRotation),"Euler preview checks did not restore the initial Quaternion");tick();
    Check(scenes->SaveScene(error)&&ReadFile(scenePath)==originalBytes,
        "Euler UI regression checks left changes in the source scene");
    std::cout<<"PASS Transform Euler Inspector: XYZ degrees, ZXY basis, Quaternion persistence, Undo and untouched rotation preservation\n";
}

static void CheckInspectorResponsiveLayout(QDockWidget* inspectorDock,QWidget* animatorForm,
    const std::function<void()>& tick) {
    auto* scroll=inspectorDock->findChild<QScrollArea*>("componentScroll");
    auto* direct=animatorForm->findChild<QWidget*>("clipPreviewEditor");
    auto* resource=animatorForm->findChild<QWidget*>("skeletonResource");
    auto* name=resource?resource->findChild<QLineEdit*>("resourceNameEdit"):nullptr;
    Check(scroll&&direct&&resource&&name,"Inspector layout fixture missing its actual generated forms");
    Check(!direct->findChild<QLineEdit*>("resourceNameEdit")&&direct->findChildren<QComboBox*>().size()==1,
        "Direct animation preview still duplicates its Clip reference and same-source selector");
    Check(name->cursorPosition()==0,"Resource field is scrolled to the filename suffix instead of its beginning");
    const bool floating=inspectorDock->isFloating();
    const QRect oldGeometry=inspectorDock->geometry();
    auto* window=qobject_cast<QMainWindow*>(inspectorDock->window());
    const QByteArray oldLayout=window?window->saveState():QByteArray{};
    const int oldScroll=scroll->verticalScrollBar()->value();
    struct DockScope {
        QDockWidget* Dock;QMainWindow* Window;QByteArray Layout;bool Floating;QRect Geometry;QScrollArea* Scroll;int Value;
        ~DockScope(){
            Dock->setFloating(Floating);
            if(Window&&!Layout.isEmpty())Window->restoreState(Layout);
            if(Floating)Dock->setGeometry(Geometry);
            Dock->show();Scroll->verticalScrollBar()->setValue(Value);
            QCoreApplication::processEvents();
        }
    } dockScope{inspectorDock,window,oldLayout,floating,oldGeometry,scroll,oldScroll};
    inspectorDock->setFloating(true);inspectorDock->show();
    const auto settle=[&] {
        for(int pass=0;pass<4;++pass){QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);QCoreApplication::processEvents();}
    };
    for(int width:{320,480,640}) {
        inspectorDock->resize(width,1000);settle();tick();settle();scroll->verticalScrollBar()->setValue(0);
        const std::string context=" at Inspector width "+std::to_string(width);
        Check(inspectorDock->width()<=width+2,"Inspector cannot shrink to its requested dock width"+context);
        Check(scroll->horizontalScrollBar()->maximum()==0&&scroll->widget()->width()<=scroll->viewport()->width()+1,
            "Inspector components overflow horizontally"+context);
        Check(name->width()>=resource->width()-50,"Resource actions still consume the resource-name area"+context);
        for(const char* axis:{"transformPositionXLabel","transformPositionYLabel","transformPositionZLabel",
            "transformRotationXLabel","transformRotationYLabel","transformRotationZLabel",
            "transformScaleXLabel","transformScaleYLabel","transformScaleZLabel"}) {
            auto* label=inspectorDock->findChild<QLabel*>(axis);
            Check(label&&label->isVisible()&&label->fontMetrics().horizontalAdvance(label->text())<=label->width(),
                std::string("Missing or clipped Transform axis label: ")+axis+context);
        }
        for(auto* header:inspectorDock->findChildren<QToolButton*>("componentHeaderButton"))
            if(header->isVisible())Check(header->height()<=26,"Component header is still an oversized action button"+context);
        for(QWidget* widget:scroll->widget()->findChildren<QWidget*>()) {
            if(!widget->isVisible()||(!qobject_cast<QLineEdit*>(widget)&&!qobject_cast<QComboBox*>(widget)
                &&!qobject_cast<QPushButton*>(widget)&&!qobject_cast<QToolButton*>(widget)&&!qobject_cast<QSlider*>(widget)))continue;
            const int left=widget->mapTo(scroll->viewport(),QPoint{}).x();
            Check(left>=-1&&left+widget->width()<=scroll->viewport()->width()+1,
                "Inspector control crosses the content bounds: "+widget->objectName().toStdString()+context);
        }
        const auto* play=direct->findChild<QPushButton*>("clipPlayButton");
        const auto* pause=direct->findChild<QPushButton*>("clipPauseButton");
        Check(play&&pause&&(play->isVisible()!=pause->isVisible())&&direct->height()<=180,
            "Clip transport is duplicated or takes an excessive amount of vertical space"+context);

        // The in-memory animation provider has no source Meta. Substitute only
        // presentation text for visual QA, then restore it before functional tests.
        struct Presentation {QLineEdit* Field;QString Text;int Cursor;};
        std::vector<Presentation> fields;
        std::vector<QLabel*> errors;
        for(const auto& item:std::vector<std::pair<const char*,const char*>>{
            {"skeletonResource","WomanAnimations_Skeleton"},{"controllerResource","WomanMovement_Controller"},
            {"clipResource","WomanWalk"}}) {
            auto* field=animatorForm->findChild<QWidget*>(item.first);
            if(!field)continue;
            if(auto* edit=field->findChild<QLineEdit*>("resourceNameEdit")) {
                fields.push_back({edit,edit->text(),edit->cursorPosition()});edit->setText(QString::fromUtf8(item.second));edit->setCursorPosition(0);
            }
            if(auto* label=field->findChild<QLabel*>("resourceErrorLabel"))if(label->isVisible()){errors.push_back(label);label->hide();}
        }
        auto* combo=direct->findChild<QComboBox*>("relatedClipCombo");
        const int index=combo->currentIndex();const QString clipText=combo->currentText();
        if(index>=0){const QSignalBlocker block(combo);combo->setItemText(index,"WomanWalk");}
        settle();
        const bool captured=inspectorDock->grab().save(QStringLiteral("/tmp/gamesprout-inspector-layout-%1.png").arg(width));
        if(index>=0){const QSignalBlocker block(combo);combo->setItemText(index,clipText);}
        for(const auto& field:fields){field.Field->setText(field.Text);field.Field->setCursorPosition(field.Cursor);}
        for(auto* label:errors)label->show();settle();
        Check(captured,"Cannot capture the Inspector layout"+context);
    }
    std::cout<<"PASS actual Inspector layout at 320/480/640: bounds, resource names, XYZ labels, compact headers and Clip transport\n";
}

#include "StageUIFixtures.h"
#include <iostream>

int main(int argc,char** argv) {
    QApplication application(argc,argv);
    Scenes scenes;Stage stage;stage.World=&scenes;Assets assets;Browser browser;IDE ide;SceneView sceneView;Inspector inspector;Hierarchy hierarchy;stage.InspectorView=&inspector;
    SceneEditorUI sceneCommands;PrefabStageUI view;Editor editor;
    editor.Views={{"IDEWindow",&ide},{"ProjectAssetsUI",&browser},{"SceneEditorUI",&sceneCommands},{"SceneViewUI",&sceneView},{"InspectorUI",&inspector},{"HierarchyUI",&hierarchy}};
    editor.Modules={{"PrefabStage",&stage},{"AssetsManager",&assets},{"SceneManager",&scenes}};
    bool commandsStarted=false,viewStarted=false;
    const auto release=[&] {
        if(viewStarted){GameSproutEditor::GameSproutEditorFinal::Release(view,editor);viewStarted=false;}
        if(commandsStarted){GameSproutEditor::GameSproutEditorFinal::Release(sceneCommands,editor);commandsStarted=false;}
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    };
    try {
        GameSproutEditor::GameSproutEditorFinal::Before(sceneCommands,editor);
        GameSproutEditor::GameSproutEditorFinal::Before(view,editor);
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(sceneCommands,editor),"Scene commands initialization failed");commandsStarted=true;
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(view,editor),"Stage UI initialization failed");viewStarted=true;
        Check(GameSproutEditor::GameSproutEditorFinal::Launch(sceneCommands,editor)&&GameSproutEditor::GameSproutEditorFinal::Launch(view,editor),"View registration failed");
        Pump();Check(ide.Toolbar&&!ide.Toolbar->isVisible()&&ide.ToolbarAction()&&!ide.ToolbarAction()->isVisible()&&ide.Shell()&&!ide.Shell()->isVisible()&&ide.DockCreates==0,"Stage registered a dock or visible idle toolbar");
        Check(hierarchy.Header&&!hierarchy.Header->isVisible(),"Stage header occupied the normal scene Hierarchy");
        const int idleReads=stage.Reads;
        for(int i=0;i<100;++i){GameSproutEditor::GameSproutEditorFinal::Tick(view,editor);GameSproutEditor::GameSproutEditorFinal::Tick(sceneCommands,editor);}Pump();
        Check(stage.Reads==idleReads,"Idle UI polled the stage module");
        const auto mainCamera=sceneView.Camera;const auto lockedMain=inspector.State;
        bool handled=false;const auto handlers=browser.Open;
        stage.InlineCallback=true;
        for(const auto& entry:handlers)if(entry.second({assets.Source._Guid.GetGuidString(),0,false})){handled=true;break;}
        Check(!stage.State.Active,"Prefab navigation was not deferred until the originating callback returned");stage.InlineCallback=false;Pump();
        Check(handled&&stage.Opens==1&&stage.State.Active&&ide.Toolbar->isVisible()&&sceneView.Focuses==1,"Prefab double-click did not enter and focus Stage");
        const auto checkToolbar=[&](bool active) {
            Pump();auto* action=ide.ToolbarAction();auto* shell=ide.Shell();auto* button=Find<QToolButton>(ide.Toolbar,"returnButton");
            Check(action&&shell&&action->isVisible()==active&&shell->isVisible()==active,"Stage toolbar action/host visibility did not follow its state");
            if(active) {
                const QRect rect(button->mapTo(shell,QPoint()),button->size());
                Check(button->isVisible()&&button->isEnabled()&&shell->rect().contains(rect),"Return button is not laid out inside the actual Qt toolbar");
            }
        };
        checkToolbar(true);
        Check(hierarchy.Header->isVisible()&&Find<QToolButton>(hierarchy.Header,"returnButton")->isEnabled()&&Find<QLabel>(hierarchy.Header,"sourceLabel")->toolTip()=="Hero.prefab","Stage header did not expose its source and return control in Hierarchy");
        Check(!inspector.State.Locked&&inspector.State.Scene==SceneObjectSelection{20,21},"Stage did not temporarily release the main Inspector lock for its content root");
        Check(!ide.Bar.Children.at("File")->Children.at("Save Scene As...")->Enabled,"Stage allowed Save Scene As");
        sceneCommands.SaveScene();sceneCommands.SaveSceneAs();Pump();
        Check(stage.Saves==1&&!scenes.Saves&&!scenes.SaveAs&&!Dialog("SaveSceneDialog"),"Save shortcut escaped to scene serialization");

        inspector.State.Locked=true;inspector.State.Scene={20,22};
        stage.State.Dirty=true;stage.Emit();view.ReturnToMainScene();Pump();
        auto* dirty=Dialog("PrefabStageCloseDialog");Check(dirty,"Dirty Stage had no confirmation");
        Find<QPushButton>(dirty,"cancelButton")->click();Pump();
        Check(stage.State.Active&&stage.Closes==0&&sceneView.Restores==0,"Cancel discarded Stage or main camera");
        Check(inspector.State.Locked&&inspector.State.Scene==SceneObjectSelection{20,22},"Cancel changed the current Stage Inspector lock/target");
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        stage.FailSave=true;view.ReturnToMainScene();Pump();dirty=Dialog("PrefabStageCloseDialog");
        Find<QPushButton>(dirty,"saveButton")->click();Pump();
        Check(stage.State.Active&&stage.State.Dirty&&!Find<QLabel>(dirty,"errorLabel")->text().isEmpty(),"Save failure lost edits or closed Stage");
        Find<QPushButton>(dirty,"discardButton")->click();Pump();
        Check(!stage.State.Active&&sceneView.Restores==1&&sceneView.Camera.Position.X==mainCamera.Position.X&&sceneView.Camera.Position.Y==mainCamera.Position.Y&&sceneView.Camera.Position.Z==mainCamera.Position.Z&&scenes.Dirty,"Discard Stage altered main document/camera");
        Check(inspector.State.Locked==lockedMain.Locked&&inspector.State.Scene==lockedMain.Scene&&inspector.GlobalSelection==SceneObjectSelection{10,200},"Returning Stage replaced the locked main target with global selection or changed selection");
        checkToolbar(false);
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);stage.FailSave=false;

        view.OpenPrefab(assets.Source._Guid.GetGuidString());Pump();checkToolbar(true);stage.State.Dirty=true;stage.Emit();
        sceneCommands.OpenSceneAsset("Other.scene");Pump();dirty=Dialog("PrefabStageCloseDialog");Check(dirty,"Opening scene skipped Stage confirmation");
        Find<QPushButton>(dirty,"discardButton")->click();Pump();
        auto* mainDirty=Dialog("UnsavedSceneDialog");Check(mainDirty&&!stage.State.Active&&scenes.Opens==0&&scenes.Dirty,"Closing Stage skipped main scene's independent unsaved decision");
        Find<QPushButton>(mainDirty,"cancelButton")->click();Pump();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        Check(scenes.Opens==0,"Cancelling main scene confirmation still opened another scene");

        view.OpenPrefab(assets.Source._Guid.GetGuidString());Pump();stage.State.Dirty=true;stage.Emit();
        const int opens=stage.Opens;view.OpenPrefab("fedcba9876543210fedcba9876543210");Pump();dirty=Dialog("PrefabStageCloseDialog");
        Find<QPushButton>(dirty,"cancelButton")->click();Pump();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        Check(stage.Opens==opens&&stage.State.Guid==assets.Source._Guid.GetGuidString(),"Cancel switched to a different source");
        view.ReturnToMainScene();Pump();dirty=Dialog("PrefabStageCloseDialog");Find<QPushButton>(dirty,"saveButton")->click();Pump();
        Check(!stage.State.Active&&!stage.State.Dirty,"Save then return did not close Stage");
        checkToolbar(false);
        view.OpenPrefab(assets.Source._Guid.GetGuidString());Pump();checkToolbar(true);
        stage.RevealDirtyOnClose=true;view.ReturnToMainScene();Pump();dirty=Dialog("PrefabStageCloseDialog");
        Check(dirty&&stage.State.Dirty,"A stale clean snapshot did not show the dirty decision on the first return request");
        stage.InlineCallback=true;Find<QPushButton>(dirty,"discardButton")->click();
        Check(stage.State.Active,"Discard closed Stage inside its button callback");stage.InlineCallback=false;Pump();checkToolbar(false);
        view.OpenPrefab(assets.Source._Guid.GetGuidString());Pump();checkToolbar(true);
        stage.InlineCallback=true;Find<QToolButton>(ide.Toolbar,"returnButton")->click();
        Check(stage.State.Active,"Toolbar return closed Stage inside its button callback");stage.InlineCallback=false;Pump();
        Check(!stage.State.Active,"The actual Qt toolbar return button did not close a clean Stage");checkToolbar(false);
        Check(!hierarchy.Header->isVisible(),"Hierarchy retained the Stage header after returning");
        view.OpenPrefab(assets.Source._Guid.GetGuidString());Pump();
        Find<QToolButton>(hierarchy.Header,"returnButton")->click();Pump();
        Check(!stage.State.Active&&!hierarchy.Header->isVisible(),"Hierarchy Stage header return did not restore the main scene");checkToolbar(false);
        auto* permanentToolbarWidget=new QLabel(QStringLiteral("Play controls"));
        Check(ide.Host.AttachToolbarWidget(permanentToolbarWidget),"A normal initially-hidden feature widget could not attach");Pump();
        Check(ide.Shell()->isVisible()&&permanentToolbarWidget->isVisible(),"Attaching a regular toolbar feature did not make its QWidgetAction visible");
        Check(ide.Host.SetToolbarWidgetVisible(ide.Toolbar,false)&&ide.Shell()->isVisible(),"Hiding Stage also hid another feature's visible toolbar");
        Check(ide.Host.RemoveToolbarWidget(permanentToolbarWidget),"Another toolbar feature could not be removed");checkToolbar(false);
        Check(ide.DockCreates==0,"Stage UI created an independent panel");
        const int stageOpens=stage.Opens;view.OpenPrefab(assets.Source._Guid.GetGuidString());
        GameSproutEditor::GameSproutEditorFinal::Release(view,editor);viewStarted=false;
        Pump();Check(stage.Opens==stageOpens&&!stage.State.Active,"A released Stage View retained a queued navigation operation");
        int owner=0;bool active=true;std::function<void()> delayed;
        Check(sceneCommands.RegisterDocumentHandler(&owner,[&]{return active;},[]{},[&](const std::function<void()>& value){delayed=value;}),"Cannot register another document owner");
        sceneCommands.OpenSceneAsset("Stale.scene");Check(bool(delayed),"Document navigation did not delegate close");
        sceneCommands.UnregisterDocumentHandler(&owner);active=false;delayed();Check(scenes.Opens==0,"Released document owner retained a navigation continuation");
        release();Check(stage.Listeners.empty()&&!ide.Toolbar&&!hierarchy.Header&&browser.Open.empty(),"Stage UI left callbacks/toolbar/header after release");
        std::cout<<"Stage UI double-click, save routing, dirty decisions, independent main scene prompt, camera and lifecycle checks passed\n";return 0;
    }catch(const std::exception& error){release();std::cerr<<error.what()<<'\n';return 1;}
}

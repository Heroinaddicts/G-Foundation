#include "StaticUIFixtures.h"
#include "GameSproutRuntimeExport.h"
#include <QCheckBox>
#include <QToolButton>
#include <QPushButton>
#include <QLineEdit>
#include <QMenu>
#include <QDialog>
#include <QTimer>
#include <QTemporaryDir>
#include <QDir>
#include <iostream>

int main(int argc,char** argv) {
    QApplication app(argc,argv);GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        Check(argc>=2,"Pass Runtime directory");runtime=Export(argv[1]);Check(runtime,"Runtime cannot load");
        QTemporaryDir temporary;Check(temporary.isValid(),"Cannot create UI fixture project");QDir(temporary.path()).mkpath("Assets");
        IDE ide;Project project;project.Root=temporary.path().toStdString();Assets assets;Library library;Selection selection;PlayMode play;Host host;
        Editor editor(runtime);SceneManager scenes;ObjectEditing editing;AuthoringProbe authoring;InspectorUI inspector;SceneObjectUI sceneUI;StaticUI staticUI;
        IStaticAuthoring* production=CreateProductionAuthoring();authoring.Production=production;authoring.Scenes=&scenes;
        editor.Modules={{"Project",&project},{"AssetsManager",&assets},{"LibraryManager",&library},{"SelectionManager",&selection},{"PlayMode",&play},{"SceneManager",&scenes},{"StaticAuthoring",&authoring},{"RuntimeHost",&host},{"ObjectEditing",&editing}};
        editor.Views={{"IDEWindow",&ide},{"InspectorUI",&inspector},{"SceneObjectUI",&sceneUI},{"StaticUI",&staticUI}};
        GameSproutEditor::GameSproutModuleApi moduleApi(nullptr,&editor);
        Check(GameSproutEditor::GameSproutEditorFinal::Start(scenes,moduleApi),"SceneManager failed");
        Check(GameSproutEditor::GameSproutEditorFinal::Start(editing,moduleApi),"ObjectEditing failed");
        Check(GameSproutEditor::GameSproutEditorFinal::Start(*production,moduleApi),"StaticAuthoring failed");
        const auto modulePlayListeners=play.Listeners;
        const auto moduleSelectionListeners=selection.Listeners;
        Check(GameSproutEditor::GameSproutEditorFinal::Start(inspector,editor),"InspectorUI failed");
        Check(GameSproutEditor::GameSproutEditorFinal::Start(sceneUI,editor),"SceneObjectUI failed");
        Check(GameSproutEditor::GameSproutEditorFinal::Start(staticUI,editor),"StaticUI failed");
        GameSproutEditor::GameSproutEditorFinal::Finished(inspector,editor);
        auto* scene=scenes.GetActiveScene();auto* first=scene->CreateGameObject("First");auto* second=scene->CreateGameObject("Second");
        const SceneObjectSelection firstSelection{scene->GetInstanceID(),first->GetInstanceID()};
        const SceneObjectSelection secondSelection{scene->GetInstanceID(),second->GetInstanceID()};
        selection.SetSceneObjectSelection(firstSelection);Pump(*runtime);inspector.Show();
        auto* root=ide.Inspector.Widget;root->resize(360,520);Pump(*runtime);
        auto* header=Find<QWidget>(root,"objectHeader");
        QPointer<QWidget> section=Find<QWidget>(root,"CompactStaticHeaderForm");
        Check(header->isAncestorOf(section),"Static control is not in production object header");
        auto* checkbox=Find<QCheckBox>(section,"staticCheck");auto* dropdown=Find<QToolButton>(section,"staticMenuButton");
        auto* menu=dropdown->menu();Check(menu&&menu->toolTipsVisible(),"Static menu is missing .ui tooltip support");
        auto* nothing=Find<QAction>(menu,"nothingAction");auto* everything=Find<QAction>(menu,"everythingAction");
        auto* shadowCaster=Find<QAction>(menu,"shadowCasterAction");
        const char* optionNames[]={"contributeGIAction","occluderAction","batchingAction","navigationAction","occludeeAction","offMeshLinkAction","reflectionAction"};
        using GameSproutRuntime::StaticFlags;
        Check(production->GetSupportedFlags()==StaticFlags::ShadowCasterStatic,"Product claims unavailable static optimizations");
        Check(section->isVisible()&&Find<QWidget>(root,"objectHeaderExtensions")->isVisible(),"Production Static control is hidden");
        Check(checkbox->isEnabled()&&checkbox->checkState()==Qt::Unchecked,"Static checkbox default state wrong");
        Check(everything->isEnabled()&&nothing->isEnabled()&&nothing->isChecked(),"Nothing/Everything initial availability wrong");
        Check(shadowCaster->isVisible()&&shadowCaster->isEnabled()&&!shadowCaster->isChecked(),"Static shadow caster option is unavailable");
        for(const char* name:optionNames){auto* action=Find<QAction>(menu,name);Check(!action->isVisible()&&action->isCheckable()&&!action->isEnabled(),"Unimplemented option leaked into menu");}
        const int initialWrites=authoring.Writes;Find<QAction>(menu,optionNames[0])->trigger();
        Check(authoring.Writes==initialWrites,"Disabled QAction still authored values");
        shadowCaster->trigger();Pump(*runtime);
        Check(first->GetStaticFlags()==StaticFlags::ShadowCasterStatic&&checkbox->isChecked(),"Static shadow caster option did not reach scene");
        std::string error;Check(scenes.Undo(error)&&first->GetStaticFlags()==StaticFlags::None,"Static shadow caster Undo failed");Pump(*runtime);
        Check(!checkbox->isChecked()&&!shadowCaster->isChecked(),"Static shadow caster Undo left the Inspector stale");
        const auto firstOriginal=StaticFlags::ContributeGI|StaticFlags::BatchingStatic;
        Check(first->SetStaticFlags(firstOriginal)&&second->SetStaticFlags(StaticFlags::Everything),"Cannot seed metadata");Pump(*runtime);
        Check(checkbox->checkState()==Qt::Unchecked&&checkbox->isEnabled(),"Hidden metadata changed current capability checkbox");
        Check(!Find<QAction>(menu,"contributeGIAction")->isVisible(),"Stored unavailable flag leaked into menu");
        // The visible set is the intersection of definitions and support mask.
        // Nothing/Everything/checkbox must preserve metadata outside that set.
        authoring.FutureCapabilities=true;
        authoring.FutureFlags=StaticFlags::ContributeGI|StaticFlags::OccluderStatic;
        authoring.FutureDefinitions=StaticFlags::ContributeGI;
        inspector.OnSceneObjectSelectionChanged(firstSelection);Pump(*runtime);
        Check(section->isVisible()&&Find<QAction>(menu,"contributeGIAction")->isVisible()&&!Find<QAction>(menu,"occluderAction")->isVisible(),"Definitions and capability mask were not intersected");
        Check(checkbox->checkState()==Qt::Checked&&everything->isEnabled(),"Subset Everything incorrectly requires all future flags");
        nothing->trigger();Pump(*runtime);
        Check(first->GetStaticFlags()==StaticFlags::BatchingStatic,"Nothing cleared a hidden metadata bit");
        everything->trigger();Pump(*runtime);
        Check(first->GetStaticFlags()==firstOriginal,"Everything enabled flags beyond the available subset");
        Check(scenes.Undo(error)&&first->GetStaticFlags()==StaticFlags::BatchingStatic,"Subset Everything Undo failed");
        Check(scenes.Redo(error)&&first->GetStaticFlags()==firstOriginal,"Subset Everything Redo failed");Pump(*runtime);
        authoring.FutureDefinitions=authoring.FutureFlags;
        inspector.OnSceneObjectSelectionChanged(firstSelection);Pump(*runtime);
        Check(checkbox->checkState()==Qt::PartiallyChecked,"Subset checkbox did not show partial state");
        Check(Find<QLineEdit>(root,"objectNameEdit")->width()>=40&&section->width()>=65&&section->height()<=30,"360px subset Inspector header clips Static/name controls");
        Check(std::abs(checkbox->mapTo(root,QPoint()).y()-Find<QLineEdit>(root,"objectNameEdit")->mapTo(root,QPoint()).y())<=4,"Static is not aligned in object name row");
        if(argc==3){
            const QString path=QString::fromLocal8Bit(argv[2]);Check(QDir().mkpath(path),"Cannot create screenshot directory");
            Check(root->grab().save(path+"/static-subset-test-fixture-360.png"),"Subset Inspector screenshot failed");
            menu->popup(dropdown->mapToGlobal(QPoint(0,dropdown->height())));QCoreApplication::processEvents();
            Check(menu->grab().save(path+"/static-subset-test-fixture-menu.png"),"Subset menu screenshot failed");menu->hide();
        }
        checkbox->click();Pump(*runtime);
        Check(first->GetStaticFlags()==(firstOriginal|StaticFlags::OccluderStatic),"Partial checkbox failed to enable available subset");
        checkbox->click();Pump(*runtime);
        Check(first->GetStaticFlags()==StaticFlags::BatchingStatic,"Checkbox cleared hidden metadata");
        Check(scenes.Undo(error)&&scenes.Undo(error)&&first->GetStaticFlags()==firstOriginal,"Subset checkbox Undo failed");Pump(*runtime);
        authoring.FutureFlags=StaticFlags::Everything;authoring.FutureDefinitions=StaticFlags::Everything;
        inspector.OnSceneObjectSelectionChanged(firstSelection);Pump(*runtime);
        auto* lock=Find<QToolButton>(root,"objectLockButton");lock->click();Pump(*runtime);
        selection.SetSceneObjectSelection(secondSelection);Pump(*runtime);
        Check(Find<QLineEdit>(root,"objectNameEdit")->text()=="First","Locked Inspector switched target");
        nothing->trigger();Pump(*runtime);
        Check(first->GetStaticFlags()==StaticFlags::None&&second->GetStaticFlags()==StaticFlags::Everything,"Clear followed global selection instead of locked Inspector target");
        Check(scenes.Undo(error)&&first->GetStaticFlags()==firstOriginal,"Clear cannot Undo");Pump(*runtime);
        play.Emit(EditorPlayState::Playing);Check(!checkbox->isEnabled()&&!nothing->isEnabled(),"Play permits Static editing");
        nothing->trigger();Check(first->GetStaticFlags()==firstOriginal,"Disabled Play action mutated metadata");play.Emit(EditorPlayState::Edit);
        authoring.FutureCapabilities=true;first->SetName("First editable fixture");Pump(*runtime);
        auto* occluder=Find<QAction>(menu,"occluderAction");Check(occluder->isEnabled(),"Future capability path cannot enable available menu item");
        occluder->trigger();Pump(*runtime);
        Check(first->GetStaticFlags()==(firstOriginal|StaticFlags::OccluderStatic),"Single flag edit destroyed unrelated flags");
        Check(scenes.Undo(error)&&first->GetStaticFlags()==firstOriginal,"Single flag edit Undo failed");Pump(*runtime);
        auto* child=scene->CreateGameObject("Child",first);Check(child->SetStaticFlags(StaticFlags::ReflectionProbeStatic),"Cannot seed child Static");Pump(*runtime);
        const auto answer=[&](const char* buttonName){QTimer::singleShot(0,[&,buttonName]{auto* prompt=Find<QDialog>(section,"StaticChildrenDialogForm");Find<QPushButton>(prompt,buttonName)->click();});};
        answer("cancelButton");occluder->trigger();Pump(*runtime);
        Check(first->GetStaticFlags()==firstOriginal&&child->GetStaticFlags()==StaticFlags::ReflectionProbeStatic,"Cancel changed Static flags");
        answer("selfButton");occluder->trigger();Pump(*runtime);
        Check(first->GetStaticFlags()==(firstOriginal|StaticFlags::OccluderStatic)&&child->GetStaticFlags()==StaticFlags::ReflectionProbeStatic,"Self-only change affected child");
        Check(scenes.Undo(error),"Cannot Undo self edit");Pump(*runtime);
        answer("childrenButton");occluder->trigger();Pump(*runtime);
        Check(first->GetStaticFlags()==(firstOriginal|StaticFlags::OccluderStatic)&&child->GetStaticFlags()==(StaticFlags::ReflectionProbeStatic|StaticFlags::OccluderStatic),"Subtree edit did not preserve per-node unrelated flags");
        Check(scenes.Undo(error)&&first->GetStaticFlags()==firstOriginal&&child->GetStaticFlags()==StaticFlags::ReflectionProbeStatic,"Subtree change is not one Undo transaction");Pump(*runtime);
        Check(scenes.Redo(error)&&first->GetStaticFlags()==(firstOriginal|StaticFlags::OccluderStatic)&&child->GetStaticFlags()==(StaticFlags::ReflectionProbeStatic|StaticFlags::OccluderStatic),"Subtree Redo failed");Pump(*runtime);
        answer("childrenButton");everything->trigger();Pump(*runtime);
        Check(checkbox->checkState()==Qt::Checked&&first->GetStaticFlags()==StaticFlags::Everything&&child->GetStaticFlags()==StaticFlags::Everything,"Everything did not check whole subtree");
        answer("childrenButton");checkbox->click();Pump(*runtime);
        Check(first->GetStaticFlags()==StaticFlags::None&&child->GetStaticFlags()==StaticFlags::None,"Static checkbox did not clear subtree");
        Check(!authoring.SetFlags(first->GetInstanceID(),static_cast<StaticFlags>(256),StaticFlags::Everything,false,error)&&first->GetStaticFlags()==StaticFlags::None,"Unknown flags accepted");
        authoring.FutureCapabilities=false;Check(first->SetStaticFlags(firstOriginal),"Cannot prepare screenshot metadata");Pump(*runtime);
        Check(section->isVisible()&&everything->isEnabled()&&checkbox->isEnabled()&&shadowCaster->isVisible(),"Production capabilities were not restored");
        for(const char* name:optionNames)Check(!Find<QAction>(menu,name)->isVisible(),"Unavailable item became visible after capabilities changed");
        const int reads=authoring.Reads,selectionReads=selection.Reads;
        for(int index=0;index<100;++index){GameSproutEditor::GameSproutEditorFinal::Tick(staticUI,editor);GameSproutEditor::GameSproutEditorFinal::Tick(inspector,editor);}
        Pump(*runtime);Check(reads==authoring.Reads&&selectionReads==selection.Reads,"Idle views query Module data");
        Check(Find<QLineEdit>(root,"objectNameEdit")->width()>=40,"360px production Inspector clips object name");
        if(argc==3){const QString path=QString::fromLocal8Bit(argv[2]);Check(root->grab().save(path+"/static-production-shadow-caster-360.png"),"Production Inspector screenshot failed");}
        // Header children may disappear when the main feature extension unloads.
        // InspectorUI's additive-section entries must tolerate this parent teardown.
        GameSproutEditor::GameSproutEditorFinal::Stop(sceneUI,editor);Check(!section,"Main panel unload retained child header section");Pump(*runtime);
        Check(GameSproutEditor::GameSproutEditorFinal::Start(sceneUI,editor),"Cannot reload SceneObjectUI");Pump(*runtime);
        section=Find<QWidget>(root,"CompactStaticHeaderForm");const auto listeners=play.Listeners.size();
        GameSproutEditor::GameSproutEditorFinal::Stop(staticUI,editor);Check(!section&&play.Listeners.size()+1==listeners,"Static extension retained header or Play callback after release");Pump(*runtime);
        Check(GameSproutEditor::GameSproutEditorFinal::Start(staticUI,editor),"Cannot reload StaticUI");Pump(*runtime);Check(Find<QWidget>(root,"CompactStaticHeaderForm"),"Reload did not register header extension");
        GameSproutEditor::GameSproutEditorFinal::Stop(staticUI,editor);GameSproutEditor::GameSproutEditorFinal::Stop(sceneUI,editor);GameSproutEditor::GameSproutEditorFinal::Stop(inspector,editor);
        Check(play.Listeners==modulePlayListeners&&assets.Listeners.empty()&&selection.Listeners==moduleSelectionListeners,"View release retained callbacks");
        GameSproutEditor::GameSproutEditorFinal::Stop(*production,moduleApi);delete production;
        GameSproutEditor::GameSproutEditorFinal::Stop(editing,moduleApi);
        GameSproutEditor::GameSproutEditorFinal::Stop(scenes,moduleApi);
        Check(play.Listeners.empty()&&selection.Listeners.empty(),"Module release retained callbacks");
        delete runtime;runtime=nullptr;
        std::cout<<"PASS actual Inspector header routing, hidden unavailable capabilities and subset masks, partial/clear, lock, Play guard, child choices, single Undo, event-only refresh and extension teardown\n";return 0;
    }catch(const std::exception& error){delete runtime;std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}

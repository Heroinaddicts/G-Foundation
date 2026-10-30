#include "ProjectViewTestSupport.h"
#include "InspectorPanelChecks.h"

QByteArray ReadBytes(const QString & path) {
    QFile file(path);
    Check(file.open(QIODevice::ReadOnly),"Cannot read test file");
    return file.readAll();
}

int main(int argc,char ** argv) {
    QApplication application(argc,argv);
    Foundation::FoundationApi * real=nullptr;
    try {
        QTemporaryDir temporary;
        Check(temporary.isValid(),"Cannot create inspector test project");
        QDir root(temporary.path());
        Check(root.mkpath("Assets"),"Cannot create Assets");
        QImage image(32,16,QImage::Format_RGBA8888);image.fill(QColor(45,145,220));
        Check(image.save(root.filePath("Assets/Bear.png")),"Cannot create texture");
        std::string error;
        Check(AssetArtifactIO::Write(root.filePath("Assets/Readme.txt"),"Inspector test",error),error);
        Check(AssetArtifactIO::Write(root.filePath("Assets/Triangle.obj"),"o Triangle\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n",error),error);
        real=Foundation::GetFoundationApi(TEST_BINARY_ROOT);
        Check(real&&real->Initialize(argc,argv)&&real->Launch(),"Foundation startup failed");
        TestProject project;project.Root=temporary.path().toStdString();TestConsole console;
        PreviewIDE ide;PreviewEditor editor;editor.IDE=&ide;
        editor.Modules["Project"]=&project;editor.Modules["Console"]=&console;
        FoundationProxy foundation;foundation.Real=real;foundation.Tasks.Real=real->TaskApi();foundation.Tasks.IDE=&ide.Counter;
        GameSprout::GameSproutApi api(&foundation,&editor);
        for (const std::string & name:{"LocalDatabase","LibraryManager","ShaderCompiler","AssetsPipeline","AssetsManager","GuidCreator","SelectionManager"})
            editor.Modules[name]=LoadModule(name);
        for (const auto & entry:editor.Modules) GameSprout::GameSproutEditorFinal::Before(entry.second,&api);
        for (const auto & entry:editor.Modules) Check(GameSprout::GameSproutEditorFinal::Initialize(entry.second,&api),"Module init failed");
        auto * browser=LoadProjectView();
        auto * inspector=static_cast<IInspectorUI *>(LoadView("InspectorUI"));
        for (GameSprout::IGameSproutView * view:{static_cast<GameSprout::IGameSproutView *>(browser),static_cast<GameSprout::IGameSproutView *>(inspector)}) {
            GameSprout::GameSproutEditorFinal::Before(view,&editor);
            Check(GameSprout::GameSproutEditorFinal::Initialize(view,&editor),"View init failed");
        }
        for (const auto & entry:editor.Modules) if (entry.first!="Project") Check(GameSprout::GameSproutEditorFinal::Launch(entry.second,&api),"Module launch failed");
        Check(GameSprout::GameSproutEditorFinal::Launch(&project,&api),"Project launch failed");
        Check(GameSprout::GameSproutEditorFinal::Launch(browser,&editor),"Browser launch failed");
        Check(GameSprout::GameSproutEditorFinal::Launch(inspector,&editor),"Inspector launch failed");
        auto * manager=static_cast<IAssetsManager *>(editor.Modules["AssetsManager"]);
        auto * selection=static_cast<ISelectionManager *>(editor.Modules["SelectionManager"]);
        std::string stage="startup";
        QTimer::singleShot(60000,&application,[&] { std::cerr<<"FAIL inspector stalled at "<<stage<<std::endl;std::_Exit(2); });
        auto pump=[&](const std::function<bool()> & done) {
            std::cerr<<"Inspector stage: "<<stage<<std::endl;
            const auto start=std::chrono::steady_clock::now();
            do {
                real->Update();QCoreApplication::processEvents();
                GameSprout::GameSproutEditorFinal::Update(manager,&api);
                GameSprout::GameSproutEditorFinal::Update(selection,&api);
                GameSprout::GameSproutEditorFinal::Update(browser,&editor);
                GameSprout::GameSproutEditorFinal::Update(inspector,&editor);
                if (done()) return;
                QThread::msleep(2);
            } while (std::chrono::steady_clock::now()-start<std::chrono::seconds(30));
            throw std::runtime_error("Inspector timeout: "+stage);
        };
        const auto idle=[&] {
            if (ide.Counter.Count!=0) return false;
            for (IAssetMeta * meta:manager->GetAssetMetas()) if (meta->GetImportState()==eAssetImportState::Pending) return false;
            return true;
        };
        GameSprout::GameSproutEditorFinal::Finished(manager,&api);
        pump([&]{return manager->GetAssetMetas().size()==3&&idle();});
        QWidget * content=ide.InspectorDock.Content;
        auto * tree=ide.Dock.Content->findChild<QTreeView *>("assetTree");
        auto * title=content->findChild<QLabel *>("assetNameLabel");
        auto * type=content->findChild<QLabel *>("typeLabel");
        auto * path=content->findChild<QLineEdit *>("pathEdit");
        auto * maxSize=content->findChild<QComboBox *>("maxSizeCombo");
        auto * lock=content->findChild<QToolButton *>("lockButton");
        auto * apply=content->findChild<QPushButton *>("applyButton");
        Check(tree&&title&&type&&path&&maxSize&&lock&&apply,"Inspector Designer controls missing");
        const auto click=[&](const QString & name) {
            const auto index=FindRow(tree,name);Check(index.isValid(),"Cannot select resource");
            tree->selectionModel()->setCurrentIndex(index,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
        };
        stage="texture selection";click("Bear.png");pump([&]{return title->text()=="Bear.png";});
        Check(type->text()=="Texture"&&!apply->isEnabled(),"Texture inspector initial state invalid: "+type->text().toStdString()+"; "+content->findChild<QLabel *>("messageLabel")->text().toStdString());
        const auto chosen=selection->GetAssetSelection();Check(chosen.size()==1,"Unified selection missing");
        const Guid textureGuid(chosen.front().GuidString);
        const auto readSettings=[&] {
            std::string json;Json::Value meta;
            Check(manager->QueryAsset(textureGuid)->SerializeMeta(json)&&AssetArtifactIO::Parse(json,meta),"Cannot snapshot Meta");
            return meta["ImportSettings"];
        };
        const Json::Value original=readSettings();
        const QByteArray originalMeta=ReadBytes(root.filePath("Assets/Bear.png.meta"));
        Json::Value invalid=original;invalid["MaxSize"]="invalid";
        IAssetImportSettings * originalSettings=manager->QueryAsset(textureGuid)->GetImportSettings();
        Check(!manager->ApplyImportSettings(textureGuid,AssetArtifactIO::JsonText(invalid),AssetArtifactIO::JsonText(original),error)
            &&manager->QueryAsset(textureGuid)->GetImportSettings()==originalSettings&&readSettings()==original,
            "Manager accepted invalid settings or replaced the original object");
        maxSize->setCurrentText("16");
        Check(inspector->HasPendingChanges()&&apply->isEnabled(),"Draft not detected");
        Check(readSettings()==original&&ReadBytes(root.filePath("Assets/Bear.png.meta"))==originalMeta,"Draft changed live Meta");
        inspector->RevertChanges();
        Check(!inspector->HasPendingChanges()&&maxSize->currentText()=="2048","Revert did not restore draft");
        maxSize->setCurrentText("0");
        Check(!apply->isEnabled()&&!inspector->ApplyChanges(),"Invalid draft accepted");
        Check(readSettings()==original,"Invalid draft replaced settings");
        inspector->RevertChanges();maxSize->setCurrentText("16");
        Check(inspector->ApplyChanges(),"Apply failed");
        stage="apply import";pump(idle);
        Check(readSettings()["MaxSize"].asUInt()==16&&!inspector->HasPendingChanges(),"Applied setting missing");
        auto * library=static_cast<ILibraryManager *>(editor.Modules["LibraryManager"]);
        AssetImportManifest manifest;Check(library->GetManifest(textureGuid,manifest),"Texture manifest missing");
        auto * texture=library->QueryAsset(textureGuid,manifest.Assets.front().LocalID);
        const auto metadata=ReadArtifact(texture);
        Check(metadata["Width"].asUInt()==16,"Apply did not regenerate texture payload");

        // Optimistic settings commit rejects stale drafts while retaining their UI values.
        maxSize->setCurrentText("8");
        const auto before=readSettings();Json::Value external=before;external["MaxSize"]=32;
        Check(manager->ApplyImportSettings(textureGuid,AssetArtifactIO::JsonText(external),AssetArtifactIO::JsonText(before),error),error);
        stage="external settings";pump(idle);
        Check(maxSize->currentText()=="8"&&!inspector->ApplyChanges(),"External edit overwrote draft or stale draft committed");
        inspector->RevertChanges();Check(maxSize->currentText()=="32","Revert did not load latest settings");

        // A rejected Meta save preserves the previous settings object and file.
        Check(root.rename("Assets/Bear.png.meta","Assets/Bear.saved"),"Cannot move Meta for failure test");
        Check(root.mkdir("Assets/Bear.png.meta"),"Cannot create Meta write blocker");
        maxSize->setCurrentText("8");
        Check(!inspector->ApplyChanges()&&readSettings()==external,"Failed Meta save altered committed settings");
        Check(root.rmdir("Assets/Bear.png.meta")&&root.rename("Assets/Bear.saved","Assets/Bear.png.meta"),"Cannot restore Meta");
        inspector->RevertChanges();

        QTimer dialogResponder;
        QString dialogButton;
        QObject::connect(&dialogResponder,&QTimer::timeout,&application,[&] {
            if (QWidget * dialog=QApplication::activeModalWidget()) {
                dialogResponder.stop();
                dialog->findChild<QPushButton *>(dialogButton)->click();
            }
        });
        stage="cancel switch";maxSize->setCurrentText("8");
        dialogButton="cancelButton";dialogResponder.start(5);
        click("Readme.txt");pump([&]{return selection->GetAssetSelection()==chosen&&browser->GetSelection()==chosen&&title->text()=="Bear.png";});
        Check(inspector->HasPendingChanges(),"Cancel switch lost draft");
        stage="discard switch";
        dialogButton="discardButton";dialogResponder.start(5);
        click("Readme.txt");pump([&]{return title->text()=="Readme.txt";});
        Check(!inspector->HasPendingChanges()&&readSettings()["MaxSize"].asUInt()==32,"Discard applied settings");

        stage="apply during switch";click("Bear.png");pump([&]{return title->text()=="Bear.png";});
        maxSize->setCurrentText("16");dialogButton="applyButton";dialogResponder.start(5);
        click("Readme.txt");pump([&]{return title->text()=="Readme.txt"&&idle();});
        Check(readSettings()["MaxSize"].asUInt()==16,"Switch apply did not save settings");

        stage="lock";click("Bear.png");pump([&]{return title->text()=="Bear.png";});
        lock->setChecked(true);click("Readme.txt");
        pump([&]{return selection->GetAssetSelection().front().GuidString!=textureGuid.GetGuidString();});
        Check(title->text()=="Bear.png","Locked inspector followed selection");
        lock->setChecked(false);pump([&]{return title->text()=="Readme.txt";});

        stage="subresource";click("Triangle.obj");pump([&]{return title->text()=="Triangle.obj";});
        const auto source=FindRow(tree,"Triangle.obj");tree->expand(source);QCoreApplication::processEvents();
        const auto child=tree->model()->index(0,0,source);Check(child.isValid(),"Mesh child missing");
        tree->selectionModel()->setCurrentIndex(child,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
        pump([&]{return type->text()=="Mesh";});
        Check(!content->findChild<QWidget *>("textureHost")->isVisible()&&!apply->isEnabled(),"Mesh displayed source texture settings");

        CheckInspectorPanels(root,manager,library,inspector,ide,stage,pump,idle,click);
        stage="rename";click("Bear.png");pump([&]{return title->text()=="Bear.png";});
        Check(root.rename("Assets/Bear.png","Assets/BlueBear.png"),"Cannot rename texture");
        pump([&]{return path->text()=="Assets/BlueBear.png"&&idle();});
        Check(selection->GetAssetSelection().front().GuidString==textureGuid.GetGuidString(),"Rename changed selection identity");
        Check(ide.InspectorDock.Host.grab().save("/tmp/gamesprout-inspector-ui.png"),"Cannot render inspector");
        stage="remove";maxSize->setCurrentText("8");Check(root.remove("Assets/BlueBear.png"),"Cannot delete texture");
        pump([&]{return selection->GetAssetSelection().empty()&&!inspector->HasPendingChanges()&&idle();});
        Check(!apply->isEnabled(),"Deleted resource still editable");
        inspector->Hide();inspector->Show();Check(content->isVisible(),"Inspector did not reopen");
        GameSprout::GameSproutEditorFinal::Release(inspector,&editor);
        GameSprout::GameSproutEditorFinal::Release(browser,&editor);
        GameSprout::GameSproutEditorFinal::Release(selection,&api);
        GameSprout::GameSproutEditorFinal::Release(manager,&api);
        GameSprout::GameSproutEditorFinal::Release(editor.Modules["AssetsPipeline"],&api);
        GameSprout::GameSproutEditorFinal::Release(library,&api);
        Check(ide.Counter.Count==0,"Inspector loading imbalance");real->Release();
        std::cout<<"PASS real Inspector: selection, texture draft/apply/revert, stale/save failures, switch/cancel, lock, subresources and rename/delete\n";
        return 0;
    } catch (const std::exception & exception) {
        std::cerr<<"FAIL "<<exception.what()<<"\n";
        if (real) real->Release();
        return 1;
    }
}

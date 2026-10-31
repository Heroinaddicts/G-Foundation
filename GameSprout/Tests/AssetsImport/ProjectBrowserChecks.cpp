#include "ProjectViewTestSupport.h"

int main(int argc,char **argv) {
    QApplication application(argc,argv);
    Foundation::FoundationApi * real=nullptr;
    try {
        const bool late=argc>1&&std::string(argv[1])=="--late";
        QTemporaryDir temporary;Check(temporary.isValid(),"Cannot create UI project");
        QDir root(temporary.path());Check(root.mkpath("Assets/Art/Subfolder"),"Cannot create folders");
        QImage image(8,8,QImage::Format_RGBA8888);image.fill(QColor(40,130,210));
        Check(image.save(root.filePath("Assets/Root.png")),"Cannot create image");
        std::string error;
        Check(AssetArtifactIO::Write(root.filePath("Assets/Readme.txt"),"Project assets test",error),error);
        Check(AssetArtifactIO::Write(root.filePath("Assets/Art/Triangle.obj"),"o Triangle\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n",error),error);
        real=Foundation::GetFoundationApi(TEST_BINARY_ROOT);Check(real&&real->Initialize(argc,argv)&&real->Launch(),"Foundation startup failed");
        TestProject project;project.Root=temporary.path().toStdString();TestConsole console;
        PreviewIDE ide;PreviewEditor editor;editor.IDE=&ide;editor.Modules["Project"]=&project;editor.Modules["Console"]=&console;
        FoundationProxy foundation;foundation.Real=real;foundation.Tasks.Real=real->TaskApi();foundation.Tasks.IDE=&ide.Counter;
        GameSproutEditor::GameSproutModuleApi api(&foundation,&editor);
        for(const std::string & name:{"LocalDatabase","LibraryManager","ShaderCompiler","AssetsPipeline","AssetsManager","GuidCreator","SelectionManager"})editor.Modules[name]=LoadModule(name);
        for(const auto & entry:editor.Modules)GameSproutEditor::GameSproutEditorFinal::Before(entry.second,&api);
        for(const auto & entry:editor.Modules)Check(GameSproutEditor::GameSproutEditorFinal::Initialize(entry.second,&api),"Module initialization failed");
        IProjectAssetsUI * ui=LoadProjectView();
        GameSproutEditor::GameSproutEditorFinal::Before(ui,&editor);
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(ui,&editor),"UI initialization failed");
        for(const auto & entry:editor.Modules)if(entry.first!="Project")Check(GameSproutEditor::GameSproutEditorFinal::Launch(entry.second,&api),"Module launch failed");
        Check(GameSproutEditor::GameSproutEditorFinal::Launch(&project,&api),"Project launch failed");
        auto * manager=static_cast<IAssetsManager *>(editor.Modules["AssetsManager"]);
        Recorder recorder;Check(manager->RegisterAssetListener(&recorder),"Cannot register asset listener");
        bool launched=false;
        if(!late){Check(GameSproutEditor::GameSproutEditorFinal::Launch(ui,&editor),"UI launch failed");launched=true;}
        std::string stage="startup";
        auto pump=[&](const std::function<bool()> & done) {
            const auto start=std::chrono::steady_clock::now();
            do {real->Update();QCoreApplication::processEvents();GameSproutEditor::GameSproutEditorFinal::Update(manager,&api);
                GameSproutEditor::GameSproutEditorFinal::Update(editor.Modules["SelectionManager"],&api);
                if(launched)GameSproutEditor::GameSproutEditorFinal::Update(ui,&editor);
                if(done())return;QThread::msleep(2);
            }while(std::chrono::steady_clock::now()-start<std::chrono::seconds(35));
            for(IAssetMeta * meta:manager->GetAssetMetas()) std::cerr<<"UI TIMEOUT "<<meta->_Guid.GetGuidString()<<" "<<meta->GetPath()<<" state="<<int(meta->GetImportState())<<"\n";
            std::cerr<<"Loading="<<ide.Counter.Count<<"\n";
            throw std::runtime_error("UI operation timed out: "+stage);
        };
        auto idle=[&]{if(ide.Counter.Count!=0)return false;for(IAssetMeta * meta:manager->GetAssetMetas())if(meta->GetImportState()==eAssetImportState::Pending)return false;return true;};
        GameSproutEditor::GameSproutEditorFinal::Finished(manager,&api);pump([&]{return manager->GetAssetMetas().size()>=5&&idle();});
        if(late){Check(GameSproutEditor::GameSproutEditorFinal::Launch(ui,&editor),"Late UI launch failed");launched=true;}
        auto * tree=ide.Dock.Content->findChild<QTreeView *>("assetTree");
        auto * search=ide.Dock.Content->findChild<QLineEdit *>("searchEdit");
        auto * breadcrumb=ide.Dock.Content->findChild<QComboBox *>("breadcrumbCombo");
        Check(tree&&search&&breadcrumb,"Designer controls missing");
        Check(FindRow(tree,"Art").isValid()&&FindRow(tree,"Root.png").isValid()&&tree->model()->rowCount()==3,"Initial event/snapshot synchronization failed");
        Check(ui->NavigateToDirectory("Art"),"Cannot enter folder");GameSproutEditor::GameSproutEditorFinal::Update(ui,&editor);
        const QModelIndex source=FindRow(tree,"Triangle.obj");Check(source.isValid(),"Folder query did not populate resources");
        tree->expand(source);QCoreApplication::processEvents();
        Check(tree->model()->rowCount(source)>=1,"Model subresources did not expand");
        const auto child=tree->model()->index(0,0,source);
        tree->selectionModel()->setCurrentIndex(child,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
        auto selected=ui->GetSelection();Check(selected.size()==1&&selected[0].IsSubAsset&&selected[0].LocalID!=0,"Subresource selection lost GUID/LocalID");
        const auto identity=selected[0];const Guid modelGuid(identity.GuidString);
        IAssetMeta * model=manager->QueryAsset(modelGuid);const auto children=model->GetSubAssets();
        Check(children.size()>=1,"IAssetMeta subresources missing");for(IAsset * asset:children)Check(asset->GetAssetType()!=eAssetType::Model,"Main resource was included as a child");
        stage="reimport";
        Check(manager->ReimportAsset(modelGuid),"Cannot reimport model");pump(idle);
        selected=ui->GetSelection();Check(selected.size()==1&&selected[0].GuidString==identity.GuidString&&selected[0].LocalID==identity.LocalID,"Refresh lost subresource selection");
        stage="added";
        Check(image.save(root.filePath("Assets/Art/Added.png")),"Cannot create live asset");
        pump([&]{return idle()&&FindRow(tree,"Added.png").isValid();});
        search->setText("Triangle");GameSproutEditor::GameSproutEditorFinal::Update(ui,&editor);
        Check(tree->model()->rowCount()==1&&FindRow(tree,"Triangle.obj").isValid(),"Search filter incorrect");
        search->clear();GameSproutEditor::GameSproutEditorFinal::Update(ui,&editor);
        tree->expand(FindRow(tree,"Triangle.obj"));QCoreApplication::processEvents();
        Check(ide.Dock.Host.grab().save("/tmp/gamesprout-project-assets-ui.png"),"Cannot render UI preview");
        stage="folder rename";
        std::filesystem::rename(std::filesystem::u8path(root.filePath("Assets/Art").toStdString()),std::filesystem::u8path(root.filePath("Assets/RenamedArt").toStdString()));
        pump([&]{return idle()&&breadcrumb->currentData().toString()=="RenamedArt";});
        bool renamed=false;for(const auto & event:recorder.Events)if(event.GuidValue==modelGuid&&event.OldPath=="Art/Triangle.obj"&&event.Path=="RenamedArt/Triangle.obj")renamed=true;
        Check(renamed,"Modify event omitted old/new path");
        stage="source remove";
        Check(root.remove("Assets/RenamedArt/Triangle.obj"),"Cannot remove selected source");
        pump([&]{return manager->QueryAsset(modelGuid)==nullptr&&!FindRow(tree,"Triangle.obj").isValid()&&ide.Counter.Count==0;});
        bool removed=false;for(const auto & event:recorder.Events)if(event.Action==eAssetMetaEvent::Remove&&event.GuidValue==modelGuid){removed=true;Check(event.Path=="RenamedArt/Triangle.obj","Remove snapshot lost path");}
        Check(removed,"Remove event missing");
        stage="folder remove";
        Check(QDir(root.filePath("Assets/RenamedArt")).removeRecursively(),"Cannot remove current folder");
        pump([&]{return breadcrumb->currentData().toString().isEmpty()&&FindRow(tree,"Root.png").isValid()&&ide.Counter.Count==0;});
        stage="failure display";
        Check(AssetArtifactIO::Write(root.filePath("Assets/Root.png"),"invalid PNG",error),error);
        pump([&]{const auto index=FindRow(tree,"Root.png");return index.isValid()&&index.siblingAtColumn(2).data().toString()=="导入失败"&&ide.Counter.Count==0;});
        stage="failure recovery";
        Check(image.save(root.filePath("Assets/Root.png")),"Cannot repair image");
        pump([&]{const auto index=FindRow(tree,"Root.png");return index.isValid()&&index.siblingAtColumn(2).data().toString()=="就绪"&&ide.Counter.Count==0;});
        manager->UnregisterAssetListener(&recorder);
        const auto count=recorder.Events.size();
        stage="listener unregister";
        Check(manager->ReimportAsset(manager->GetAssetsInDirectory("")[0]->_Guid),"Cannot reimport after listener removal");pump(idle);
        Check(recorder.Events.size()==count,"Unregistered listener received callbacks");
        ui->Hide();ui->Show();Check(ide.Dock.Content->isVisible(),"Hidden panel did not reopen");
        GameSproutEditor::GameSproutEditorFinal::Release(ui,&editor);
        GameSproutEditor::GameSproutEditorFinal::Release(editor.Modules["SelectionManager"],&api);
        GameSproutEditor::GameSproutEditorFinal::Release(manager,&api);
        GameSproutEditor::GameSproutEditorFinal::Release(editor.Modules["AssetsPipeline"],&api);
        GameSproutEditor::GameSproutEditorFinal::Release(editor.Modules["LibraryManager"],&api);
        Check(ide.Counter.Count==0,"UI task loading imbalance");real->Release();
        std::cout<<"PASS real ProjectAssetsUI: "<<(late?"late snapshot":"startup events")<<", directory/search/subassets, selection, rename/removal and error recovery\n";
        return 0;
    }catch(const std::exception & error){std::cerr<<"FAIL "<<error.what()<<"\n";if(real)real->Release();return 1;}
}

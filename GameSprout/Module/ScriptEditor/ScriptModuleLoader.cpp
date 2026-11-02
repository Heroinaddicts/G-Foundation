#include "ScriptEditor.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryFile>
#include <algorithm>
#include <exception>
#include <set>

namespace {
QString LibraryName() {
#if defined(_WIN32)
    return QStringLiteral("GameLogic.dll");
#elif defined(__APPLE__)
    return QStringLiteral("libGameLogic.dylib");
#else
    return QStringLiteral("libGameLogic.so");
#endif
}
bool ValidHash(const std::string& hash) {
    if(hash.size()!=64) return false;
    for(char digit:hash) if(!((digit>='0'&&digit<='9')||(digit>='a'&&digit<='f'))) return false;
    return true;
}
bool SameFields(const std::vector<GameSproutRuntime::ScriptFieldInfo>& first,
                const std::vector<GameSproutRuntime::ScriptFieldInfo>& second) {
    if(first.size()!=second.size())return false;
    for(std::size_t index=0;index<first.size();++index) {
        const auto& a=first[index];const auto& b=second[index];
        if(a.ID!=b.ID||a.Name!=b.Name||a.DeclaringType!=b.DeclaringType||a.ResourceKind!=b.ResourceKind||!(a.Default==b.Default))return false;
    }
    return true;
}
bool Copy(QIODevice& source,QIODevice& destination,std::string& error,QCryptographicHash* hash=nullptr) {
    while(!source.atEnd()) {
        const QByteArray bytes=source.read(1024*1024);
        if(bytes.isEmpty() && !source.atEnd()) {error="Cannot read GameLogic library: "+source.errorString().toStdString();return false;}
        if(destination.write(bytes)!=bytes.size()) {error="Cannot copy GameLogic library: "+destination.errorString().toStdString();return false;}
        if(hash) hash->addData(bytes);
    }
    return true;
}
bool CopyVerified(const QString& source,const QString& destination,const std::string& expected,std::string& error) {
    if(!ValidHash(expected)) {error="GameLogic result has no valid LibraryHash; compile it again";return false;}
    QFile input(source);QSaveFile output(destination);
    if(!input.open(QIODevice::ReadOnly)||!output.open(QIODevice::WriteOnly)) {error="Cannot stage GameLogic library";return false;}
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!Copy(input,output,error,&hash)||input.error()!=QFileDevice::NoError) {
        if(error.empty()) error="Cannot read GameLogic candidate: "+input.errorString().toStdString();
        return false;
    }
    if(hash.result().toHex().toStdString()!=expected) {
        error="GameLogic candidate changed after compilation; compile it again";return false;
    }
    if(!output.commit()) {error="Cannot publish staged GameLogic library: "+output.errorString().toStdString();return false;}
    return true;
}
}

QString ScriptEditor::SlotDirectory(int slot) const {
    if(_SlotsRoot.empty()||slot<0||slot>1) return {};
    return QDir(QString::fromStdString(_SlotsRoot)).filePath(slot==0?"SlotA":"SlotB");
}
bool ScriptEditor::PrepareSlots(const std::string& buildRoot,std::string& error) {
    const QString requested=QDir::cleanPath(QDir(QString::fromStdString(buildRoot)).absoluteFilePath("Loaded"));
    if(!QDir().mkpath(requested)) {error="Cannot create GameLogic Loaded directory";return false;}
    const QString root=QFileInfo(requested).canonicalFilePath();
    if(root.isEmpty()) {error="Cannot resolve GameLogic Loaded directory";return false;}
    if(_SlotsLock) {
        if(_SlotsRoot!=root.toStdString()) {error="GameLogic slots already belong to another build directory";return false;}
        return true;
    }
    auto* lock=new QLockFile(QDir(root).filePath("Slots.lock"));
    lock->setStaleLockTime(0);
    if(!lock->tryLock(0)) {delete lock;error="Another Editor process owns this project's GameLogic load slots";return false;}
    _SlotsLock=lock;_SlotsRoot=root.toStdString();return true;
}
bool ScriptEditor::RetireSlot(int slot,std::string& error) {
    if(slot<0||slot>1) return true;
    if(slot==_CurrentSlot) {error="Cannot retire the active GameLogic slot";return false;}
    auto*& library=_SlotLibraries[slot];
    if(library) {
        if(_SlotEverLoaded[slot]) {
            // Qt clears this object's Loaded tag even when unload() returns
            // false. Reacquire our own reference before each retry, otherwise
            // isLoaded()==false could conceal another QLibrary's live lease.
            if((!library->isLoaded()&&!library->load())||!library->unload()) {
                error="GameLogic "+std::string(slot==0?"SlotA":"SlotB")+" cannot be retired; release its library users and retry: "+library->errorString().toStdString();
                return false;
            }
        }
        delete library;library=nullptr;_SlotEverLoaded[slot]=false;
    }
    const QString directory=SlotDirectory(slot);
    if(!directory.isEmpty()&&QDir(directory).exists()&&!QDir(directory).removeRecursively()) {
        error="Cannot remove retired GameLogic slot: "+directory.toStdString();return false;
    }
    return true;
}
void ScriptEditor::ReleaseSlotLock() {
    if(_CurrentSlot>=0||_SlotLibraries[0]||_SlotLibraries[1]) return;
    delete _SlotsLock;_SlotsLock=nullptr;_SlotsRoot.clear();
}

bool ScriptEditor::Load(const ScriptCompileRequest& request,const ScriptCompileResult& result,std::string& error) {
    error.clear();
    if(_ReloadBlocked) {error="游戏运行会话持有当前代码，停止后才能加载新版本。";return false;}
    if(request.BuildRoot.empty()||request.ArtifactRoot.empty()||!Guid(request.AssemblyGuid).IsValid()) {
        error="GameLogic assembly/build identity is invalid";return false;
    }
    QFile schema(QString::fromStdString(result.SchemaPath));
    if(!ValidHash(result.SchemaHash)||!schema.open(QIODevice::ReadOnly)||
       QCryptographicHash::hash(schema.readAll(),QCryptographicHash::Sha256).toHex().toStdString()!=result.SchemaHash||
       schema.error()!=QFileDevice::NoError) {
        error="GameLogic 字段描述缺失或已被其他编译覆盖，请重新编译。";return false;
    }
    const QString artifactRoot=QString::fromStdString(request.ArtifactRoot);
    const QString artifactPath=QDir(artifactRoot).filePath(QString::fromStdString(request.AssemblyGuid)+"_1."+QFileInfo(LibraryName()).suffix());
    if(result.Fingerprint==_LoadedFingerprint&&CurrentLibrary()) {
        bool same=result.Types.size()==_Types.size();
        for(const auto& description:result.Types) {
            const auto current=std::find_if(_Types.begin(),_Types.end(),[&](const auto& value){return value.Type.TypeID==description.TypeID;});
            if(current==_Types.end()||current->Type.Name!=description.Name||!SameFields(current->Type.Fields,description.Fields)) {same=false;break;}
        }
        if(!same) {error="相同 GameLogic 构建标识带有不同的字段描述，请重新编译。";return false;}
        if(QFileInfo::exists(artifactPath)) return true;
        if(!QDir().mkpath(artifactRoot)) {error="Cannot create Library/Artifact";return false;}
        // Rebuild a removed Artifact from the actual successful, mapped slot.
        return CopyVerified(CurrentLibrary()->fileName(),artifactPath,_LoadedLibraryHash,error);
    }
    if(_Runtime->IsExecuting()||_Runtime->IsScriptExecutionEnabled()) {
        error="请先停止游戏逻辑，再重新加载脚本；上一次成功版本继续保留";return false;
    }
    if(!ValidHash(result.LibraryHash)) {error="GameLogic result has no valid LibraryHash; compile it again";return false;}
    if(!PrepareSlots(request.BuildRoot,error)) return false;
    const int candidateSlot=_CurrentSlot==0?1:0;
    if(!RetireSlot(candidateSlot,error)) return false;
    const QString loadedDirectory=SlotDirectory(candidateSlot);
    const QString loadedPath=QDir(loadedDirectory).filePath(LibraryName());
    const auto discard=[&] {
        std::string cleanup;
        if(!RetireSlot(candidateSlot,cleanup)) error+="\n"+cleanup;
        ReleaseSlotLock();
    };
    if(!QDir().mkpath(loadedDirectory)||!CopyVerified(QString::fromStdString(result.LibraryPath),loadedPath,result.LibraryHash,error)) {
        if(error.empty()) error="Cannot create GameLogic candidate slot";
        discard();return false;
    }
    std::vector<GameSproutRuntime::ScriptTypeRegistration> registrations;
    std::vector<EditorScriptType> types;
    std::string nextFingerprint,nextLibraryHash;
    bool published=false,artifactPublished=false;
    const bool hadArtifact=QFileInfo::exists(artifactPath);
    QTemporaryFile previousArtifact(QDir(QString::fromStdString(request.BuildRoot)).filePath("GameLogic-backup-XXXXXX"));
    try {
        auto* library=new QLibrary(loadedPath);
        _SlotLibraries[candidateSlot]=library;
        library->setLoadHints(QLibrary::ResolveAllSymbolsHint);
        if(!library->load()) error="Cannot load GameLogic candidate: "+library->errorString().toStdString();
        else _SlotEverLoaded[candidateSlot]=true;
        using fBuildFingerprint=const char* (*)() noexcept;
        const auto fingerprint=error.empty()?reinterpret_cast<fBuildFingerprint>(library->resolve("GetGameSproutGameLogicBuildFingerprint")):nullptr;
        if(error.empty()) {
            // Verify the mapped image, not just bytes on disk: macOS can retain
            // an image containing user TLS after the loader reports success.
            const char* mapped=fingerprint?fingerprint():nullptr;
            if(!mapped||result.Fingerprint!=mapped)
                error="GameLogic 映射版本与编译结果不一致；系统可能仍保留旧代码，请重新编译并重启 Editor。";
        }
        const auto get=error.empty()?reinterpret_cast<GameSproutRuntime::fGetScriptModule>(library->resolve(GameSproutRuntime::ScriptModuleEntryName)):nullptr;
        const auto* api=get?get():nullptr;
        if(error.empty()&&(!api||api->Version!=GameSproutRuntime::ScriptModuleABIVersion||(api->Count&&!api->Types)||api->Count>65536))
            error="GameLogic export or SDK ABI is invalid";
        if(error.empty()&&(!api->BuildID||result.Fingerprint!=api->BuildID||api->Count!=result.Types.size()))
            error="GameLogic 字段描述与动态库构建版本不一致。";
        nextFingerprint=result.Fingerprint;nextLibraryHash=result.LibraryHash;
        std::set<std::string> identifiers;
        // __FILE__ belongs to the one fixed Sources tree. Lexical mapping also
        // works if another build has removed that source after this compile.
        const QString buildRoot=QFileInfo(QString::fromStdString(request.BuildRoot)).canonicalFilePath();
        const QString snapshot=QDir(buildRoot).filePath("Sources");
        const QString requestedSnapshot=QDir::cleanPath(QDir(QString::fromStdString(request.BuildRoot)).absoluteFilePath("Sources"));
        for(std::size_t i=0;error.empty()&&i<api->Count;++i) {
            const auto& registration=api->Types[i];
            if(registration.Info.TypeID.empty()||registration.Info.Name.empty()||!registration.Create||!registration.Destroy||!identifiers.insert(registration.Info.TypeID).second) {
                error="GameLogic contains an invalid or duplicate TypeID";break;
            }
            const auto description=std::find_if(result.Types.begin(),result.Types.end(),[&](const auto& type){return type.TypeID==registration.Info.TypeID;});
            if(description==result.Types.end()||description->Name!=registration.Info.Name||!SameFields(description->Fields,registration.Info.Fields)) {
                error="GameLogic 的本地字段桥接与编译字段描述不一致。";break;
            }
            QString source=QString::fromStdString(registration.Info.SourceFile);
            if(!QDir::isAbsolutePath(source)) source=QDir(snapshot).filePath(source);
            source=QDir::cleanPath(source);
            if(source.startsWith(requestedSnapshot+'/')) source=QDir(snapshot).filePath(source.mid(requestedSnapshot.size()+1));
            const QString relative=QDir(snapshot).relativeFilePath(source);
            if(relative==".."||relative.startsWith("../")||QDir::isAbsolutePath(relative)) {error="Registered script is outside the Sources directory";break;}
            const auto match=std::find_if(request.Sources.begin(),request.Sources.end(),[&](const auto& entry) {
                return QDir::cleanPath(QString::fromStdString(entry.Path))==relative;
            });
            if(match==request.Sources.end()||!Guid(match->SourceGuid).IsValid()) {
                error="Registered script has no matching source .meta: "+registration.Info.SourceFile;break;
            }
            EditorScriptType type;type.Type=*description;type.Path=match->Path;
            type.Source.Guid=match->SourceGuid;type.Source.LocalID=1;type.Type.SourceFile=type.Path;
            registrations.push_back(registration);registrations.back().Source=type.Source;types.push_back(std::move(type));
        }
        if(error.empty()&&!QDir().mkpath(artifactRoot)) error="Cannot create Library/Artifact";
        QSaveFile artifact(artifactPath);
        QFile staged(loadedPath);
        if(error.empty()&&(!staged.open(QIODevice::ReadOnly)||!artifact.open(QIODevice::WriteOnly))) error="Cannot prepare GameLogic artifact";
        if(error.empty()) Copy(staged,artifact,error);
        if(error.empty()&&staged.error()!=QFileDevice::NoError) error="Cannot read staged GameLogic artifact";
        if(error.empty()&&hadArtifact) {
            QFile previous(artifactPath);
            if(!previous.open(QIODevice::ReadOnly)||!previousArtifact.open()) error="Cannot back up previous GameLogic artifact";
            if(error.empty()) Copy(previous,previousArtifact,error);
            if(error.empty()&&previous.error()!=QFileDevice::NoError) error="Cannot read previous GameLogic artifact";
            if(error.empty()&&!previousArtifact.flush()) error="Cannot flush previous GameLogic artifact backup";
        }
        std::vector<std::string> remove;
        for(const auto& old:_Registrations) remove.push_back(old.Info.TypeID);
        if(error.empty()) {
            if(!artifact.commit()) error="Cannot publish GameLogic artifact: "+artifact.errorString().toStdString();
            else artifactPublished=true;
        }
        if(error.empty()) {
            if(_Runtime->ReplaceScriptTypes(remove,registrations,error)) published=true;
            else if(error.empty()) error="Runtime rejected the GameLogic candidate";
        }
    } catch(const std::exception& exception) {
        // DLL-defined exception objects must die before candidate dlclose.
        error="Cannot prepare GameLogic candidate: "+std::string(exception.what());
    } catch(...) {error="Cannot prepare GameLogic candidate: unknown exception";}
    if(!published) {
        // This also covers exceptions crossing the Runtime implementation or a
        // test double; the prior artifact stays available until publication ends.
        if(artifactPublished) {
            bool restored=true;
            std::string restoreError;
            if(hadArtifact) {
                QSaveFile restore(artifactPath);
                restored=previousArtifact.seek(0)&&restore.open(QIODevice::WriteOnly);
                if(restored) restored=Copy(previousArtifact,restore,restoreError)&&
                    previousArtifact.error()==QFileDevice::NoError&&restore.commit();
            } else restored=QFile::remove(artifactPath);
            if(!restored) error+="\nCannot restore previous GameLogic artifact: "+restoreError;
        }
        registrations.clear();discard();return false;
    }
    const int previousSlot=_CurrentSlot;
    _CurrentSlot=candidateSlot;_Registrations.swap(registrations);_Types.swap(types);
    _LoadedFingerprint.swap(nextFingerprint);_LoadedLibraryHash.swap(nextLibraryHash);
    registrations.clear(); // Every old DLL-owned closure dies before retirement.
    if(previousSlot>=0) {
        std::string retirement;
        if(!RetireSlot(previousSlot,retirement)) {
            const std::string warning="新版本游戏逻辑已生效；旧版本槽位等待释放："+retirement;
            try {
                if(_Console) _Console->ConsoleLog(__FILE__,__LINE__,eConsoleLog::Warning,warning);
                else qWarning().noquote()<<QString::fromStdString(warning);
            } catch(...) {} // A logging extension cannot undo successful publication.
        }
    }
    return true;
}

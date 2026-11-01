#include "GameSproutEditorFinal.h"
#include "GameSproutFinal.h"
#include "FoundationExport.h"
#include "SafeSystem.hpp"
#include "GameSproutRuntimeExport.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QDebug>
#include <QStringList>

namespace GameSproutEditor {
    GameSproutEditorFinal::GameSproutEditorFinal(GameSproutRuntime::IGameSproutRuntime* runtime) : GameSproutViewApi(runtime) {}

    GameSproutEditorFinal::~GameSproutEditorFinal() {
        Release();
        FoundationApi* foundation = _Api ? _Api->_Foundation : nullptr;
        delete _Api;
        _Api = nullptr;
        delete foundation;
        for (QLibrary* library : _Libraries) {
            delete library;
        }
        _Libraries.clear();
    }

    GameSproutEditorFinal * GameSproutEditorFinal::GetInstance() {
        static GameSproutEditorFinal * s_Instance = nullptr;
        if (nullptr == s_Instance) {
            const std::string libraryDirectory =
                FoundationUtils::SafeSystem::File::GetDirectoryOfCurrentExe();

            GameSproutRuntime::IGameSproutRuntime* runtime = Export(libraryDirectory);
            if (!runtime) qFatal("Cannot load GameSproutRuntime from directory %s.", libraryDirectory.c_str());
            // Set the pointer before initialization callbacks can look it up.
            try {s_Instance = new GameSproutEditorFinal(runtime);}
            catch (...) {delete runtime;throw;}
            if (!s_Instance->Initialize()) {
                delete s_Instance;
                s_Instance = nullptr;
                qFatal("GameSprout module initialization failed; see the preceding error.");
            }
        }

        return s_Instance;
    }

    bool GameSproutEditorFinal::LoadLibraries(const char* directory, bool views) {
        const QString root = QString::fromUtf8(
            FoundationUtils::SafeSystem::File::GetDirectoryOfCurrentExe().c_str());
        const QDir folder(QDir(root).filePath(QString::fromLatin1(directory)));
        qInfo() << "Scanning" << (views ? "View" : "Module") << folder.absolutePath();
        if (!folder.exists()) {
            qInfo() << "Plugin directory does not exist; skipping:" << folder.absolutePath();
            return true;
        }

        const auto files = folder.entryInfoList(QDir::Files, QDir::Name);
        for (const QFileInfo& file : files) {
            if (!QLibrary::isLibrary(file.fileName())) {
                continue;
            }
            QString name = file.completeBaseName();
#if !defined(_WIN32)
            if (name.startsWith(QStringLiteral("lib"))) {
                name.remove(0, 3);
            }
#endif
            const std::string key = name.toUtf8().toStdString();
            if ((views && _ViewMap.count(key)) || (!views && _ModuleMap.count(key))) {
                qCritical() << "Duplicate plugin name:" << name << file.absoluteFilePath();
                return false;
            }

            QLibrary* library = new QLibrary(file.absoluteFilePath());
            try {
                _Libraries.push_back(library);
            } catch (...) {
                delete library;
                throw;
            }
            // Export macros own static instances. Keep their code resident until
            // process exit; Release is lifecycle cleanup, not delete or hot unload.
            library->setLoadHints(QLibrary::PreventUnloadHint);
            if (!library->load()) {
                qCritical() << "Cannot load plugin:" << file.absoluteFilePath()
                            << library->errorString();
                return false;
            }
            const char* entry = views ? GameSproutViewEntryName : GameSproutModuleEntryName;
            const auto symbol = library->resolve(entry);
            if (!symbol) {
                qCritical() << "Missing plugin entry:" << entry << file.absoluteFilePath()
                            << library->errorString();
                return false;
            }
            if (views) {
                using Entry = IGameSproutView* (*)() noexcept;
                IGameSproutView* view = reinterpret_cast<Entry>(symbol)();
                if (!view) {
                    qCritical() << "Null view instance:" << name;
                    return false;
                }
                _ViewMap.emplace(key, view);
            } else {
                using Entry = IGameSproutModule* (*)() noexcept;
                IGameSproutModule* module = reinterpret_cast<Entry>(symbol)();
                if (!module) {
                    qCritical() << "Null module instance:" << name;
                    return false;
                }
                _ModuleMap.emplace(key, module);
            }
            qInfo() << "Loaded" << (views ? "View" : "Module") << name
                    << library->fileName();
        }
        return true;
    }

    bool GameSproutEditorFinal::Initialize() {
        const QStringList arguments = QCoreApplication::arguments();
        std::vector<QByteArray> argumentStorage;
        argumentStorage.reserve(arguments.size());
        for (const QString& argument : arguments) {
            argumentStorage.push_back(argument.toUtf8());
        }

        std::vector<char*> args;
        args.reserve(argumentStorage.size() + 1);
        for (QByteArray& argument : argumentStorage) {
            args.push_back(argument.data());
        }
        args.push_back(nullptr);

        FoundationApi* foundation = GetFoundationApi(
            FoundationUtils::SafeSystem::File::GetDirectoryOfCurrentExe());
        if (!foundation) {
            qCritical() << "Cannot load Foundation from the executable directory.";
            return false;
        }
        try {
            if (!foundation->Initialize(static_cast<int>(arguments.size()), args.data())) {
                qCritical() << "Foundation Initialize failed.";
                foundation->Release();
                delete foundation;
                return false;
            }
            if (!foundation->Launch()) {
                qCritical() << "Foundation Launch failed.";
                foundation->Release();
                delete foundation;
                return false;
            }
            _Api = new GameSproutFinal(foundation, this);
        } catch (...) {
            foundation->Release();
            delete foundation;
            throw;
        }

        // No lifecycle method runs until both registries are complete.
        if (!LoadLibraries("Logic/Module", false)) {
            Release();
            return false;
        }
        if (!LoadLibraries("Logic/View", true)) {
            Release();
            return false;
        }

        for (const auto& [name, module] : _ModuleMap) {
            module->BeforeInitialize(_Api);
        }

        for (const auto& [name, view] : _ViewMap) {
            view->BeforeInitialize(this);
        }

        // Initialize every instance before any module or view begins Launch.
        for (const auto& [name, module] : _ModuleMap) {
            if (!module->Initialize(_Api) || !module->TriggerInitialized(_Api)) {
                qCritical() << "Module Initialize failed:" << QString::fromStdString(name);
                Release();
                return false;
            }
        }

        for (const auto& [name, view] : _ViewMap) {
            if (!view->Initialize(this) || !view->TriggerInitialized(this)) {
                qCritical() << "View Initialize failed:" << QString::fromStdString(name);
                Release();
                return false;
            }
        }

        for (const auto& [name, module] : _ModuleMap) {
            if (!module->Launch(_Api) || !module->TriggerLaunched(_Api)) {
                qCritical() << "Module Launch failed:" << QString::fromStdString(name);
                Release();
                return false;
            }
        }

        for (const auto& [name, view] : _ViewMap) {
            if (!view->Launch(this) || !view->TriggerLaunched(this)) {
                qCritical() << "View Launch failed:" << QString::fromStdString(name);
                Release();
                return false;
            }
        }
        // LaunchFinished is only delivered once all Launch calls succeed.
        for (const auto& [name, module] : _ModuleMap) {
            module->LaunchFinished(_Api);
        }
        for (const auto& [name, view] : _ViewMap) {
            view->LaunchFinished(this);
        }
        _Ready = true;
        if (auto* application = QCoreApplication::instance()) {
            QObject::connect(application, &QCoreApplication::aboutToQuit,
                             application, [this]() { Release(); });
        }
        return true;
    }

    IGameSproutView * GameSproutEditorFinal::FindGameSproutView(const std::string& name) const {
        const auto it = _ViewMap.find(name);
        return it == _ViewMap.end() ? nullptr : it->second;
    }

    IGameSproutModule * GameSproutEditorFinal::FindGameSproutModule(const std::string& name) const {
        const auto it = _ModuleMap.find(name);
        return it == _ModuleMap.end() ? nullptr : it->second;
    }

    void GameSproutEditorFinal::Update() {
        if (!_Ready) {
            return;
        }
        for (const auto& [name, module] : _ModuleMap) {
            module->Update(_Api);
        }
        for (const auto& [name, view] : _ViewMap) {
            view->Update(this);
        }
    }

    void GameSproutEditorFinal::Release() {
        _Ready = false;
        for (const auto& [name, view] : _ViewMap) {
            if(name!="IDEWindow")view->Release(this);
        }
        // The IDE container owns attached widgets. Disconnect and shut down their
        // views before deleting the container and its native viewport windows.
        const auto ide=_ViewMap.find("IDEWindow");
        if(ide!=_ViewMap.end())ide->second->Release(this);
        // Views have released their native bindings. Tear down service consumers
        // before the Runtime host; preserve the existing order for other modules.
        for(const char* name:{"AnimationPreview","SceneManager","RuntimeHost"}) {
            const auto found=_ModuleMap.find(name);
            if(found!=_ModuleMap.end())found->second->Release(_Api);
        }
        for (const auto& [name, module] : _ModuleMap) {
            if(name!="AnimationPreview"&&name!="SceneManager"&&name!="RuntimeHost")module->Release(_Api);
        }
        _ViewMap.clear();
        _ModuleMap.clear();
        if (!_RuntimeReleased) {
            _RuntimeReleased = true;
            delete _Runtime;
        }
        if (_Api) {
            _Api->_Foundation->Release();
        }
    }
}

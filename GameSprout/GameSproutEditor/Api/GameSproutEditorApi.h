#ifndef __GameSproutEditorApi_h__
#define __GameSproutEditorApi_h__

#include <functional>
#include <list>
#include <QDebug>
#include "FoundationApi.h"
#include "GRHIApi.h"
UsingFoundation;

#define UsingGameSprout using namespace GameSproutEditor;


namespace GameSproutEditor {
    class GameSproutFinal;
    class GameSproutEditorFinal;
    class IGameSproutModule;
    class IGameSproutView;

    class GameSproutViewApi {
    public:
        virtual ~GameSproutViewApi() {}
        GameSproutViewApi(GRHI::GRHIApi * const grhi) : _GRHIApi(grhi) {}

        virtual IGameSproutView * FindGameSproutView(const std::string & name) const = 0;
        template<typename V>
        V * GetGameSproutView(const std::string &name) const {
            return (V*)FindGameSproutView(name);
        }

        virtual IGameSproutModule * FindGameSproutModule(const std::string & name) const = 0;
        template<typename M>
        M * GetGameSproutModule(const std::string &name) const {
            return (M*)FindGameSproutModule(name);
        }

        GRHI::GRHIApi * const _GRHIApi;
    };

#define GetGameSproutView(editor, v, name) v = editor->GetGameSproutView<I##name>(#name);
#define GetGameSproutModule(editor, m, name) m = editor->GetGameSproutModule<I##name>(#name);

    class GameSproutModuleApi {
    public:
        virtual ~GameSproutModuleApi() {}
        GameSproutModuleApi(FoundationApi * const api, GameSproutViewApi* const editor) : _EditorApi(editor), _Foundation(api) {}

        GameSproutViewApi * const _EditorApi;
        FoundationApi * const _Foundation;
    };

    template<typename T>
    class IGameSproutEditorT {
    public:
        virtual ~IGameSproutEditorT() {}

        using fOnInitializeCallback = std::function<bool(T * const t)>;
        using fOnLaunchedCallback = std::function<bool(T * const t)>;

#define GSRegInitliazedCallback(v, callback) v->RegistInitializedCallback(callback, __FILE__, __LINE__);
        void RegistInitializedCallback(fOnInitializeCallback callback, const char * file, const int line) {
            _InitializeCallbacks.push_back(CallbackInfo(callback, nullptr, file, line));
        }

#define GSRegLaunchedCallback(v, callback) v->RegistLaunchedCallback(callback, __FILE__, __LINE__);
        void RegistLaunchedCallback(fOnLaunchedCallback callback, const char * file, const int line) {
            _LaunchedCallbacks.push_back(CallbackInfo(nullptr, callback, file, line));
        }

    protected:
        struct CallbackInfo {
            const fOnInitializeCallback _Initialize;
            const fOnLaunchedCallback _Launch;
            const char * _FILE;
            const int _LINE;
            CallbackInfo(const fOnInitializeCallback init, const fOnLaunchedCallback launch, const char * file, const int line)
                : _Initialize(init), _Launch(launch), _FILE(file), _LINE(line) {}
        };

        friend class ::GameSproutEditor::GameSproutEditorFinal;
        bool TriggerInitialized(T * const t) {
            for(auto itor=_InitializeCallbacks.begin(); itor != _InitializeCallbacks.end(); itor++) {
                if (false == (*itor)._Initialize(t)) {
                    const QString message = QStringLiteral("%1:%2 Initialize Faild").arg(
                        QString::fromUtf8((*itor)._FILE), QString::number((*itor)._LINE));
                    qCritical().noquote() << message;
                    return false;
                }
            }
            return true;
        }

        bool TriggerLaunched(T * const t) {
            for(auto itor=_LaunchedCallbacks.begin(); itor != _LaunchedCallbacks.end(); itor++) {
                if (false == (*itor)._Launch(t)) {
                    const QString message = QStringLiteral("%1:%2 Launch Faild").arg(
                        QString::fromUtf8((*itor)._FILE), QString::number((*itor)._LINE));
                    qCritical().noquote() << message;
                    return false;
                }
            }
            return true;
        }

    private:
        std::list<CallbackInfo> _LaunchedCallbacks;
        std::list<CallbackInfo> _InitializeCallbacks;
    };

    class IGameSproutView : public IGameSproutEditorT<GameSproutViewApi> {
    public:
        virtual ~IGameSproutView() {}

        virtual void Show() {}
        virtual void Hide() {}

    protected:
        virtual void BeforeInitialize(GameSproutViewApi * const editor) {}
        virtual bool Initialize(GameSproutViewApi * const editor) = 0;
        virtual bool Launch(GameSproutViewApi * const editor) = 0;
        virtual void LaunchFinished(GameSproutViewApi * const editor) {}

        virtual void Update(GameSproutEditor::GameSproutViewApi * const editor) {}

        virtual void Release(GameSproutViewApi * const editor) = 0;

        friend class GameSproutFinal;
        friend class GameSproutEditorFinal;
    };

    class IGameSproutModule : public IGameSproutEditorT<GameSproutModuleApi>  {
    public:
        virtual ~IGameSproutModule() {}

    protected:
        virtual void BeforeInitialize(GameSproutModuleApi * const api) {}
        virtual bool Initialize(GameSproutModuleApi * const api) = 0;
        virtual bool Launch(GameSproutModuleApi * const api) = 0;
        virtual void LaunchFinished(GameSproutModuleApi * const api) {}

        virtual void Update(GameSproutModuleApi * const api) {}

        virtual void Release(GameSproutModuleApi * const api) = 0;

        friend class GameSproutFinal;
        friend class GameSproutEditorFinal;
    };
}

#define GetView(editor, module, name) \
    module = editor->GetGameSproutView<I##name>(#name);


#define GetModule(editor, module, name) \
    module = editor->GetGameSproutModule<I##name>(#name);

#if defined(_WIN32)
#define GameSproutExport extern "C" __declspec(dllexport)
#else
#define GameSproutExport extern "C" __attribute__((visibility("default")))
#endif

#define GameSproutViewEntryName "GetView"
#define GameSproutViewInstance(View) \
    View s_##View; \
    GameSproutExport GameSproutEditor::IGameSproutView* (GetView)() noexcept { \
        return &s_##View; \
    }


#define GameSproutModuleEntryName "GetModule"
#define GameSproutModuleInstance(Module) \
    Module s_##Module; \
    GameSproutExport GameSproutEditor::IGameSproutModule* (GetModule)() noexcept { \
        return &s_##Module; \
    }

#endif //__GameSproutEditorApi_h__

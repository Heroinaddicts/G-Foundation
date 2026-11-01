#ifndef __GameSproutEditorFinal_h__
#define __GameSproutEditorFinal_h__

#include "GameSproutEditorApi.h"
#include <map>
#include <vector>

class QLibrary;

namespace GameSproutEditor {
    class GameSproutFinal;

    class GameSproutEditorFinal : public GameSproutViewApi {
    public:
        static GameSproutEditorFinal * GetInstance();
        ~GameSproutEditorFinal() override;

        IGameSproutView * FindGameSproutView(const std::string & name) const override;
        IGameSproutModule * FindGameSproutModule(const std::string & name) const override;

        void Update();
        void Release();

    private:
        explicit GameSproutEditorFinal(GameSproutRuntime::IGameSproutRuntime* runtime);
        GameSproutEditorFinal(const GameSproutEditorFinal&) = delete;
        GameSproutEditorFinal& operator=(const GameSproutEditorFinal&) = delete;

        bool Initialize();
        bool LoadLibraries(const char* directory, bool views);

        std::map<std::string, IGameSproutModule*> _ModuleMap;
        std::map<std::string, IGameSproutView*> _ViewMap;
        std::vector<QLibrary*> _Libraries;
        GameSproutFinal* _Api = nullptr;
        bool _Ready = false;
        bool _RuntimeReleased = false;

        friend class GameSproutFinal;
    };
}

#endif //__GameSproutEditorFinal_h__

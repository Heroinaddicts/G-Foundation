#ifndef __GameSproutFinal_h__
#define __GameSproutFinal_h__

#include "GameSproutEditorApi.h"

namespace GameSproutEditor {
    class GameSproutFinal : public GameSproutModuleApi {
    public:
        static GameSproutFinal * GetInstance();

        void Update();

    private:
        GameSproutFinal(FoundationApi * const api, GameSproutViewApi * const editor)
            : GameSproutModuleApi(api, editor) {}

        friend class GameSproutEditorFinal;
    };
}

#endif //__GameSproutFinal_h__

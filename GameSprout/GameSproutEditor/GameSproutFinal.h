#ifndef __GameSproutFinal_h__
#define __GameSproutFinal_h__

#include "GameSproutApi.h"

namespace GameSprout {
    class GameSproutFinal : public GameSproutApi {
    public:
        static GameSproutFinal * GetInstance();

        void Update();

    private:
        GameSproutFinal(FoundationApi * const api, GameSproutEditor * const editor)
            : GameSproutApi(api, editor) {}

        friend class GameSproutEditorFinal;
    };
}

#endif //__GameSproutFinal_h__

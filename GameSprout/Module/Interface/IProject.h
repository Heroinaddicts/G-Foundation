#ifndef __IProject_h__
#define __IProject_h__

#include "GameSproutEditorApi.h"

class IProject : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~IProject() {}

    virtual const std::string & GetProjectRoot() const = 0;
};

#endif //__IProject_h__

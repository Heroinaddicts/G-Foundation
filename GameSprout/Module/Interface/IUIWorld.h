#ifndef __IUIWorld_h__
#define __IUIWorld_h__

#include "IGameObject.h"

class IRectTransform : public IComponent {
public:
    virtual ~IRectTransform() {}
};

class ICanvasRender : public IComponent {
public:
    virtual ~ICanvasRender() {}
};

class IImage : public IComponent {
public:
    virtual ~IImage() {}
};

class IButton : public IComponent {
public:
    virtual ~IButton() {}
};

class ILabel : public IComponent {
public:
    virtual ~ILabel() {}
};

class IPanel : public IComponent {
public:
    virtual ~IPanel() {}
};

class ICanvas : public IComponent{
public:
    virtual ~ICanvas() {}
};

class IUIWorld : public GameSprout::IGameSproutModule {
public:
    virtual ~IUIWorld() {}

    //virtual IGameObject * CreateUIGameObject(const std::string & name) = 0;
};

#endif //__IUIWorld_h__

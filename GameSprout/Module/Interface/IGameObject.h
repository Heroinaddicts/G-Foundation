#ifndef __IGameObject_h__
#define __IGameObject_h__

#include "GameSproutApi.h"
#include "GRHIApi.h"

class IScript {
public:
    virtual ~IScript() {}

protected:
    friend class GameWorld;
    friend class UIWorld;

    virtual void OnCreate() {}
    virtual void OnStart() {}
    virtual void OnUpdate() {}
    virtual void OnRelease() {}
};

class IComponent {
public:
    virtual ~IComponent() {}
};

class IGameObject {
public:
    virtual ~IGameObject() {}

    template<typename T>
    T * AddComponent() {
        return static_cast<T*>(this->AddComponent(typeid(T)));
    }

    virtual void AddScript(IScript * const script) = 0;

    template<typename S>
    S * GetScript() {
        return static_cast<S*>(this->GetScript(typeid(S)));
    }

private:
    virtual IComponent * AddComponent(const std::type_info & info) = 0;
    virtual IScript * GetScript(const std::type_info & info) const = 0;
};

#endif //__IGameObject_h__

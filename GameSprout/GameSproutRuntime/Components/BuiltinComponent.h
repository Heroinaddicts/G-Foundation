#pragma once
#include "Api/GameSproutRuntimeApi.h"

namespace GameSproutRuntime {
template<typename Interface>
class BuiltinComponent : public Interface {
public:
    const std::type_info& GetComponentType() const override { return typeid(Interface); }
    const char* GetTypeName() const override { return _TypeName; }

protected:
    BuiltinComponent(IGameObject* owner, const char* typeName) : Interface(owner), _TypeName(typeName) {}

private:
    const char* const _TypeName;
};
}

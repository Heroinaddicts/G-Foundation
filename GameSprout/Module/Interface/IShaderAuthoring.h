#ifndef __IShaderAuthoring_h__
#define __IShaderAuthoring_h__

#include "GameSproutEditorApi.h"
#include "ResourceReference.h"

enum class BuiltinMaterialPreset : std::uint8_t { Preview, Lit };

class IShaderAuthoring : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~IShaderAuthoring() {}
    // 主线程文件事务。directory 相对于 Assets；成功仅创建源文件，Meta 和导入由资源系统处理。
    // 已有源文件或 Meta 时失败，不覆盖。返回 Assets 相对路径。
    virtual bool CreateUnlitShader(const std::string& directory,const std::string& name,
        std::string& path,std::string& error) = 0;
    virtual bool CreateBuiltinMaterial(const std::string& directory,const std::string& name,
        BuiltinMaterialPreset preset,std::string& path,std::string& error) = 0;
    // LocalID=0 表示 Shader 源文件；空引用采用当前内置预览 Shader。
    virtual bool CreateMaterial(const std::string& directory,const std::string& name,
        const GameSproutRuntime::ResourceReference& shaderSelection,std::string& path,std::string& error) = 0;
};

#endif //__IShaderAuthoring_h__

#pragma once
#include <string>
#include <vector>
#include <functional>

struct ModelTextureResolution {
    std::string Path; // 定位候选时相对Assets；后续仍须严格校验文件/IO/Meta。缺失、项目外或歧义为空。
    std::string Reason;
};

// FBX保存的Windows分隔符必须在所有宿主平台统一处理。
std::string NormalizeModelTexturePath(const std::string& path);
// 仅搜索确定的邻近目录，不递归Assets、不猜测不同后缀或不同文件名。
// materialDirectories为OBJ已读取MTL的Assets相对父目录。
ModelTextureResolution ResolveModelTexture(const std::string& assetsRoot,const std::string& sourcePath,
    const std::string& filename,const std::string& relativeFilename,
    const std::vector<std::string>& materialDirectories = {},
    const std::function<void(const std::string&)>& beforeSearchDirectory = {});

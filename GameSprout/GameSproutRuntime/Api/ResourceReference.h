#pragma once
#include <cstdint>
#include <string>

namespace GameSproutRuntime {
struct ResourceReference {
    // 资产 GUID 的规范字符串与具体资源 LocalID；空 GUID 表示未绑定。
    // 这里只保存身份，不持有源文件、Meta、预处理产物或 GPU 对象。
    std::string Guid;
    std::uint64_t LocalID = 0;
    bool operator==(const ResourceReference&) const = default;
};
}

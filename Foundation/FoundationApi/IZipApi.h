#ifndef ZIP_API_H
#define ZIP_API_H

#include "MultiSys.hpp"

namespace Foundation {
    using fUnzipCallback = std::function<void(bool success)>;

    class IZipApi {
    public:
        virtual ~IZipApi() {}

        virtual void Unzip(
            const std::string& file,
            const std::string& folder,
            const fUnzipCallback& callback) = 0;
    };
}

#endif // ZIP_API_H

#ifndef __GRHIFinal_h__
#define __GRHIFinal_h__

#include "GRHIApi.h"
#include <vector>

namespace GRHI {
    class GRHIFinal : public GRHIApi {
    public:
        ~GRHIFinal() override;

        GRHIFinal(const GRHIFinal &) = delete;
        GRHIFinal & operator=(const GRHIFinal &) = delete;

        // Main-thread access only. Repeated calls share one instance; one owner
        // deletes it after all devices and other users have finished.
        static GRHIFinal * GetInstance();

        const std::vector<AdapterInfo> & GetAdapterList() const override;
        const std::string & GetLastError() const override;
        IDevice* CreateDevice(const std::string & adapterId = DefaultDeviceAdapterID) override;
    private:
        GRHIFinal() = default;
        bool Initialize();

    private:
        std::vector<AdapterInfo> _Adapters;
        std::string _Error;
        bool _VideoInitialized = false;
        static GRHIFinal * _Instance;
    };
}

#endif //__GRHIFinal_h__

#include "vsync_provider.h"

#ifdef WB_PLATFORM_WINDOWS
#if 1
#include <Windows.h>
#include <winternl.h>
#endif
#include <D3dkmthk.h>
#include <ntstatus.h>
#include <dwmapi.h>
#endif

namespace wb {

#ifdef WB_PLATFORM_WINDOWS
struct VsyncProviderWin32 : public VsyncProvider {
    D3DKMT_WAITFORVERTICALBLANKEVENT wait_vblank {};
    bool available = false;

    VsyncProviderWin32() {
        DISPLAY_DEVICE dd {};
        dd.cb = sizeof(DISPLAY_DEVICE);

        // Find primary display device
        DWORD deviceNum = 0;
        while (EnumDisplayDevices(NULL, deviceNum, &dd, 0)) {
            if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
                break;
            deviceNum++;
        }

        HDC hdc = CreateDC(NULL, dd.DeviceName, NULL, NULL);
        if (hdc == NULL) {
            return;
        }

        D3DKMT_OPENADAPTERFROMHDC OpenAdapterData {};
        OpenAdapterData.hDc = hdc;

        if (D3DKMTOpenAdapterFromHdc(&OpenAdapterData) == STATUS_SUCCESS) {
            DeleteDC(hdc);
        } else {
            DeleteDC(hdc);
            return;
        }

        wait_vblank.hAdapter = OpenAdapterData.hAdapter;
        wait_vblank.hDevice = 0;
        wait_vblank.VidPnSourceId = OpenAdapterData.VidPnSourceId;
        available = true;
    }

    virtual void wait_for_vblank() override {
        if (available) {
            D3DKMTWaitForVerticalBlankEvent(&wait_vblank);
            //DwmFlush();
        } else {
            VsyncProvider::wait_for_vblank();
        }
    }
};
#endif

#ifdef WB_PLATFORM_WINDOWS
std::unique_ptr<VsyncProvider> g_vsync_provider {std::make_unique<VsyncProviderWin32>()};
#else
std::unique_ptr<VsyncProvider> g_vsync_provider {std::make_unique<VsyncProvider>()};
#endif
} // namespace wb
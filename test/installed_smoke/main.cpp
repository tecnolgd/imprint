// Installed-config smoke (Batch G P1.5): a foreign mini project that
// consumes the INSTALLED package — cmake --install to a staging prefix,
// then find_package(imprint CONFIG) — and drives the same headless
// frame as the add_subdirectory smoke. The point is the packaging:
// missing targets, broken include layout, or unresolved link deps in
// the export set fail here, not at a real consumer.
//
// X-1: the second half goes through the C ABI (imprint::zbapi + its
// installed header), the surface every non-C++ host rides.
#include <cstddef>
#include <cstdio>

#include "imapp.hpp"
#include "imui.hpp"
#include "zbapi.h"
#ifdef IM_SMOKE_SHELL
#include "shell/run.hpp"
#endif

using namespace zb::app;

static int c_abi_smoke()
{
    zb_app_t *app = zb_app_create(64, 32);
    if (app == nullptr)
    {
        std::printf("installed smoke: zb_app_create returned NULL\n");
        return 1;
    }
    zb_paint(app);

    uint32_t w = 0, h = 0;
    const uint8_t *pixels = zb_buffer(app, &w, &h);
    const int bpp = zb_buffer_bpp();
    const int fmt = zb_buffer_format();
    // format-agnostic: the smoke runs on whatever COLOR_DEPTH the
    // generating build used (32bpp bgra8 / 16bpp abgr1555)
    const bool format_ok = (bpp == 4 && fmt == ZB_FORMAT_BGRA8) ||
                           (bpp == 2 && fmt == ZB_FORMAT_ABGR1555);
    if (pixels == nullptr || w != 64 || h != 32 || !format_ok ||
        zb_version() != ZB_API_VERSION)
    {
        std::printf("installed smoke: C ABI buffer/format/version mismatch\n");
        zb_app_destroy(app);
        return 1;
    }

    // the frame must carry ink: the buffer is the real thing, not a stub
    bool any_ink = false;
    for (size_t i = 0; i < (size_t)w * (size_t)h * (size_t)bpp; ++i)
    {
        if (pixels[i] != 0)
        {
            any_ink = true;
            break;
        }
    }
    zb_app_destroy(app);
    if (!any_ink)
    {
        std::printf("installed smoke: C ABI frame is blank\n");
        return 1;
    }
    std::printf("installed smoke: drove a 64x32 frame through imprint::zbapi\n");
    return 0;
}

int main()
{
#ifdef IM_SMOKE_SHELL
    // X-2: take the library-mode host loop's address without running it
    // (the smoke is headless). Reference it through a volatile pointer so
    // the reference survives optimization: shell_hostloop has to be pulled
    // into this link, which is what makes the export's platform
    // requirements (X11) resolve here — the audit's `cannot find -lX11`.
    int (*volatile shell_run)(zb::SharedPtr<zb::app::IApp>, const zb::shell::run_options &) = &zb::shell::run;
    (void)shell_run;
#endif
    CanvasWindow app;
    app.create(64, 32);
    app.paint();

    if (app.data() == nullptr || app.width() != 64 || app.height() != 32)
    {
        std::printf("installed smoke: window surface not usable\n");
        return 1;
    }
    std::printf("installed smoke: painted a 64x32 headless frame via find_package(imprint)\n");
    return c_abi_smoke();
}

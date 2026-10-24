// Framework-mode thin main (code-contract §11): the loop lives in
// run_win.cpp; a library consumer calls zb::shell::run from their own
// main or WinMain instead of this executable (the subsystem is the
// consumer executable's link-time choice).
#include <windows.h>
#include "app_maker.hpp"
#include "shell/main_guard.hpp"
#include "shell/run.hpp"

extern "C" int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    // X-5: every escape becomes a diagnostic + exit 1 (main_guard.hpp)
    return zb::shell::guarded_main(
        []
        { return zb::shell::run(zb::app::make_app()); });
}

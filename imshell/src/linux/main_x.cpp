// Framework-mode thin main (code-contract §11): the loop lives in
// run_x11.cpp; a library consumer calls zb::shell::run from their own
// main instead of this executable.
#include <iostream>
#include "app_maker.hpp"
#include "logging.hpp"
#include "shell/main_guard.hpp"
#include "shell/run.hpp"

int main(int, char *[])
{
    zb::Logging::set_log_handle([](const zb::Logging_Level &, const std::string &message)
    {
        std::cerr << message;
    });

    LD << "run on X";
    // X-5: every escape becomes a diagnostic + exit 1 (main_guard.hpp)
    return zb::shell::guarded_main(
        [] { return zb::shell::run(zb::app::make_app()); });
}

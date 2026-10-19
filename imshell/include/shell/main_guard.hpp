#pragma once

#include <exception>
#include <string>

#include "core/error.hpp"
#include "logging.hpp"

namespace zb::shell
{
    namespace main_guard_detail
    {
        /*
         * Best-effort diagnostic. This path runs precisely when memory or
         * a platform call has already failed, and it is reached from a
         * catch block, so a throw out of it would reach std::terminate --
         * exactly what the guard exists to stop. Two throw sources are
         * contained here:
         *
         *  - building the message (the string, the handler copy inside
         *    Logging::log) can fail with bad_alloc;
         *  - the installed log handler itself may throw.
         *
         * The second is why this calls Logging::log instead of the LE
         * macro (code-contract §3 carve-out): the macro's Stream temporary
         * runs Logging::log from its destructor, which is implicitly
         * noexcept, so a throwing handler would terminate inside the
         * stream's destruction and no catch here could see it. Calling
         * the sink directly keeps the throw inside this try.
         */
        inline void report(const char *what) noexcept
        {
            try
            {
                zb::Logging::log(zb::Logging_Level::error,
                                 std::string("fatal: ") +
                                     (what != nullptr ? what
                                                      : "unknown exception"));
            }
            catch (...)
            {
            }
        }
    }

    /*
     * X-5 (the W-14 "shell mains catch only zb::ui::error" class): every
     * framework-mode main used to catch exactly one exception type, so
     * any other escape -- std::bad_alloc from a design-file value, a
     * container's length_error, a platform API's runtime_error -- walked
     * out of main and called std::terminate with no diagnostic at all.
     * guarded_main is the single home of the rule for every shell main:
     * run the body once, turn every escape into one log line plus a
     * nonzero exit. Exception support is required on every target that
     * has a main (the NDS toolchain passes -fexceptions too).
     *
     * Library-mode hosts do NOT use this: they own their main and decide
     * their own failure policy (code-contract §11).
     */
    template <typename Body>
    int guarded_main(Body &&body)
    {
        try
        {
            return static_cast<int>(body());
        }
        catch (const zb::ui::error &e)
        {
            main_guard_detail::report(e.what());
        }
        catch (const std::exception &e)
        {
            main_guard_detail::report(e.what());
        }
        catch (...)
        {
            main_guard_detail::report(nullptr);
        }
        return 1;
    }
}

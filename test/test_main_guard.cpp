#include "test.hpp"

#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/error.hpp"
#include "logging.hpp"
#include "shell/main_guard.hpp"

namespace
{
    /*
     * Captures the messages the guard emits. The guard runs precisely
     * when memory or a platform call has already failed, so the test also
     * covers a handler that throws: the diagnostic is best-effort and an
     * unwinding throw out of it would reach std::terminate, which is what
     * the guard exists to prevent.
     */
    struct log_probe
    {
        std::vector<std::string> messages;
        bool throw_on_log = false;

        log_probe()
        {
            zb::Logging::set_log_handle(
                [this](const zb::Logging_Level level, const std::string &msg) {
                    if (throw_on_log)
                    {
                        throw std::bad_alloc();
                    }
                    if (level == zb::Logging_Level::error)
                    {
                        messages.push_back(msg);
                    }
                });
        }

        ~log_probe() { zb::Logging::set_log_handle(); }

        log_probe(const log_probe &) = delete;
        log_probe &operator=(const log_probe &) = delete;
    };
}

int test_main_guard()
{
    // a body that returns normally passes its value through untouched
    {
        log_probe probe;
        EXPECT(zb::shell::guarded_main([] { return 0; }) == 0);
        EXPECT(zb::shell::guarded_main([] { return 42; }) == 42);
        EXPECT(probe.messages.empty());
    }

    // the init-path error type: one error-level diagnostic and a nonzero
    // exit instead of an escape out of main
    {
        log_probe probe;
        const int rc = zb::shell::guarded_main(
            []() -> int { throw zb::ui::error("no display"); });
        EXPECT(rc == 1);
        EXPECT(probe.messages.size() == 1);
        if (probe.messages.size() == 1)
        {
            EXPECT(probe.messages[0].find("no display") != std::string::npos);
        }
    }

    // X-5: every other std::exception is the class the shells used to let
    // escape -- a design-file value's bad_alloc walked out of main and
    // called std::terminate with no diagnostic at all
    {
        log_probe probe;
        EXPECT(zb::shell::guarded_main(
                   []() -> int { throw std::bad_alloc(); }) == 1);
        EXPECT(probe.messages.size() == 1);

        probe.messages.clear();
        EXPECT(zb::shell::guarded_main([]() -> int {
                   throw std::length_error("too long");
               }) == 1);
        EXPECT(probe.messages.size() == 1);
        if (probe.messages.size() == 1)
        {
            EXPECT(probe.messages[0].find("too long") != std::string::npos);
        }

        probe.messages.clear();
        EXPECT(zb::shell::guarded_main([]() -> int {
                   throw std::runtime_error("device gone");
               }) == 1);
        EXPECT(probe.messages.size() == 1);
    }

    // a non-std throw still gets a diagnostic (what() unavailable)
    {
        log_probe probe;
        EXPECT(zb::shell::guarded_main([]() -> int { throw 7; }) == 1);
        EXPECT(probe.messages.size() == 1);
        if (probe.messages.size() == 1)
        {
            EXPECT(probe.messages[0].find("unknown exception") !=
                   std::string::npos);
        }
    }

    // the diagnostic itself must not escape: a throwing log handler is
    // swallowed and the exit code is still nonzero (terminate otherwise)
    {
        log_probe probe;
        probe.throw_on_log = true;
        EXPECT(zb::shell::guarded_main(
                   []() -> int { throw zb::ui::error("boom"); }) == 1);
        EXPECT(zb::shell::guarded_main(
                   []() -> int { throw std::bad_alloc(); }) == 1);
        EXPECT(zb::shell::guarded_main([]() -> int { throw 'x'; }) == 1);
    }

    return test::report("main_guard");
}

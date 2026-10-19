// X-7: a shell event loop is executed, not only compiled.
//
// The battery used to run pure translators and the presenter seam in
// isolation: no `zb::shell::run` loop ever executed, which is exactly
// where the X-4 frame-scope leak lived and where X-10 (the 16bpp
// presenter asymmetry) sits. This suite drives the real X11 loop --
// event pump -> feed_input -> paint -> region_to_present -> teardown --
// against a self-closing probe app, and it runs the loop twice in one
// process to pin code-contract 11.1: "a second run in the same process
// starts from the state the first one started from".
//
// Headless: the loop needs an X display. A battery run without one skips
// (desk and CI containers have no server); CI's linux-x11 job wraps this
// suite in Xvfb and sets IM_TEST_REQUIRE_DISPLAY=1, which turns a missing
// display into a FAILURE so the coverage cannot silently disappear.
//
// Compiled only for the X11 backend (test/CMakeLists.txt): the FB and
// SIXEL targets have no `run` and no shell_hostloop to link.
#include "test.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "canvas_window.hpp"
#include "core/error.hpp"
#include "iapp.hpp"
#include "shell/run.hpp"
#include "theme.hpp"

#include <X11/Xlib.h>

using namespace zb::app;

namespace
{
    /*
     * Minimal IApp adapter (the hello/ pattern) with an empty widget
     * tree: every paint covers the frame with the theme background, so
     * the test reads the loop's effect straight out of the buffer.
     *
     * `frames_` counts painted callbacks -- the observable that the shell
     * pumped a frame through an Expose. The subscription is app-owned (a
     * real app registers it when it creates its window); the test detaches
     * it for the second run, which needs the app's event to be empty so
     * the shell's own teardown is observable.
     */
    class loop_probe : public IApp
    {
    public:
        void create_window() override { create_window(96, 64); }

        void create_window(const uint32_t w, const uint32_t h) override
        {
            create_window(w, h, nullptr);
        }

        void create_window(uint32_t w, uint32_t h, void *buffer) override
        {
            window_ = zb::make_shared<CanvasWindow>();
            window_->create(w, h, buffer);
            frames_ = 0;
            painted_sub_ = window_->painted.subscribe([this](const void *) {
                ++frames_;
                if (close_on_first_frame_)
                {
                    window_->close();
                }
            });
        }

        zb::SharedPtr<IWindow> window() noexcept override { return window_; }

        void input(const zb::input::input_event &ev) noexcept override
        {
            if (window_)
            {
                window_->input(ev);
            }
        }

        void paint() noexcept override
        {
            if (window_)
            {
                window_->paint();
            }
        }

        [[nodiscard]] bool is_dirty() const noexcept override
        {
            return window_ && window_->is_dirty();
        }

        [[nodiscard]] bool dirty_region(int &x, int &y, int &w, int &h) const noexcept override
        {
            return window_ && window_->dirty_region(x, y, w, h);
        }

        zb::event::Subscription<const void *> on_painting(
            zb::event::PAINT_EVENT::EventHandler h) noexcept override
        {
            return window_ ? window_->painting.subscribe(h)
                           : zb::event::Subscription<const void *>();
        }

        zb::event::Subscription<const void *> on_painted(
            zb::event::PAINT_EVENT::EventHandler h) noexcept override
        {
            return window_ ? window_->painted.subscribe(h)
                           : zb::event::Subscription<const void *>();
        }

        zb::event::Subscription<> on_closing(
            zb::event::CLOSE_EVENT::EventHandler h) noexcept override
        {
            return window_ ? window_->closing.subscribe(h) : zb::event::Subscription<>();
        }

        zb::event::Subscription<> on_closed(
            zb::event::CLOSE_EVENT::EventHandler h) noexcept override
        {
            return window_ ? window_->closed.subscribe(h) : zb::event::Subscription<>();
        }

        // the test-side observables
        void reset_frames() { frames_ = 0; }
        [[nodiscard]] int frames() const { return frames_; }
        void set_close_on_first_frame(const bool on) { close_on_first_frame_ = on; }
        void detach_frame_handler() { painted_sub_.reset(); }
        [[nodiscard]] CanvasWindow *canvas() { return window_.get(); }

    private:
        zb::SharedPtr<CanvasWindow> window_;
        zb::event::Subscription<const void *> painted_sub_;
        int frames_ = 0;
        bool close_on_first_frame_ = true;
    };

    /*
     * run() is an init path (code-contract 11.2) and may throw on a
     * platform failure; the suite reports that as a failure instead of
     * letting it escape the battery.
     */
    int run_loop(const zb::SharedPtr<IApp> &app, const zb::shell::run_options &options)
    {
        try
        {
            return zb::shell::run(app, options);
        }
        catch (const zb::ui::error &e)
        {
            std::printf("[shell_loop] run() threw: %s\n", e.what());
            return -1;
        }
    }

    bool display_reachable()
    {
        Display *display = XOpenDisplay(nullptr);
        if (display == nullptr)
        {
            return false;
        }
        XCloseDisplay(display);
        return true;
    }

    void report_skip(const char *reason)
    {
        std::printf("[shell_loop] skipped: %s\n", reason);
    }
}

int test_shell_loop()
{
    // 16bpp builds: the X11 shell presents the 32bpp internal layout only
    // and refuses at run()'s entry (code-contract 4.8 / X-10), so there is
    // no loop to run here; the 32bpp battery covers it
    if (sizeof(zb::ui::core::Color) != 4)
    {
        report_skip("COLOR_DEPTH=16 presents the 32bpp layout only");
        return test::report("shell_loop");
    }

    if (!display_reachable())
    {
        if (std::getenv("IM_TEST_REQUIRE_DISPLAY") != nullptr)
        {
            const char *display = std::getenv("DISPLAY");
            std::printf("[shell_loop] FAIL: IM_TEST_REQUIRE_DISPLAY is set but no X "
                        "display is reachable (DISPLAY=%s)\n",
                        display != nullptr ? display : "<unset>");
            EXPECT(false);
            return test::report("shell_loop");
        }
        report_skip("no X display reachable (CI drives this suite under Xvfb)");
        return test::report("shell_loop");
    }

    auto probe = zb::make_shared<loop_probe>();
    zb::SharedPtr<IApp> app = probe;

    zb::shell::run_options options;
    options.width = 96;
    options.height = 64;

    // ---- run 1: the shell creates the window (the app has none yet),
    // one Expose paints the frame, the probe closes the app, the loop
    // returns ----
    int rc = run_loop(app, options);
    EXPECT(rc == 0);
    EXPECT(probe->frames() >= 1);
    EXPECT(probe->canvas() != nullptr);
    const int frames_run1 = probe->frames();

    const auto *pixels =
        probe->canvas() != nullptr
            ? static_cast<const zb::ui::core::Color *>(probe->canvas()->data())
            : nullptr;
    EXPECT(pixels != nullptr);
    if (pixels != nullptr)
    {
        // the loop painted the app's buffer: the empty tree leaves the
        // theme background over the whole frame
        EXPECT(pixels[0].pixel == zb::ui::theme().background.pixel);
    }

    CanvasWindow *window = probe->canvas();
    const auto buffer_bytes =
        window != nullptr
            ? static_cast<std::size_t>(window->width()) *
                  static_cast<std::size_t>(window->height()) *
                  sizeof(zb::ui::core::Color)
            : 0;
    EXPECT(buffer_bytes > 0);

    // ---- run 2: the same app, a second loop in the same process ----
    // (code-contract 11.1: nothing of run 1's frame scope survives, the
    // app's existing window is used as-is -- run must not recreate it --
    // and the new presenter receives a fresh frame)
    probe->detach_frame_handler();  // the test owns the handler now, so
                                    // an empty event proves run()'s own
                                    // subscriptions detached
    probe->set_close_on_first_frame(false);
    probe->reset_frames();
    if (window != nullptr && buffer_bytes > 0)
    {
        // a frame run 2 owes: run 1's paint consumed the initial dirty
        // flag, so mark the app dirty and scribble the buffer -- the
        // repaint below is then observable
        std::memset(window->data(), 0xA5, buffer_bytes);
        window->invalidate();
    }

    int frames_run2 = 0;
    {
        // the window exists, so the subscription is real (an IApp with no
        // window returns an empty one -- the no-window path run 1 covers)
        auto sub = app->on_painted([&](const void *) {
            ++frames_run2;
            probe->set_close_on_first_frame(false);
            window->close();
        });
        rc = run_loop(app, options);
    }
    EXPECT(rc == 0);
    EXPECT(frames_run2 >= 1);
    EXPECT(probe->canvas() == window);  // existing-window path
    if (pixels != nullptr)
    {
        EXPECT(pixels[0].pixel == zb::ui::theme().background.pixel);
    }

    // 11.1: no shell handler survives its loop -- the event is back to
    // empty (the test's own subscription died with the scope), so a host
    // that keeps driving the app after run() returns cannot call into a
    // presenter whose window is gone (the review-4 dangling-closure class)
    if (window != nullptr)
    {
        EXPECT(!static_cast<bool>(window->painted));
        EXPECT(!static_cast<bool>(window->closed));
        EXPECT(window->data() != nullptr);
    }

    std::printf("[shell_loop] X11 loop executed: 2 run() calls, "
                "run-1 frames=%d, run-2 frames=%d, buffer=%zu bytes\n",
                frames_run1, frames_run2, buffer_bytes);
    return test::report("shell_loop");
}

// Logger behavior. Headless. Exits non-zero if any check fails.
// With SPDLOG_LEVEL=MARLE=trace,APP=error set it checks the environment override instead
// of the defaults (the levels are fixed when the loggers are first used).
#include "Marle/Log.h"
#include <spdlog/sinks/ostream_sink.h>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

using namespace Marle;

static int fails = 0;
#define CHECK(c, name) do { if (!(c)) { fails++; printf("FAIL: %s\n", name); } else { printf("ok:   %s\n", name); } } while (0)

static bool Contains(const std::string& haystack, const std::string& needle) { return haystack.find(needle) != std::string::npos; }

int main()
{
    // Lazy init: no Log::Init() call before first use
    auto& core = Log::GetCoreLogger();
    auto& app = Log::GetClientLogger();
    CHECK(core && app && core != app, "loggers exist without calling Init()");
    CHECK(core->name() == "MARLE" && app->name() == "APP", "loggers are named MARLE and APP");

    Log::Init();
    CHECK(core == Log::GetCoreLogger(), "Init() is idempotent");

    // Capture what the loggers emit
    std::ostringstream coreOut, appOut;
    core->sinks().push_back(std::make_shared<spdlog::sinks::ostream_sink_mt>(coreOut));
    app->sinks().push_back(std::make_shared<spdlog::sinks::ostream_sink_mt>(appOut));

    MRL_CORE_TRACE("core trace {}", 1);
    MRL_CORE_INFO("core info {}x{}", 640, 480);
    MRL_CORE_WARN("core warn {}", "w");
    MRL_CORE_ERROR("core error {}", 4);
    MRL_CORE_FATAL("core fatal {}", 5);
    MRL_TRACE("app trace {}", 1);
    MRL_INFO("app info {}", 2);
    MRL_WARN("app warn {}", 3);
    MRL_ERROR("app error {}", 4);
    MRL_FATAL("app fatal {}", 5);

    CHECK(Contains(coreOut.str(), "core info 640x480"), "core macros format fmt-style arguments");
    CHECK(Contains(coreOut.str(), "core warn w") && Contains(coreOut.str(), "core error 4") && Contains(coreOut.str(), "core fatal 5"), "core warn/error/fatal are emitted");
    CHECK(!Contains(appOut.str(), "core"), "client output does not include core messages");

    const char* env = getenv("SPDLOG_LEVEL");
    if (!env) {
        CHECK(!Contains(coreOut.str(), "core trace") && !Contains(appOut.str(), "app trace"), "trace is off by default");
        CHECK(Contains(appOut.str(), "app info 2") && Contains(appOut.str(), "app warn 3") && Contains(appOut.str(), "app error 4") && Contains(appOut.str(), "app fatal 5"), "client macros emit info and above by default");

        core->set_level(spdlog::level::trace);
        MRL_CORE_TRACE("core trace again");
        CHECK(Contains(coreOut.str(), "core trace again"), "the level can be lowered at runtime");
    } else {
        CHECK(Contains(coreOut.str(), "core trace 1"), "SPDLOG_LEVEL=MARLE=trace enables core trace");
        CHECK(!Contains(appOut.str(), "app info") && !Contains(appOut.str(), "app warn"), "SPDLOG_LEVEL=APP=error filters client info/warn");
        CHECK(Contains(appOut.str(), "app error 4"), "client errors still pass at APP=error");
    }

    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}

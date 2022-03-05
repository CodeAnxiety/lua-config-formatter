#include "logging.h"
#include "args.h"

#include <fmt/color.h>

using namespace app;

namespace
{
    constexpr fmt::text_style to_style(log_level level)
    {
        switch (level) {
            case log_level::debug: return fmt::fg(fmt::terminal_color::yellow);
            case log_level::verbose: return fmt::fg(fmt::terminal_color::cyan);
            default:
            case log_level::info:
                return fmt::fg(fmt::terminal_color::bright_white);
            case log_level::error:
                return fmt::fg(fmt::terminal_color::bright_red);
            case log_level::fatal:
                return fmt::bg(fmt::terminal_color::red)
                     | fmt::fg(fmt::terminal_color::bright_white);
        }
    }

    constexpr int to_verbosity(log_level level)
    {
        switch (level) {
            case log_level::debug: return 2;
            case log_level::verbose: return 1;
            default:
            case log_level::info: return 0;
            case log_level::error: return -1;
            case log_level::fatal: return -2;
        }
    }

    constexpr std::string_view to_prefix(log_level level)
    {
        switch (level) {
            case log_level::debug: return "DEBUG: ";
            default:
            case log_level::verbose:
            case log_level::info: return "";
            case log_level::error:
            case log_level::fatal: return "ERROR: ";
        }
    }
}

bool app::should_print(log_level level)
{
    return args.verbosity >= to_verbosity(level);
}

void app::print(log_level level, std::string_view message, int depth)
{
    if (!should_print(level))
        return;

    auto* out = (level >= log_level::error) ? (stderr) : (stdout);

    auto style = to_style(level);

    if (depth > 0)
        fmt::print(out, style, "{:>{}}", "- ", (depth - 1) * 2);

    fmt::print(out, style, to_prefix(level));
    fmt::print(out, style, message);
    fmt::print(out, "\n");

    if (level >= log_level::fatal) {
        std::fflush(stdout);
        std::fflush(stderr);
        std::exit(1);
    }
}

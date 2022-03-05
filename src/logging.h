#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <fmt/format.h>

template <>
struct fmt::formatter<std::filesystem::path> : formatter<std::string>
{
    template <typename FormatContext>
    auto format(const std::filesystem::path & path, FormatContext & ctx)
    {
        return formatter<std::string>::format(path.string(), ctx);
    }
};

namespace app
{
    enum class log_level : int
    {
        debug,
        verbose,
        info,
        error,
        fatal,
    };

    bool should_print(log_level level);
    void print(log_level level, std::string_view message, int depth = 0);

#define IMPLEMENT_LOG_LEVEL(level)                                             \
    template <typename... Args>                                                \
    inline void level(int depth, std::string_view message, Args &&... args)    \
    {                                                                          \
        if (!should_print(log_level::level))                                   \
            return;                                                            \
        print(log_level::level,                                                \
              fmt::format(fmt::runtime(message), std::forward<Args>(args)...), \
              depth);                                                          \
    }                                                                          \
                                                                               \
    template <typename... Args>                                                \
    inline void level(std::string_view message, Args &&... args)               \
    {                                                                          \
        level(0, message, std::forward<Args>(args)...);                        \
    }                                                                          \
                                                                               \
    inline void level(std::string_view message)                                \
    {                                                                          \
        print(log_level::level, message);                                      \
    }                                                                          \
                                                                               \
    inline void level(int depth, std::string_view message)                     \
    {                                                                          \
        print(log_level::level, message, depth);                               \
    }

    IMPLEMENT_LOG_LEVEL(debug);
    IMPLEMENT_LOG_LEVEL(verbose);
    IMPLEMENT_LOG_LEVEL(info);
    IMPLEMENT_LOG_LEVEL(error);
    IMPLEMENT_LOG_LEVEL(fatal);
#undef IMPLEMENT_LOG_LEVEL
}

#pragma once

#include <filesystem>
#include <optional>

namespace app
{
    struct arguments
    {
        std::filesystem::path exe;
        int verbosity = 0;
        bool dry_run = false;
        bool print_output = false;
        bool validate_output = false;
        std::filesystem::path input_path;
        std::filesystem::path output_path;
    };

    extern const arguments & args;

    std::optional<int> parse_args(int argc, char ** argv);
}

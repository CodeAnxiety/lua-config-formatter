#include "logging.h"
#include "args.h"
#include "formatter.h"

#include <fmt/format.h>
#include <fmt/os.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stack>
#include <string_view>
#include <string>
#include <unordered_set>
#include <tuple>
#include <variant>

using namespace app;
namespace fs = std::filesystem;

namespace
{
    class file_searcher
    {
      private:
        std::string_view m_extension;
        std::vector<fs::path> m_files;
        std::unordered_set<std::string> m_visited;

      public:
        file_searcher(const fs::path & path, std::string_view extension)
            : m_extension(extension)
        {
            visit(path, 0);
        }

        auto begin() const { return m_files.begin(); }
        auto end() const { return m_files.end(); }

        auto empty() const { return m_files.empty(); }
        auto size() const { return m_files.size(); }

      private:
        void visit(const fs::path & path, int depth)
        {
            debug(depth, "Visiting: {}", path);

            if (!fs::exists(path)) {
                debug(depth + 1, "Invalid: {}", path);
                return;
            }

            // ensure we only visit a path once
            {
                std::string canonical = fs::canonical(path).string();
                if (m_visited.contains(canonical))
                    return;
                m_visited.emplace(std::move(canonical));
            }

            if (fs::is_directory(path)) {
                for (const fs::path & child : fs::directory_iterator{path})
                    visit(child, depth + 1);
            }
            else if (path.extension() == m_extension) {
                debug(depth + 1, "Found: {}", path);
                m_files.emplace_back(path);
            }
            else
                debug(depth + 1, "Ignored: {}", path);
        }
    };

    std::optional<std::string> format(const std::filesystem::path & path)
    {
        app::formatter formatter;
        if (!formatter.load(path)) {
            error("Could not load file: {}", path);
            return std::nullopt;
        }

        auto formatted = formatter.render();

        if (args.validate_output) {
            app::formatter round_trip_formatter;
            if (round_trip_formatter.parse(formatted)) {
                std::string round_trip = round_trip_formatter.render();
                if (formatted != round_trip) {
                    error("Format validation failed: {}", path);
                    debug("--- FORMATTED ---");
                    debug("{}", formatted);
                    debug("--- ROUND TRIP ---");
                    debug("{}", round_trip);
                    debug("--- DONE ---");
                    return std::nullopt;
                }
            }
        }

        return formatted;
    }

    bool make_directory(const fs::path & path)
    {
        auto directory = fs::is_directory(path) ? path : path.parent_path();
        if (fs::exists(directory) || fs::create_directories(directory))
            return true;

        error("Could not create directory: {}", directory);
        return false;
    }

    fs::path determine_output(const fs::path & path)
    {
        static bool is_output_directory = fs::is_directory(args.output_path);
        if (!is_output_directory)
            return args.output_path;

        static bool is_input_directory = fs::is_directory(args.input_path);
        if (!is_input_directory)
            return args.output_path / path.filename();

        return args.output_path / fs::relative(path, args.input_path);
    }

    bool save_to_output(const fs::path & path, std::string_view text)
    {
        auto output_path = determine_output(path);

        if (args.print_output) {
            fmt::print("--[[BEGIN: {0}]]\n{1}\n--[[END: {0}]]\n", output_path,
                       text);
        }

        if (args.dry_run) {
            debug("{} -> {}", fs::relative(path, args.input_path), output_path);
            return true;
        }

        if (!make_directory(output_path)) {
            error("Could nto create directory: {}", output_path.parent_path());
            return false;
        }

        std::ofstream stream{output_path};
        if (stream.fail()) {
            error("Could not save file: {}", output_path);
            return false;
        }

        stream << text;
        return true;
    }
}

int main(int argc, char ** argv)
{
    if (!parse_args(argc, argv))
        return 0;

    info("{} v0.0.1-alpha", args.exe.filename());
    debug("arguments:");
    debug("- verbosity:       {}", args.verbosity);
    debug("- dry_run:         {}", args.dry_run ? "true" : "false");
    debug("- print_output:    {}", args.print_output ? "true" : "false");
    debug("- validate_output: {}", args.validate_output ? "true" : "false");
    debug("- input_path:      {}", args.input_path);
    debug("- output_path:     {}", args.output_path);

    if (!std::filesystem::exists(args.input_path)) {
        error("Input path not found: {}", args.input_path);
        return 1;
    }

    file_searcher files{args.input_path, ".lua"};
    if (files.empty()) {
        error("No lua files found for path: {}", args.input_path);
        return 0;
    }

    size_t index = 0;
    double percent_multipier = 100.0 / files.size();
    for (const fs::path & path : files) {
        verbose("[{0:>3.0f}%] {1} of {2}: {3}", ++index * percent_multipier,
                index, files.size(), path);
        if (auto formatted = format(path)) {
            if (save_to_output(path, formatted.value()))
                continue;
        }
        info("Problems encountered, aborted.");
        break;
    }

    info("Done. Formatted {} file(s).", files.size());
    return 0;
}

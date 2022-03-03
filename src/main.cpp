#include <sol/sol.hpp>

#include <fmt/format.h>
#include <fmt/color.h>

#include <lyra/lyra.hpp>
#include <lyra/help.hpp>

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
#include <variant>

static bool QUIET = false;
static bool VERBOSE = false;
static bool DRY_RUN = false;
static bool SHOW_OUTPUT = false;
static std::filesystem::path INPUT_PATH;
static std::filesystem::path OUTPUT_PATH;

#define LOG_INFO(format, ...)                     \
    do {                                          \
        if (!QUIET)                               \
            fmt::print(format "\n", __VA_ARGS__); \
    }                                             \
    while (0)

#define LOG_ERROR(format, ...)                                                            \
    do {                                                                                  \
        fmt::print(stderr, fmt::fg(fmt::color::red), "ERROR: " format "\n", __VA_ARGS__); \
    }                                                                                     \
    while (0)

#define LOG_DEBUG(format, ...)                                               \
    do {                                                                     \
        if (!QUIET && VERBOSE)                                               \
            fmt::print(fmt::fg(fmt::color::gray), format "\n", __VA_ARGS__); \
    }                                                                        \
    while (0)

struct TableEntry
{
    sol::reference key;
    std::string rendered;
};

class LuaFormatter
{
  private:
    sol::state m_lua;
    fmt::memory_buffer m_buffer;
    std::stack<std::optional<double>> m_previous_index;

  public:
    bool load(std::string path)
    {
        auto result = m_lua.do_file(path);
        return result.valid();
    }

    bool parse(std::string_view script)
    {
        auto result = m_lua.do_string(script);
        return result.valid();
    }

    std::string render()
    {
        // disable indexing at root level
        m_previous_index.push(std::nullopt);

        for (const auto & entry : gather_and_sort_table_entries(m_lua.globals())) {
            if (render_table_key(entry.key).starts_with("sol."))
                continue;
            write_entry(entry.key, m_lua[entry.key], 0);
        }

        assert(m_previous_index.size() == 1);
        return {m_buffer.data(), m_buffer.size()};
    }

  private:
    template <typename T>
    void write(T && value)
    {
        fmt::format_to(m_buffer, "{}", value);
    }

    template <>
    void write<std::string_view>(std::string_view && value)
    {
        m_buffer.append(value);
    }

    template <>
    void write<bool>(bool && value)
    {
        write(value ? "true" : "false");
    }

    void write_indent(int depth)
    {
        for (int i = 0; i < depth; i++)
            write("\t");
    }

    void write_escaped(std::string_view text)
    {
        write("\"");
        for (char character : text) {
            switch (character) {
                case '\r': break;
                case '"': write("\\\""); break;
                case '\\': write("\\\\"); break;
                case '\t': write("\\t"); break;
                case '\n': write("\\n"); break;
                default: write(character); break;
            }
        }
        write("\"");
    }

    bool is_identifier(std::string_view text)
    {
        for (char character : text) {
            if (!std::isalnum(character) && character != '_')
                return false;
        }
        return true;
    }

    bool write_key(std::string_view text, bool allow_identifier)
    {
        if (allow_identifier && is_identifier(text))
            write(text);
        else {
            write("[");
            write_escaped(text);
            write("]");
        }

        invalidate_index();
        return true;
    }

    bool write_key(double index)
    {
        if (update_index(index)) {
            return false;
        }

        fmt::format_to(m_buffer, "[{}]", index);
        return true;
    }

    bool write_key(const sol::object & key, bool allow_identifier)
    {
        if (key.is<double>())
            return write_key(key.as<double>());
        else
            return write_key(key.as<std::string>(), allow_identifier);
    }

    void write_table(const sol::table & table, int depth)
    {
        auto starting_size = m_previous_index.size();
        m_previous_index.push(0);

        write("{\n");

        for (const auto & entry : gather_and_sort_table_entries(table))
            write_entry(entry.key, table[entry.key], depth + 1);

        write_indent(depth);
        write("}");

        m_previous_index.pop();
        assert(m_previous_index.size() == starting_size);
    }

    void write_entry(const sol::object & key, const sol::object & value, int depth)
    {
        write_indent(depth);

        if (write_key(key, depth == 0)) {
            write(" = ");
        }

        switch (value.get_type()) {
            case sol::type::nil: write("nil"); break;
            case sol::type::none: write("none"); break;
            case sol::type::boolean: write(value.as<bool>()); break;
            case sol::type::string: write_escaped(value.as<std::string>()); break;
            case sol::type::number: write(value.as<double>()); break;
            case sol::type::table: write_table(value.as<sol::table>(), depth); break;
            default:
                LOG_ERROR("Encountered invalid value type: {}", int(value.get_type()));
                assert(false && "invalid value type");
                break;
        }

        if (depth > 0)
            write(",");

        if (is_indexed()) {
            write(" -- [");
            write(static_cast<int64_t>(key.as<double>()));
            write("]");
        }

        write("\n");
    }

    bool is_indexed() const { return m_previous_index.top().has_value(); }

    void invalidate_index()
    {
        if (m_previous_index.top().has_value()) {
            m_previous_index.pop();
            m_previous_index.push(std::nullopt);
        }
    }

    bool update_index(double index)
    {
        if (m_previous_index.top() == index - 1) {
            m_previous_index.pop();
            m_previous_index.push(index);
            return true;
        }

        invalidate_index();
        return false;
    }

    static std::string render_table_key(const sol::object & key)
    {
        if (key.is<double>())
            return fmt::format("{:020f}", key.as<double>());
        else
            return key.as<std::string>();
    }

    TableEntry build_entry(const sol::object & key) { return {sol::make_reference(m_lua, key), render_table_key(key)}; }

    std::vector<TableEntry> gather_and_sort_table_entries(const sol::table & table)
    {
        if (table.empty()) {
            return {};
        }

        // work around since `table.size()` seems to always return `0`
        size_t entry_count = 0;
        for (const auto & _ : table)
            entry_count++;

        std::vector<TableEntry> entries;
        entries.reserve(entry_count);

        for (const auto & [child_key, child_value] : table)
            entries.emplace_back(build_entry(child_key));

        std::sort(entries.begin(), entries.end(), [](const auto & lhs, const auto & rhs) {
            // lua starts its indexes at 1, so ensure 0 is sorted last
            {
                if (lhs.rendered == "00000000000000000000.0")
                    return false;
                if (rhs.rendered == "00000000000000000000.0")
                    return true;
            }
            return lhs.rendered < rhs.rendered;
        });

        return entries;
    }
};

static bool find_lua_files(const std::filesystem::path & path, std::vector<std::string> & files,
                           std::unordered_set<std::string> & visited)
{
    std::string full_path = std::filesystem::canonical(path).string();

    if (visited.contains(full_path))
        return true;
    visited.emplace(full_path);

    if (!std::filesystem::exists(path)) {
        LOG_ERROR("Input path does not exist: {}", path.string());
        return false;
    }

    if (std::filesystem::is_directory(path)) {
        LOG_DEBUG("Entering: {}", path.string());
        for (const std::filesystem::path & child_path : std::filesystem::directory_iterator{path}) {
            if (!find_lua_files(child_path, files, visited))
                return false;
        }
    }
    else if (path.extension() == ".lua") {
        LOG_DEBUG("Found: {}", path.string());
        files.emplace_back(std::move(full_path));
    }

    return true;
}

static std::optional<std::vector<std::string>> find_lua_files(const std::filesystem::path & path)
{
    std::vector<std::string> files;
    std::unordered_set<std::string> visited;
    if (find_lua_files(path, files, visited))
        return files;
    return std::nullopt;
}

bool format(const std::filesystem::path & path)
{
    LuaFormatter formatter;
    if (!formatter.load(path.string())) {
        LOG_ERROR("Could not load file: {}", path.string());
        return false;
    }

    std::string formatted = formatter.render();

#if true
    LuaFormatter round_trip_formatter;
    if (round_trip_formatter.parse(formatted)) {
        std::string round_trip = round_trip_formatter.render();
        if (formatted != round_trip) {
            LOG_ERROR("Failed to format file properly, round-trip failed: {}", path.string());
            LOG_DEBUG("--- FORMATTED ---");
            LOG_DEBUG("{}", formatted);
            LOG_DEBUG("--- ROUND TRIP ---");
            LOG_DEBUG("{}", round_trip);
            LOG_DEBUG("--- DONE ---");
            return false;
        }
    }
#endif

    auto relative_path = std::filesystem::relative(path, INPUT_PATH);
    auto absolute_path = OUTPUT_PATH / relative_path;

    if (SHOW_OUTPUT) {
        LOG_INFO("");
        LOG_INFO("-- BEGIN: {}", absolute_path.string());
        LOG_INFO("{}", formatted);
        LOG_INFO("");
    }

    if (DRY_RUN) {
        LOG_DEBUG("{} -> {}", relative_path.string(), absolute_path.string());
    }
    else {
        auto parent_path = absolute_path.parent_path();
        if (!std::filesystem::exists(parent_path) && !std::filesystem::create_directories(parent_path)) {
            LOG_ERROR("Could not create directory tree: {}", parent_path.string());
            return false;
        }

        std::ofstream stream{absolute_path};
        if (stream.fail()) {
            LOG_ERROR("Could not save file: {}", absolute_path.string());
            return false;
        }

        stream << formatted;
    }

    return true;
}

std::optional<std::filesystem::path> get_canonical_directory(std::filesystem::path path, bool should_create)
{
    if (!std::filesystem::is_directory(path))
        path = path.parent_path();

    if (std::filesystem::exists(path))
        return std::filesystem::canonical(path);

    if (should_create && std::filesystem::create_directories(path))
        return std::filesystem::canonical(path);

    return std::nullopt;
}

int main(int argc, const char ** argv)
{
    bool usage_shown = false;
    std::string input_path_raw, output_path_raw;
    auto cli = lyra::cli() | lyra::help(usage_shown) | lyra::opt(VERBOSE)["-v", "--verbose"]("Print verbose messages.")
             | lyra::opt(QUIET)["-q", "--quiet"]("Suppress messages.")
             | lyra::opt(DRY_RUN)["--dry-run"]("Do not save the formatted files.")
             | lyra::opt(SHOW_OUTPUT)["--show"]("Output formatting result.")
             | lyra::arg(input_path_raw, "input_path")("Path to be formatted.")
             | lyra::arg(output_path_raw, "output_path")("Path to save changes.");

    auto result = cli.parse({argc, argv});
    if (!result) {
        std::cerr << result.message() << "\n";
        std::cout << "\n";
        std::cout << cli;
        return 1;
    }

    if (usage_shown) {
        std::cout << cli;
        return 0;
    }

    if (auto input_path = get_canonical_directory(input_path_raw, false))
        INPUT_PATH = input_path.value();
    else {
        LOG_ERROR("Input path not found: {}", input_path_raw);
        return 1;
    }

    if (output_path_raw.empty()) {
        output_path_raw = input_path_raw;
    }
    if (auto output_path = get_canonical_directory(output_path_raw, true))
        OUTPUT_PATH = output_path.value();
    else {
        LOG_ERROR("Output path not found: {}", output_path_raw);
        return 1;
    }

    if (auto files = find_lua_files(input_path_raw)) {
        if (files.value().empty()) {
            LOG_ERROR("No files lua found at path: {}", input_path_raw);
            return 0;
        }

        size_t index = 0;
        double percent_multipier = 100 / static_cast<double>(files.value().size());
        for (const std::string & path : files.value()) {
            LOG_DEBUG("[{0:0.0f}%] {1} of {2}: {3}", ++index * percent_multipier, index, files.value().size(), path);
            if (!format(path)) {
                return 1;
            }
        }

        LOG_INFO("Done, formatted {} files.", files.value().size());
    }
    else {
        LOG_INFO("Done, no lua files found.");
    }

    return 0;
}

#include "formatter.h"
#include "logging.h"

#include <sol/sol.hpp>

#include <unordered_set>

using namespace app;

namespace
{
    const std::unordered_set<std::string_view> s_keywords = {
        "and", "break",    "do",     "else", "elseif", "end",   "false",
        "for", "function", "if",     "in",   "local",  "nil",   "not",
        "or",  "repeat",   "return", "then", "true",   "until", "while",
    };

    bool is_keyword(std::string_view text)
    {
        return s_keywords.find(text) != s_keywords.end();
    }

    bool is_identifier(std::string_view text)
    {
        if (text.empty())
            return false;

        // leading character must be a letter or underscore
        if (!std::isalpha(text[0]) && text[0] != '_')
            return false;

        if (is_keyword(text))
            return false;

        text.remove_prefix(1);
        for (char character : text) {
            if (!std::isalnum(character) && character != '_')
                return false;
        }

        return true;
    }

    std::string render_key(const sol::state & lua, const sol::object & key,
                           bool sortable)
    {
        switch (key.get_type()) {
            case sol::type::number:
                return fmt::format("{:>{}f}", key.as<double>(),
                                   sortable ? 20 : 0);

            case sol::type::string: return key.as<std::string>();

            default:
                fatal("Encountered unsupported key type: {}",
                      sol::type_name(lua, key.get_type()));
                return {};
        }
    };

    class sorted_table_keys
    {
      private:
        std::vector<sol::reference> m_keys;

      public:
        sorted_table_keys(sol::state & lua, const sol::table & table,
                          bool is_root)
        {
            if (table.empty()) {
                return;
            }

            bool is_indexed = !is_root;
            size_t entry_count = 0;
            for (const auto & [key, _] : table) {
                is_indexed &= key.is<double>();
                entry_count++;
            }

            using entry = std::pair<sol::reference, std::string>;
            std::vector<entry> entries;
            entries.reserve(entry_count);

            for (const auto & [key, _] : table) {
                auto rendered = render_key(lua, key, is_indexed);
                if (is_root && rendered.starts_with("sol."))
                    continue;

                entries.emplace_back(sol::make_reference(lua, key),
                                     std::move(rendered));
            }

            if (is_indexed) {
                std::sort(entries.begin(), entries.end(),
                          [](const auto & lhs, const auto & rhs) {
                              // lua starts at index 1, ensure 0 is last
                              {
                                  if (lhs.second.ends_with(" 0.0"))
                                      return false;
                                  if (rhs.second.ends_with(" 0.0"))
                                      return true;
                              }
                              return lhs.second < rhs.second;
                          });
            }
            else {
                std::sort(entries.begin(), entries.end(),
                          [](const auto & lhs, const auto & rhs) {
                              return lhs.second < rhs.second;
                          });
            }

            m_keys.reserve(entries.size());
            for (const auto & [key, _] : entries)
                m_keys.emplace_back(key);
        }

        auto begin() { return m_keys.begin(); }
        auto end() { return m_keys.end(); }

        auto begin() const { return m_keys.begin(); }
        auto end() const { return m_keys.end(); }
    };
}

formatter::formatter()
{
    m_lua.set_exception_handler([](lua_State *,
                                   sol::optional<const std::exception &>,
                                   std::string_view message) {
        error("Lua exception: {}", message);
        return 1;
    });
}

bool formatter::load(const std::filesystem::path & path)
{
    auto result = m_lua.do_file(path.string());
    if (result.valid())
        return true;

    std::string reason;
    auto status = sol::to_string(result.status());
    if (sol::type_of(m_lua, result.stack_index()) == sol::type::string) {
        reason = sol::stack::unqualified_get<std::string_view>(
            m_lua, result.stack_index());
    }
    else
        reason = path.string();

    fatal("Failed to process, lua {} error:\n{}", status, reason);
    return false;
}

bool formatter::parse(std::string_view script)
{
    auto result = m_lua.do_string(script);
    return result.valid();
}

std::string formatter::render()
{
    write_table(m_lua.globals(), 0);
    return {m_buffer.data(), m_buffer.size()};
}

void formatter::write_indent(int depth)
{
    for (int i = 0; i < depth; i++)
        write("  ");
}

void formatter::write_escaped(std::string_view text)
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

bool formatter::write_key(const sol::object & key)
{
    switch (key.get_type()) {
        case sol::type::number: return write_key(key.as<double>());
        case sol::type::string: return write_key(key.as<std::string>());
        default:
            fatal("Encountered unsupported key type: {}",
                  sol::type_name(m_lua, key.get_type()));
            return false;
    }
}

bool formatter::write_key(std::string_view text)
{
    if (is_identifier(text))
        write(text);
    else {
        write("[");
        write_escaped(text);
        write("]");
    }

    invalidate_index();
    return true;
}

bool formatter::write_key(double index)
{
    if (update_index(index)) {
        return false;
    }

    fmt::format_to(m_buffer, "[{}]", index);
    return true;
}

void formatter::write_table(const sol::table & table, int depth)
{
    if (table.empty())
        return write("{}");

    auto starting_size = m_previous_index.size();
    if (depth == 0)
        m_previous_index.push(std::nullopt);  // disabling indexes at the root
    else
        m_previous_index.push(0);

    if (depth > 0)
        write("{\n");

    for (const auto & key : sorted_table_keys(m_lua, table, depth == 0))
        write_table_entry(key, table[key], depth);

    if (depth > 0) {
        write_indent(depth - 1);
        write("}");
    }

    m_previous_index.pop();
    assert(m_previous_index.size() == starting_size);
}

void formatter::write_table_entry(const sol::object & key,
                                  const sol::object & value, int depth)
{
    write_indent(depth);

    if (write_key(key)) {
        write(" = ");
    }

    switch (value.get_type()) {
        case sol::type::nil: write("nil"); break;
        case sol::type::none: write("none"); break;
        case sol::type::boolean: write(value.as<bool>()); break;
        case sol::type::string: write_escaped(value.as<std::string>()); break;
        case sol::type::number: write(value.as<double>()); break;
        case sol::type::table:
            write_table(value.as<sol::table>(), depth + 1);
            break;
        default:
            fatal("Encountered unsupported value type: {}",
                  sol::type_name(m_lua, value.get_type()));
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

bool formatter::is_indexed() const
{
    return m_previous_index.top().has_value();
}

void formatter::invalidate_index()
{
    if (m_previous_index.top().has_value()) {
        m_previous_index.pop();
        m_previous_index.push(std::nullopt);
    }
}

bool formatter::update_index(double index)
{
    if (m_previous_index.top() == index - 1) {
        m_previous_index.pop();
        m_previous_index.push(index);
        return true;
    }

    invalidate_index();
    return false;
}

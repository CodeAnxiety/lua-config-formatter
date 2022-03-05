#pragma once

#include <filesystem>
#include <optional>
#include <stack>

#include <sol/sol.hpp>

#include <fmt/format.h>

namespace app
{
    class formatter
    {
      private:
        struct table_entry
        {
            sol::reference key;
            sol::reference value;
            std::string rendered;
        };

        sol::state m_lua;
        fmt::memory_buffer m_buffer;
        std::stack<std::optional<double>> m_previous_index;

      public:
        formatter();

        [[nodiscard]] bool load(const std::filesystem::path & path);
        [[nodiscard]] bool parse(std::string_view script);
        [[nodiscard]] std::string render();

      private:
        template <typename T>
        void write(T && value)
        {
            fmt::format_to(m_buffer, "{}", value);
        }

        void write(std::string_view value) { m_buffer.append(value); }
        void write(bool value) { write(value ? "true" : "false"); }
        void write_indent(int depth);
        void write_escaped(std::string_view text);

        bool write_key(const sol::object & key);
        bool write_key(double index);
        bool write_key(std::string_view text);

        void write_table(const sol::table & table, int depth);
        void write_table_entry(const sol::object & key,
                               const sol::object & value, int depth);

        bool is_indexed() const;
        void invalidate_index();
        bool update_index(double index);
    };
}

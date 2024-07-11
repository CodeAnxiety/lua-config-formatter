add_requires("sol2", "fmt", "dimcli")
add_rules("mode.debug", "mode.release")
set_languages("c++20")

target("lua-config-formatter")
    set_kind("binary")
    add_files("src/*.cpp")
    add_headerfiles("src/*.h")
    add_packages("sol2", "fmt", "dimcli")

add_requires("sol2 >=3.2.3", "fmt >=8.1.1", "lyra >=1.6")
add_rules("mode.debug", "mode.release")
set_languages("c++20")

target("wow-savedvar-sorter")
    set_kind("binary")
    add_files("src/*.cpp")
    add_packages("sol2", "fmt", "lyra")

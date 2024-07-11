#include "args.h"

#include <algorithm>
#include <filesystem>
#include <iostream>

#include <dimcli/cli.h>

namespace fs = std::filesystem;

static app::arguments s_args;
const app::arguments & app::args = s_args;

int calculate_verbosity(const std::vector<bool> & verbose)
{
    int verbosity = 0;
    for (const bool value : verbose) {
        verbosity += value ? +1 : -1;
    }

    return std::clamp(verbosity, -2, 2);
}

std::optional<int> app::parse_args(int argc, char ** argv)
{
    Dim::Cli cli;
    cli.title("Lua Config Formatter");
    auto & verbose =
        cli.optVec<bool>("v !q verbose. !quiet.").desc("Increase verbosity");
    auto & dry_run =
        cli.opt<bool>("n dry-run.").desc("Skip saving the formatted file(s).");
    auto & print_output =
        cli.opt<bool>("print-output.").desc("Print formatted result(s).");
    auto & validate_output = cli.opt<bool>("validate-output.")
                                 .desc("Round-trip validation the result.");
    auto & input_path = cli.group("~")
                            .opt<std::string>("i input <input>")
                            .desc("Path to be formatted.")
                            .require();
    auto & output_path = cli.group("~")
                             .opt<std::string>("o output [output]")
                             .desc("Path to save changes.");

    if (!cli.parse(argc, argv)) {
        return cli.printError(std::cerr);
    }

    s_args.exe = argv[0];
    s_args.verbosity = calculate_verbosity(*verbose);
    s_args.dry_run = *dry_run;
    s_args.print_output = *print_output;
    s_args.validate_output = *validate_output;
    s_args.input_path = *input_path;
    s_args.output_path = *output_path;

    if (s_args.output_path.empty())
        s_args.output_path = s_args.input_path;

    return std::nullopt;
}

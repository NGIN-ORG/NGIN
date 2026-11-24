#pragma once

#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace NGIN::CLI
{
    struct CommandHelp
    {
        std::string_view name, usage, description, example, options;
        bool selection{};
    };

    inline constexpr std::string_view SelectionOptions =
        "--project --workspace --configuration --target --toolchain --run --profile --option";

    inline constexpr CommandHelp Commands[] = {
        {"run", "[--run NAME] [-- ARGS...]", "Build, stage and run an application.", "run --project App.nginproj -- \"two words\"", "--output --launch-plan --no-stage", true},
        {"build", "[--output DIR]", "Build the selected product.", "build --configuration Release", "--output --configure-preset --build-preset", true},
        {"test", "[NAME] [-- ARGS...]", "Run all registrations or one named test.", "test smoke", "--output --launch-plan --no-stage --test-preset --test-name --configure-preset", true},
        {"new", "<executable|library> NAME", "Create a C++ project.", "new executable Hello", ""},
        {"configure", "[--output DIR]", "Generate and configure backend build files.", "configure", "--output --configure-preset", true},
        {"stage", "[--output DIR]", "Build and assemble runtime files.", "stage", "--output", true},
        {"benchmark", "[NAME]", "Run a benchmark registration.", "benchmark", "--output", true},
        {"publish", "[NAME]", "Prepare a publication.", "publish", "--output", true},
        {"validate", "", "Check the authored manifest.", "validate", "", true},
        {"explain", "<FILE|IDENTITY>", "Explain project-relative file membership or graph provenance.", "explain src/main.cpp", "--format", true},
        {"graph", "[--format json]", "Display the resolved Composition Graph.", "graph --format json", "--format", true},
        {"inspect", "[--effective]", "Inspect the project and its selected rules.", "inspect --effective", "--effective --format", true},
        {"diff", "--against PROJECT", "Compare resolved projects.", "diff --against Other.nginproj", "--against --format", true},
        {"format", "[--check]", "Format a manifest or check its formatting.", "format --check", "--check", true},
        {"analyze", "[--file PATH]...", "Run selected analyzer actions.", "analyze --file src/main.cpp", "--file --lock --output --format", true},
        {"tooling format", "[--file PATH]...", "Run selected source formatters.", "tooling format --file src/main.cpp", "--file --lock --output", true},
        {"restore", "", "Resolve package dependencies.", "restore", "", true},
        {"package lock", "[--output FILE]", "Record exact dependency instances.", "package lock", "--output", true},
        {"package verify-lock", "[--lock FILE]", "Verify a dependency lock.", "package verify-lock", "--lock", true},
        {"package add", "NAME", "Add a package requirement.", "package add Demo --exact 1.0.0", "--exact --version --at-least --after --at-most --before --export --option", true},
        {"package update", "NAME", "Update a package requirement.", "package update Demo --version 2.0.0", "--exact --version --at-least --after --at-most --before --export --option", true},
        {"package remove", "NAME", "Remove a direct package requirement.", "package remove Demo", "", true},
        {"add package", "NAME", "Add a package requirement.", "add package Demo", "--exact --version --at-least --after --at-most --before --export --option", true},
        {"add project-reference", "PATH", "Reference another project.", "add project-reference ../Lib/Lib.nginproj", "", true},
        {"add action", "PACKAGE::ACTION --kind KIND", "Select a package action.", "add action Tools::Lint --kind Analyze", "--kind", true},
        {"schema", "--format json", "Print manifest editor metadata.", "schema --format json", "--format"},
        {"editor workspace", "", "Discover workspace projects.", "editor workspace", "--workspace"},
        {"editor snapshot", "", "Inspect a project for an editor.", "editor snapshot --project App.nginproj", "--configure-preset", true},
        {"editor plan", "--intent INTENT", "Preview a manifest authoring operation.", "editor plan --intent IncludeItems --item 'Source|Private|src/main.cpp'", "--intent --item --from --to --package --version --exact --precondition", true},
        {"completion", "bash", "Generate a Bash completion script.", "completion bash", ""},
        {"version", "", "Print the CLI version.", "version", ""},
        {"help", "[COMMAND]", "Show command usage.", "help run", ""}
    };

    inline auto PrintHelp(const std::string &name = {}) -> void
    {
        if (name.empty()) std::cout << "usage: ngin <command> [options]\n\n";
        bool found = false;
        for (const auto &entry : Commands)
        {
            if (entry.name == name)
            {
                std::cout << "usage: ngin " << name << ' ' << entry.usage << "\n" << entry.description
                          << "\n\nExample:\n  ngin " << entry.example << '\n';
                if (entry.selection) std::cout << "\nSelection:\n  --project FILE  --workspace FILE  --configuration NAME  --target NAME\n"
                                                 "  --toolchain NAME  --run NAME  --profile NAME  --option NAME=VALUE\n";
                if (!entry.options.empty()) std::cout << "Options: " << entry.options << '\n';
                return;
            }
            if (name.empty() || entry.name.starts_with(name + " "))
            {
                found = true;
                std::cout << "  " << entry.name << "  " << entry.description << '\n';
            }
        }
        if (!found) throw std::runtime_error("unknown command '" + name + "'");
    }

    inline auto PrintBashCompletion() -> void
    {
        std::cout << "_ngin_complete() {\n  local words='' candidate cur=\"${COMP_WORDS[COMP_CWORD]}\"\n"
                     "  case \"${COMP_WORDS[1]} ${COMP_WORDS[2]-}\" in\n";
        for (const auto &entry : Commands)
        {
            std::cout << "    '" << entry.name << (entry.name.contains(' ') ? "'" : " '*")
                      << ") words='" << entry.options << ' ' << (entry.selection ? SelectionOptions : "") << " --help' ;;\n";
        }
        std::cout << "  esac\n  if (( COMP_CWORD == 1 )); then words='";
        std::set<std::string_view> roots{};
        for (const auto &entry : Commands) roots.insert(entry.name.substr(0, entry.name.find(' ')));
        for (const auto root : roots) std::cout << root << ' ';
        std::cout << "'; elif (( COMP_CWORD == 2 )); then case \"${COMP_WORDS[1]}\" in\n";
        for (const auto group : {"add", "package", "editor", "tooling"})
        {
            std::cout << "    " << group << ") words='";
            for (const auto &entry : Commands)
                if (entry.name.starts_with(std::string(group) + " ")) std::cout << entry.name.substr(entry.name.find(' ') + 1) << ' ';
            std::cout << "' ;;\n";
        }
        std::cout << "    new) words='executable library' ;;\n    completion) words=bash ;;\n  esac; fi\n"
                     "  COMPREPLY=(); while IFS= read -r candidate; do COMPREPLY+=(\"$candidate\"); done < <(compgen -W \"$words\" -- \"$cur\")\n}\ncomplete -o default -F _ngin_complete ngin\n";
    }
}

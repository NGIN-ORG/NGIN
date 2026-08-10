#include "ManifestCli.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef NGIN_CLI_VERSION
#define NGIN_CLI_VERSION "0.0.0-dev"
#endif

namespace
{
    auto Help() -> void
    {
        std::cout << R"(usage: ngin <command> [options]

Selection:
  --project <file.nginproj>   --workspace <file.ngin>
  --configuration <name>     --target <name>     --toolchain <name>
  --preset <name>             --option <Name=Value>

Authoring:
  new <app|lib|tool|test|benchmark|plugin|external> <Name>
  add package <Name> [--exact V|--compatible V|--at-least V --before V]
                     [--use Kind:Name] [--option Name=Value]
  add project-reference <Path>
  add action <Package::Action> --kind <Generate|Analyze|Format|Validate|Custom>
  package add|update|remove <Name> [version and activation options]
  manifest format
  schema --format json

Resolution:
  validate
  inspect --format json
  graph --format json
  explain <graph-identity>
  diff --against <other.nginproj> [--format json]

Execution commands are driven by typed plans: configure, build, stage, run,
test, publish, analyze, format, restore, and package lock.
)";
    }
}

auto main(const int argc, char **argv) -> int
{
    try
    {
        if (argc < 2) { Help(); return 0; }
        const std::string command = argv[1];
        if (command == "--version" || command == "version") { std::cout << "ngin " << NGIN_CLI_VERSION << '\n'; return 0; }
        if (command == "--help" || command == "help") { Help(); return 0; }
        const auto root = std::filesystem::current_path();
        if (command == "new")
        {
            if (argc != 4) throw std::runtime_error("new requires a kind and name");
            return NGIN::CLI::NewProject(root, argv[2], argv[3]);
        }
        if (command == "validate") return NGIN::CLI::ValidateManifest(root, NGIN::CLI::ParseCliArguments(argc, argv, 2));
        if (command == "schema") return NGIN::CLI::PrintSchema(NGIN::CLI::ParseCliArguments(argc, argv, 2));
        if (command == "inspect" || command == "graph")
            return NGIN::CLI::InspectComposition(root, NGIN::CLI::ParseCliArguments(argc, argv, 2));
        if (command == "diff")
            return NGIN::CLI::DiffComposition(root, NGIN::CLI::ParseCliArguments(argc, argv, 2));
        if (command == "explain")
            return NGIN::CLI::ExplainComposition(root, NGIN::CLI::ParseCliArguments(argc, argv, 2));
        if (command == "configure") return NGIN::CLI::ConfigureProject(root, NGIN::CLI::ParseCliArguments(argc, argv, 2));
        if (command == "build") return NGIN::CLI::BuildProject(root, NGIN::CLI::ParseCliArguments(argc, argv, 2));
        if (command == "stage") return NGIN::CLI::StageProject(root, NGIN::CLI::ParseCliArguments(argc, argv, 2));
        if (command == "run") return NGIN::CLI::RunProject(root, NGIN::CLI::ParseCliArguments(argc, argv, 2));
        if (command == "test") return NGIN::CLI::TestProject(root, NGIN::CLI::ParseCliArguments(argc, argv, 2));
        if (command == "publish") return NGIN::CLI::PublishProject(root, NGIN::CLI::ParseCliArguments(argc, argv, 2));
        if (command == "analyze" || command == "format")
            return NGIN::CLI::ExecuteProjectActions(root, NGIN::CLI::ParseCliArguments(argc, argv, 2), command);
        if (command == "restore") return NGIN::CLI::RestorePackages(root, NGIN::CLI::ParseCliArguments(argc, argv, 2));
        if (command == "manifest")
        {
            if (argc < 3 || std::string{argv[2]} != "format") throw std::runtime_error("manifest requires format");
            return NGIN::CLI::FormatManifest(root, NGIN::CLI::ParseCliArguments(argc, argv, 3));
        }
        if (command == "package")
        {
            if (argc >= 3 && std::string{argv[2]} == "lock")
                return NGIN::CLI::WriteDependencyLock(root, NGIN::CLI::ParseCliArguments(argc, argv, 3));
            if (argc >= 3 && std::string{argv[2]} == "verify-lock")
                return NGIN::CLI::VerifyDependencyLockFile(root, NGIN::CLI::ParseCliArguments(argc, argv, 3));
            if (argc < 4) throw std::runtime_error("package requires add, update, or remove and a name");
            const std::string operation = argv[2];
            auto arguments = NGIN::CLI::ParseCliArguments(argc, argv, 4);
            arguments.packageName = argv[3];
            if (operation == "add") return NGIN::CLI::AddPackage(root, arguments);
            if (operation == "update") return NGIN::CLI::UpdatePackage(root, arguments);
            if (operation == "remove") return NGIN::CLI::RemovePackage(root, arguments);
            throw std::runtime_error("unknown package operation '" + operation + "'");
        }
        if (command == "add")
        {
            if (argc < 4) throw std::runtime_error("add requires a kind and value");
            const std::string kind = argv[2];
            auto arguments = NGIN::CLI::ParseCliArguments(argc, argv, 4);
            if (kind == "package") { arguments.packageName = argv[3]; return NGIN::CLI::AddPackage(root, arguments); }
            arguments.positional.insert(arguments.positional.begin(), argv[3]);
            if (kind == "project-reference") return NGIN::CLI::AddProjectReference(root, arguments);
            if (kind == "action") return NGIN::CLI::AddAction(root, arguments);
            throw std::runtime_error("unknown add kind '" + kind + "'");
        }
        throw std::runtime_error("unknown command '" + command + "'");
    }
    catch (const std::exception &error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}

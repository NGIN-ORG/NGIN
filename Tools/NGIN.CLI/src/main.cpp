#include "Authoring.hpp"
#include "Commands.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef NGIN_CLI_VERSION
#define NGIN_CLI_VERSION "0.0.0-dev"
#endif

namespace
{
    auto PrintHelp() -> void
    {
        std::cout
            << "usage: ngin <command> [options]\n\n"
            << "Version:\n"
            << "  ngin --version\n\n"
            << "Output options:\n"
            << "  --quiet | -q\n"
            << "  --verbose | -v\n"
            << "  --trace\n"
            << "  --plain\n"
            << "  --color auto|always|never\n"
            << "  --ui auto|pretty|compact|plain|json\n"
            << "  --events none|jsonl\n"
            << "  --backend-output stream|compact|silent\n"
            << "  --output-root <dir>  (appends <project>/<profile>)\n"
            << "  --json  (alias for --format json where supported)\n\n"
            << "Project selection options:\n"
            << "  --profile <name>\n\n"
            << "Commands:\n"
            << "  workspace list\n"
            << "  workspace status\n"
            << "  workspace doctor\n"
            << "  new <app|lib|tool|test|benchmark|plugin> <Name>\n"
            << "  settings init [--project <file.nginproj>]\n"
            << "  variables explain [--project <file.nginproj>] [--profile <name>]\n"
            << "  crypto info\n"
            << "  crypto explain --algorithm <name> [--project <file.nginproj>] [--profile <name>] [--format json]\n"
            << "  explain condition <Name> [--project <file.nginproj>] [--profile <name>]\n"
            << "  explain package-feature <PackageName> <FeatureName> [--project <file.nginproj>] [--profile <name>]\n"
            << "  explain generator <Name> [--project <file.nginproj>] [--profile <name>]\n"
            << "  explain <kind>:<identity> [--project <file.nginproj>] [--profile <name>]\n"
            << "  inspect [--project <file.nginproj>] [--profile <name>] [--output <dir>] --format json\n"
            << "  validate [--project <file.nginproj>] [--profile <name>]\n"
            << "  restore [--project <file.nginproj>] [--profile <name>] [--output <package-store-dir>] [--locked]\n"
            << "  add package <PackageName> --version <range> [--scope <scope>] [--project <file.nginproj>]\n"
            << "  add project-reference <Path> [--project <file.nginproj>]\n"
            << "  add tool-action <Package::Action> [--run <RunName>] [--project <file.nginproj>]\n"
            << "  graph [--project <file.nginproj>] [--profile <name>] [--format json|--build-plan|--stage-plan|--package-plan|--package-output-plan|--launch-plan|--runtime-plan|--environment-plan|--publish-plan|--tooling-plan]\n"
            << "  diff [--project <file.nginproj>] --from-profile <name> --to-profile <name>\n"
            << "  diff --from-lock <old-ngin.lock> --to-lock <new-ngin.lock>\n"
            << "  manifest format [--project <file.nginproj|file.ngin|file.nginpkg>]\n"
            << "  schema [--format json]\n"
            << "  configure [--project <file.nginproj>] [--profile <name>] [--output <dir>]\n"
            << "  clean [--project <file.nginproj>] [--profile <name>] [--output <dir>]\n"
            << "  build [--project <file.nginproj>] [--profile <name>] [--output <dir>]\n"
            << "  stage [--project <file.nginproj>] [--profile <name>] [--output <dir>]\n"
            << "  rebuild [--project <file.nginproj>] [--profile <name>] [--output <dir>]\n"
            << "  run [--project <file.nginproj>] [--profile <name>] [--output <dir>] [-- <args...>]\n"
            << "  test [--project <file.nginproj>] [--profile <name>] [--output <dir>] [-- <args...>]\n"
            << "  benchmark [--project <file.nginproj>] [--profile <name>] [--output <dir>] [-- <args...>]\n"
            << "  analyze [--project <file.nginproj>] [--profile <name>] [--file <path>|--changed-since <revision>] [--jobs <n>] [--no-cache] [--no-configure] [--fix-preview|--apply-fixes]\n"
            << "  format [--project <file.nginproj>] [--profile <name>] [--file <path>|--changed-since <revision>] [--check|--apply] [--jobs <n>] [--allow-unsafe]\n"
            << "  scan [--project <file.nginproj>] [--profile <name>]\n"
            << "  report [--project <file.nginproj>] [--profile <name>]\n"
            << "  quality [--project <file.nginproj>] [--profile <name>]\n"
            << "  quality baseline create|update|verify --run <RunName> [--output <file>]\n"
            << "  tool list [--available] [--project <file.nginproj>] [--profile <name>] [--format json]\n"
            << "  tool plan [--project <file.nginproj>] [--profile <name>] [--format json]\n"
            << "  tool doctor [--run <RunName>] [--project <file.nginproj>] [--profile <name>] [--format json]\n"
            << "  tool run <RunName> [--project <file.nginproj>] [--profile <name>]\n"
            << "  tool results [RunId] [--run <RunName>] [--project <file.nginproj>] [--profile <name>] [--format json]\n"
            << "  tool edits [RunId] --run <RunName> [--project <file.nginproj>] [--profile <name>] [--format json]\n"
            << "  publish [PublishName] [--project <file.nginproj>] [--profile <name>] [--output <build-dir>]\n"
            << "  package list\n"
            << "  package add <PackageName> --version <range> [--scope <scope>] [--project <file.nginproj>]\n"
            << "  package remove <PackageName> [--project <file.nginproj>]\n"
            << "  package update <PackageName> --version <range> [--scope <scope>] [--project <file.nginproj>]\n"
            << "  package pack [PackageOutputName] [--project <file.nginproj>] [--output <dir|file.nginpkg|file.nginpack>]\n"
            << "  package sources list\n"
            << "  package sources add <Name> <PathOrUrl>\n"
            << "  package sources remove <Name>\n"
            << "  package show <PackageName>\n"
            << "  package lock [--project <file.nginproj>] [--profile <name>] [--output <ngin.lock>]\n"
            << "  package verify-lock [--project <file.nginproj>] [--profile <name>] [--lock <ngin.lock>]\n";
    }
}

auto main(int argc, char **argv) -> int
{
    std::string command{};
    try
    {
        const auto root = NGIN::CLI::RootDir(argv[0]);
        if (argc < 2)
        {
            PrintHelp();
            return 0;
        }

        command = argv[1];
        if (command == "--version" || command == "version")
        {
            std::cout << "ngin " << NGIN_CLI_VERSION << "\n";
            return 0;
        }
        if (command == "new")
        {
            if (argc < 4)
            {
                throw std::runtime_error("new requires a project kind and name");
            }
            return NGIN::CLI::CmdNew(root, argv[2], argv[3]);
        }
        if (command == "workspace")
        {
            if (argc < 3)
            {
                throw std::runtime_error("workspace requires a subcommand");
            }
            const std::string subcommand = argv[2];
            if (subcommand == "list")
            {
                return NGIN::CLI::CmdList(root);
            }
            if (subcommand == "status")
            {
                return NGIN::CLI::CmdStatus(root, NGIN::CLI::ParseCommonArgs(argc, argv, 3));
            }
            if (subcommand == "doctor")
            {
                return NGIN::CLI::CmdDoctor(root, NGIN::CLI::ParseCommonArgs(argc, argv, 3));
            }
            throw std::runtime_error("unknown workspace subcommand '" + subcommand + "'");
        }
        if (command == "package")
        {
            if (argc < 3)
            {
                throw std::runtime_error("package requires a subcommand");
            }
            const std::string subcommand = argv[2];
            if (subcommand == "list")
            {
                return NGIN::CLI::CmdPackageList(root);
            }
            if (subcommand == "show")
            {
                return NGIN::CLI::CmdPackageShow(root, NGIN::CLI::ParseCommonArgs(argc, argv, 3));
            }
            if (subcommand == "add")
            {
                return NGIN::CLI::CmdPackageAdd(root, NGIN::CLI::ParseCommonArgs(argc, argv, 3));
            }
            if (subcommand == "remove")
            {
                return NGIN::CLI::CmdPackageRemove(root, NGIN::CLI::ParseCommonArgs(argc, argv, 3));
            }
            if (subcommand == "update")
            {
                return NGIN::CLI::CmdPackageUpdate(root, NGIN::CLI::ParseCommonArgs(argc, argv, 3));
            }
            if (subcommand == "pack")
            {
                return NGIN::CLI::CmdPackagePack(root, NGIN::CLI::ParseCommonArgs(argc, argv, 3));
            }
            if (subcommand == "sources")
            {
                if (argc < 4)
                {
                    throw std::runtime_error("package sources requires a subcommand");
                }
                const std::string sourceSubcommand = argv[3];
                if (sourceSubcommand == "list")
                {
                    return NGIN::CLI::CmdPackageSourcesList(root, NGIN::CLI::ParseCommonArgs(argc, argv, 4));
                }
                if (sourceSubcommand == "add")
                {
                    return NGIN::CLI::CmdPackageSourcesAdd(root, NGIN::CLI::ParseCommonArgs(argc, argv, 4));
                }
                if (sourceSubcommand == "remove")
                {
                    return NGIN::CLI::CmdPackageSourcesRemove(root, NGIN::CLI::ParseCommonArgs(argc, argv, 4));
                }
                throw std::runtime_error("unknown package sources subcommand '" + sourceSubcommand + "'");
            }
            if (subcommand == "lock")
            {
                return NGIN::CLI::CmdPackageLock(root, NGIN::CLI::ParseCommonArgs(argc, argv, 3));
            }
            if (subcommand == "verify-lock")
            {
                return NGIN::CLI::CmdPackageVerifyLock(root, NGIN::CLI::ParseCommonArgs(argc, argv, 3));
            }
            throw std::runtime_error("unknown package subcommand '" + subcommand + "'");
        }
        if (command == "add")
        {
            if (argc < 4)
            {
                throw std::runtime_error("add requires a subcommand and value");
            }
            auto args = NGIN::CLI::ParseCommonArgs(argc, argv, 2);
            if (!args.packageName.has_value())
            {
                throw std::runtime_error("add requires a subcommand");
            }
            const auto subcommand = *args.packageName;
            if (subcommand == "package")
            {
                if (!args.featureName.has_value())
                {
                    throw std::runtime_error("add package requires a package name");
                }
                args.packageName = args.featureName;
                args.featureName.reset();
                return NGIN::CLI::CmdPackageAdd(root, args);
            }
            if (subcommand == "project-reference")
            {
                if (!args.featureName.has_value())
                {
                    throw std::runtime_error("add project-reference requires a project path");
                }
                args.packageName = args.featureName;
                args.featureName.reset();
                return NGIN::CLI::CmdProjectReferenceAdd(root, args);
            }
            if (subcommand == "tool-action")
            {
                if (!args.featureName.has_value())
                {
                    throw std::runtime_error("add tool-action requires Package::Action");
                }
                args.packageName = args.featureName;
                args.featureName.reset();
                return NGIN::CLI::CmdToolActionAdd(root, args);
            }
            throw std::runtime_error("unknown add subcommand '" + subcommand + "'");
        }
        if (command == "settings")
        {
            if (argc < 3)
            {
                throw std::runtime_error("settings requires a subcommand");
            }
            const std::string subcommand = argv[2];
            if (subcommand == "init")
            {
                return NGIN::CLI::CmdSettingsInit(root, NGIN::CLI::ParseCommonArgs(argc, argv, 3));
            }
            throw std::runtime_error("unknown settings subcommand '" + subcommand + "'");
        }
        if (command == "variables")
        {
            if (argc < 3)
            {
                throw std::runtime_error("variables requires a subcommand");
            }
            const std::string subcommand = argv[2];
            if (subcommand == "explain")
            {
                return NGIN::CLI::CmdVariablesExplain(root, NGIN::CLI::ParseCommonArgs(argc, argv, 3));
            }
            throw std::runtime_error("unknown variables subcommand '" + subcommand + "'");
        }
        if (command == "crypto")
        {
            if (argc < 3)
            {
                throw std::runtime_error("crypto requires a subcommand");
            }
            const std::string subcommand = argv[2];
            if (subcommand == "info")
            {
                return NGIN::CLI::CmdCryptoInfo(root, NGIN::CLI::ParseCommonArgs(argc, argv, 3));
            }
            if (subcommand == "explain")
            {
                return NGIN::CLI::CmdCryptoExplain(root, NGIN::CLI::ParseCommonArgs(argc, argv, 3));
            }
            throw std::runtime_error("unknown crypto subcommand '" + subcommand + "'");
        }
        if (command == "explain")
        {
            if (argc < 3)
            {
                throw std::runtime_error("explain requires a subcommand");
            }
            const std::string subcommand = argv[2];
            if (subcommand == "condition")
            {
                return NGIN::CLI::CmdExplainCondition(root, NGIN::CLI::ParseCommonArgs(argc, argv, 3));
            }
            if (subcommand == "package-feature")
            {
                return NGIN::CLI::CmdExplainPackageFeature(root, NGIN::CLI::ParseCommonArgs(argc, argv, 3));
            }
            if (subcommand == "generator")
            {
                return NGIN::CLI::CmdExplainGenerator(root, NGIN::CLI::ParseCommonArgs(argc, argv, 3));
            }
            if (subcommand.find(':') != std::string::npos)
            {
                return NGIN::CLI::CmdExplainObject(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
            }
            throw std::runtime_error("unknown explain subcommand '" + subcommand + "'");
        }
        if (command == "list")
        {
            return NGIN::CLI::CmdList(root);
        }
        if (command == "status")
        {
            return NGIN::CLI::CmdStatus(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }
        if (command == "doctor")
        {
            return NGIN::CLI::CmdDoctor(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }
        if (command == "validate")
        {
            return NGIN::CLI::CmdValidate(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }
        if (command == "restore")
        {
            return NGIN::CLI::CmdRestore(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }
        if (command == "inspect")
        {
            return NGIN::CLI::CmdInspect(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }
        if (command == "graph")
        {
            return NGIN::CLI::CmdGraph(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }
        if (command == "diff")
        {
            return NGIN::CLI::CmdDiff(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }
        if (command == "manifest")
        {
            if (argc < 3 || std::string(argv[2]) != "format")
            {
                throw std::runtime_error("manifest requires the format subcommand");
            }
            return NGIN::CLI::CmdManifestFormat(root, NGIN::CLI::ParseCommonArgs(argc, argv, 3));
        }
        if (command == "schema")
        {
            return NGIN::CLI::CmdSchema(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }
        if (command == "clean")
        {
            return NGIN::CLI::CmdClean(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }
        if (command == "configure")
        {
            return NGIN::CLI::CmdConfigure(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }
        if (command == "build")
        {
            return NGIN::CLI::CmdBuild(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }
        if (command == "stage")
        {
            return NGIN::CLI::CmdStage(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }
        if (command == "rebuild")
        {
            return NGIN::CLI::CmdRebuild(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }
        if (command == "run")
        {
            return NGIN::CLI::CmdRun(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }
        if (command == "test")
        {
            return NGIN::CLI::CmdTest(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }
        if (command == "benchmark")
        {
            return NGIN::CLI::CmdBenchmark(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }
        if (command == "analyze")
        {
            auto args = NGIN::CLI::ParseCommonArgs(argc, argv, 2);
            args.toolActionKind = "Analyze";
            args.toolCommandName = "analyze";
            return NGIN::CLI::CmdAnalyze(root, args);
        }
        if (command == "format" || command == "scan" || command == "report" || command == "quality")
        {
            if (command == "quality" && argc >= 4 && std::string(argv[2]) == "baseline")
            {
                auto args = NGIN::CLI::ParseCommonArgs(argc, argv, 4);
                if (!args.packageName.has_value() ||
                    (*args.packageName != "create" && *args.packageName != "update" &&
                     *args.packageName != "verify"))
                    throw std::runtime_error("quality baseline expects create, update, or verify");
                if (!args.toolRunName.has_value())
                    throw std::runtime_error("quality baseline requires --run <RunName>");
                args.toolBaselineOperation = args.packageName;
                args.packageName.reset();
                args.toolBaselinePath = args.outputPath;
                args.outputPath.reset();
                args.toolCommandName = "quality baseline";
                return NGIN::CLI::CmdAnalyze(root, args);
            }
            auto args = NGIN::CLI::ParseCommonArgs(argc, argv, 2);
            args.toolCommandName = command;
            if (command == "format")
            {
                args.toolActionKind = "Format";
                if (!args.toolEditMode.has_value()) args.toolEditMode = "check";
            }
            else if (command == "scan") args.toolActionKind = "Scan";
            else if (command == "report") args.toolActionKind = "Report";
            else args.toolOnlyGated = true;
            return NGIN::CLI::CmdAnalyze(root, args);
        }
        if (command == "tool")
        {
            if (argc < 3)
            {
                throw std::runtime_error("tool requires list, plan, doctor, run, results, or edits");
            }
            const std::string subcommand = argv[2];
            auto args = NGIN::CLI::ParseCommonArgs(argc, argv, 3);
            if (subcommand == "list")
            {
                return NGIN::CLI::CmdToolList(root, args);
            }
            if (subcommand == "plan")
            {
                args.graphPlan = "tooling";
                return NGIN::CLI::CmdGraph(root, args);
            }
            if (subcommand == "doctor")
            {
                return NGIN::CLI::CmdToolDoctor(root, args);
            }
            if (subcommand == "run")
            {
                if (!args.packageName.has_value())
                {
                    throw std::runtime_error("tool run requires a run name");
                }
                args.toolRunName = args.packageName;
                args.packageName.reset();
                args.toolCommandName = "tool run";
                return NGIN::CLI::CmdToolRun(root, args);
            }
            if (subcommand == "results")
            {
                return NGIN::CLI::CmdToolResults(root, args);
            }
            if (subcommand == "edits")
            {
                if (!args.toolRunName.has_value() && !args.packageName.has_value())
                {
                    throw std::runtime_error("tool edits requires --run <RunName> or a run id");
                }
                return NGIN::CLI::CmdToolEdits(root, args);
            }
            throw std::runtime_error("unknown tool subcommand '" + subcommand + "'");
        }
        if (command == "publish")
        {
            return NGIN::CLI::CmdPublish(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }

        throw std::runtime_error("unknown command '" + command + "'");
    }
    catch (const std::exception &ex)
    {
        std::cerr << "error: " << ex.what() << "\n";
        if (command == "tool" || command == "analyze" || command == "format" ||
            command == "scan" || command == "report" || command == "quality")
            return 2;
        return 1;
    }
}

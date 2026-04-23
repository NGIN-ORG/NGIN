#include "Authoring.hpp"
#include "Commands.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    auto PrintHelp() -> void
    {
        std::cout
            << "usage: ngin <command> [options]\n\n"
            << "Commands:\n"
            << "  workspace list\n"
            << "  workspace status\n"
            << "  workspace doctor\n"
            << "  validate [--project <file.nginproj>] [--configuration <name>]\n"
            << "  graph [--project <file.nginproj>] [--configuration <name>]\n"
            << "  clean [--project <file.nginproj>] [--configuration <name>] [--output <dir>]\n"
            << "  build [--project <file.nginproj>] [--configuration <name>] [--output <dir>]\n"
            << "  rebuild [--project <file.nginproj>] [--configuration <name>] [--output <dir>]\n"
            << "  run [--project <file.nginproj>] [--configuration <name>] [--output <dir>] [-- <args...>]\n"
            << "  package list\n"
            << "  package show <PackageName>\n";
    }
}

auto main(int argc, char **argv) -> int
{
    try
    {
        const auto root = NGIN::CLI::RootDir(argv[0]);
        if (argc < 2)
        {
            PrintHelp();
            return 0;
        }

        const std::string command = argv[1];
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
            throw std::runtime_error("unknown package subcommand '" + subcommand + "'");
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
        if (command == "graph")
        {
            return NGIN::CLI::CmdGraph(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }
        if (command == "clean")
        {
            return NGIN::CLI::CmdClean(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }
        if (command == "build")
        {
            return NGIN::CLI::CmdBuild(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }
        if (command == "rebuild")
        {
            return NGIN::CLI::CmdRebuild(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }
        if (command == "run")
        {
            return NGIN::CLI::CmdRun(root, NGIN::CLI::ParseCommonArgs(argc, argv, 2));
        }

        throw std::runtime_error("unknown command '" + command + "'");
    }
    catch (const std::exception &ex)
    {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}

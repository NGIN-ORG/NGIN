#include "ManifestCli.hpp"
#include "Canonical.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>

namespace NGIN::CLI
{
    namespace
    {
        auto LayoutRoot(const std::string &root) -> std::string
        {
            if (root == ".")
                return ".";
            static const std::regex portable{R"([A-Za-z0-9_-][A-Za-z0-9_. -]*(/[A-Za-z0-9_-][A-Za-z0-9_. -]*)*)"};
            if (!std::regex_match(root, portable))
                throw std::runtime_error("invalid project layout root: " + root);
            return root;
        }

        auto Under(const std::string &root, const std::string &file) -> std::string
        {
            return root == "." ? file : root + "/" + file;
        }
    } // namespace

    auto NewProject(const fs::path &root, const std::string_view kind, const std::string_view name,
                    const CliArguments &arguments) -> int
    {
        const std::string product{name};
        static const std::regex identifier{"[A-Za-z0-9][A-Za-z0-9_.-]*"};
        if (!std::regex_match(product, identifier))
            throw std::runtime_error("invalid project name");
        const bool executable = kind == "executable";
        const std::map<std::string_view, std::string> libraries{{"library", "Static"},
                                                                {"static", "Static"},
                                                                {"shared", "Shared"},
                                                                {"interface", "Interface"},
                                                                {"plugin", "Plugin"}};
        if (!executable && !libraries.contains(kind))
            throw std::runtime_error("unknown project kind");
        if (!arguments.positional.empty())
            throw std::runtime_error("unexpected project creation argument");
        auto symbol = product;
        std::ranges::replace_if(symbol, [](char c) { return c == '-' || c == '.'; }, '_');
        if (symbol.front() >= '0' && symbol.front() <= '9')
            symbol = "Project_" + symbol;
        std::string source = "src", header = "include";
        if (arguments.layout == "custom")
        {
            source = LayoutRoot(arguments.sourceRoot);
            header = LayoutRoot(arguments.headerRoot);
        }
        else if (arguments.layout == "colocated")
        {
            source = ".";
            header = ".";
        }
        else if (arguments.layout == "public-private")
        {
            source = "Source/" + symbol + "/Private";
            header = "Source/" + symbol + "/Public";
        }
        else if (arguments.layout != "split")
            throw std::runtime_error("unknown project layout");
        const bool compiled = kind != "interface";
        const std::string visibility = compiled ? "Public" : "Interface";
        std::map<std::string, std::string> files{};
        const auto tag = executable ? "Executable" : "Library";
        std::string manifest = "<" + std::string(tag) + " Name=\"" + product + "\"" +
                               (executable ? "" : " Kind=\"" + libraries.at(kind) + "\"") +
                               (kind == "interface" ? ">\n  <Build Conventions=\"false\">\n" : ">\n  <Build>\n");
        if (compiled)
            manifest += "    <Source Include=\"" + Under(source, source == "." ? "*.cpp" : "**/*.cpp") + "\" />\n";
        if (executable)
            files[Under(source, "main.cpp")] =
                "#include <iostream>\n\nint main() { std::cout << \"Hello from NGIN\\n\"; }\n";
        else
        {
            const auto filename = symbol + ".hpp";
            const auto function = symbol + "_version";
            std::string declaration = "#pragma once\n\n";
            std::string implementation;
            if (kind == "shared" || kind == "plugin")
            {
                const auto api = symbol + "_API";
                const auto building = symbol + "_BUILDING_LIBRARY";
                declaration += "#if defined(_WIN32)\n#  if defined(" + building + ")\n#    define " + api +
                               " __declspec(dllexport)\n#  else\n#    define " + api +
                               " __declspec(dllimport)\n#  endif\n#else\n#  define " + api + "\n#endif\n\n" + api + " ";
                implementation = "#define " + building + "\n";
            }
            if (compiled)
            {
                declaration += "int " + function + "();\n";
                files[Under(source, symbol + ".cpp")] =
                    implementation + "#include <" + filename + ">\n\nint " + function + "() { return 1; }\n";
            }
            else
                declaration += "inline int " + function + "() { return 1; }\n";
            files[Under(header, filename)] = std::move(declaration);
            manifest += "    <Header Include=\"" + Under(header, header == "." ? "*.hpp" : "**/*.hpp") +
                        "\" Visibility=\"" + visibility + "\" />\n";
            manifest += "    <IncludeDirectory Path=\"" + header + "\" Visibility=\"" + visibility + "\" />\n";
        }
        manifest += "  </Build>\n</" + std::string(tag) + ">\n";
        files[product + ".nginproj"] = manifest;
        if (arguments.format == "json")
        {
            CanonicalValue::Object contents{};
            for (const auto &[path, text] : files)
                contents.emplace(path, text);
            std::cout << SerializeCanonical(
                             CanonicalValue::Object{{"kind", "NGIN.ProjectTemplate"}, {"files", std::move(contents)}})
                      << '\n';
            return 0;
        }
        const auto directory = root / product;
        if (!fs::create_directory(directory))
            throw std::runtime_error(directory.string() + ": already exists");
        try
        {
            for (const auto &[path, text] : files)
            {
                const auto file = directory / path;
                fs::create_directories(file.parent_path());
                std::ofstream stream;
                stream.exceptions(std::ios::failbit | std::ios::badbit);
                stream.open(file, std::ios::binary);
                stream << text;
                stream.close();
            }
        }
        catch (...)
        {
            fs::remove_all(directory);
            throw;
        }
        std::cout << "Created " << tag << " project at " << directory << '\n';
        return 0;
    }
} // namespace NGIN::CLI

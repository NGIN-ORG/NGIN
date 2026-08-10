#include "ManifestArtifacts.hpp"

#include <exception>
#include <iostream>

auto main(const int argc, const char *const *argv) -> int
{
    if (argc != 2)
    {
        std::cerr << "usage: ngin_manifest_schema_generator <output-directory>\n";
        return 2;
    }
    try
    {
        NGIN::CLI::WriteManifestArtifacts(argv[1]);
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

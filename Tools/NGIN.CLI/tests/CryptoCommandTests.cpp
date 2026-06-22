#include "TestSupport.hpp"

using Catch::Matchers::ContainsSubstring;

namespace NGIN::CLI::Tests
{
    TEST_CASE("crypto info reports selected backend and algorithms")
    {
        TempDir temp{};
        ParsedArgs args{};
        args.colorMode = OutputColorMode::Never;

        std::ostringstream captured{};
        auto *previous = std::cout.rdbuf(captured.rdbuf());
        const auto exitCode = CmdCryptoInfo(temp.path(), args);
        std::cout.rdbuf(previous);

        const auto output = captured.str();
        REQUIRE(exitCode == 0);
        REQUIRE_THAT(output, ContainsSubstring("NGIN crypto"));
        REQUIRE_THAT(output, ContainsSubstring("backend"));
        REQUIRE_THAT(output, ContainsSubstring("Algorithms"));
        REQUIRE_THAT(output, ContainsSubstring("random [Random]"));
        REQUIRE_THAT(output, ContainsSubstring("supported"));
    }

    TEST_CASE("crypto explain reports one algorithm support result")
    {
        TempDir temp{};
        ParsedArgs args{};
        args.colorMode = OutputColorMode::Never;
        args.algorithmName = "random";

        std::ostringstream captured{};
        auto *previous = std::cout.rdbuf(captured.rdbuf());
        const auto exitCode = CmdCryptoExplain(temp.path(), args);
        std::cout.rdbuf(previous);

        const auto output = captured.str();
        REQUIRE(exitCode == 0);
        REQUIRE_THAT(output, ContainsSubstring("NGIN crypto explain"));
        REQUIRE_THAT(output, ContainsSubstring("algorithm"));
        REQUIRE_THAT(output, ContainsSubstring("random"));
        REQUIRE_THAT(output, ContainsSubstring("result"));
        REQUIRE_THAT(output, ContainsSubstring("supported"));
    }

    TEST_CASE("crypto diagnostics support json output")
    {
        TempDir temp{};

        ParsedArgs infoArgs{};
        infoArgs.format = "json";
        std::ostringstream infoCaptured{};
        auto *previous = std::cout.rdbuf(infoCaptured.rdbuf());
        const auto infoExitCode = CmdCryptoInfo(temp.path(), infoArgs);
        std::cout.rdbuf(previous);

        REQUIRE(infoExitCode == 0);
        REQUIRE_THAT(infoCaptured.str(), ContainsSubstring("\"available\":true"));
        REQUIRE_THAT(infoCaptured.str(), ContainsSubstring("\"backend\""));
        REQUIRE_THAT(infoCaptured.str(), ContainsSubstring("\"algorithms\""));

        ParsedArgs explainArgs{};
        explainArgs.format = "json";
        explainArgs.algorithmName = "AES-256-GCM";
        std::ostringstream explainCaptured{};
        previous = std::cout.rdbuf(explainCaptured.rdbuf());
        const auto explainExitCode = CmdCryptoExplain(temp.path(), explainArgs);
        std::cout.rdbuf(previous);

        REQUIRE(explainExitCode == 0);
        REQUIRE_THAT(explainCaptured.str(), ContainsSubstring("\"id\":\"aes-256-gcm\""));
        REQUIRE_THAT(explainCaptured.str(), ContainsSubstring("\"result\""));
    }

    TEST_CASE("crypto explain includes workspace provider graph when project is supplied")
    {
        TempDir temp{};
        WriteFile(temp.path() / "Workspace.ngin",
                  R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="CryptoGraphWorkspace">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
  </Packages>
</Workspace>
)xml");
        WriteFile(temp.path() / "Packages/OpenSSL/OpenSSL.nginpkg",
                  R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="OpenSSL" Version="3.0.0">
  <Build Backend="CMake"
         Mode="FindPackage"
         ProviderPackage="openssl"
         ProviderVersion="3.0.0"
         CMakePackage="OpenSSL"
         Linkage="Static;Shared"
         RuntimeDeployment="PackageRuntimeLibraries"
         RuntimeArtifacts="libcrypto" />
  <Features>
    <Feature Name="CryptoProvider" Description="Test crypto provider metadata.">
      <Provides>
        <Capability Name="Crypto.Provider.openssl" />
        <Capability Name="Crypto.Algorithm.Aes256Gcm" />
      </Provides>
    </Feature>
  </Features>
</Package>
)xml");
        WriteFile(temp.path() / "App/App.nginproj",
                  R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="CryptoGraph.App" DefaultProfile="dev">
  <Application>
    <Uses>
      <Package Name="OpenSSL" Version="[3.0.0,4.0.0)" Scope="Target">
        <Feature Name="CryptoProvider" />
      </Package>
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
  <Profile Name="dev">
    <Defaults>
      <BuildType Name="Debug" />
    </Defaults>
  </Profile>
</Project>
)xml");
        WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

        ParsedArgs args{};
        args.projectPath = (temp.path() / "App/App.nginproj").string();
        args.profileName = "dev";
        args.colorMode = OutputColorMode::Never;
        args.algorithmName = "AES-256-GCM";

        std::ostringstream captured{};
        auto *previous = std::cout.rdbuf(captured.rdbuf());
        const auto exitCode = CmdCryptoExplain(temp.path(), args);
        std::cout.rdbuf(previous);

        REQUIRE(exitCode == 0);
        REQUIRE_THAT(captured.str(), ContainsSubstring("Workspace graph"));
        REQUIRE_THAT(captured.str(), ContainsSubstring("algorithmCapability  Crypto.Algorithm.Aes256Gcm"));
        REQUIRE_THAT(captured.str(), ContainsSubstring("selected package feature declares this algorithm"));
        REQUIRE_THAT(captured.str(), ContainsSubstring("OpenSSL::CryptoProvider"));
        REQUIRE_THAT(captured.str(), ContainsSubstring("openssl via OpenSSL::CryptoProvider"));
        REQUIRE_THAT(captured.str(), ContainsSubstring("providerPackage=openssl"));
        REQUIRE_THAT(captured.str(), ContainsSubstring("linkage=Static;Shared"));
        REQUIRE_THAT(captured.str(), ContainsSubstring("runtimeArtifacts=libcrypto"));
    }

    TEST_CASE("crypto explain emits workspace provider graph in json")
    {
        TempDir temp{};
        WriteFile(temp.path() / "Workspace.ngin",
                  R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="CryptoGraphJsonWorkspace">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
  </Packages>
</Workspace>
)xml");
        WriteFile(temp.path() / "Packages/libsodium/libsodium.nginpkg",
                  R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="libsodium" Version="1.0.19">
  <Build Backend="CMake"
         Mode="FindPackage"
         ProviderPackage="libsodium"
         ProviderVersion="1.0.19"
         CMakePackage="libsodium"
         Linkage="Static;Shared"
         RuntimeDeployment="PackageRuntimeLibraries"
         RuntimeArtifacts="libsodium" />
  <Features>
    <Feature Name="CryptoProvider" Description="Test crypto provider metadata.">
      <Provides>
        <Capability Name="Crypto.Provider.libsodium" />
        <Capability Name="Crypto.Algorithm.XChaCha20Poly1305" />
      </Provides>
    </Feature>
  </Features>
</Package>
)xml");
        WriteFile(temp.path() / "App/App.nginproj",
                  R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="CryptoGraphJson.App" DefaultProfile="dev">
  <Application>
    <Uses>
      <Package Name="libsodium" Version="[1.0.0,2.0.0)" Scope="Target">
        <Feature Name="CryptoProvider" />
      </Package>
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
  <Profile Name="dev">
    <Defaults>
      <BuildType Name="Debug" />
    </Defaults>
  </Profile>
</Project>
)xml");
        WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

        ParsedArgs args{};
        args.projectPath = (temp.path() / "App/App.nginproj").string();
        args.profileName = "dev";
        args.format = "json";
        args.algorithmName = "xchacha20-poly1305";

        std::ostringstream captured{};
        auto *previous = std::cout.rdbuf(captured.rdbuf());
        const auto exitCode = CmdCryptoExplain(temp.path(), args);
        std::cout.rdbuf(previous);

        REQUIRE(exitCode == 0);
        REQUIRE_THAT(captured.str(), ContainsSubstring("\"workspace\""));
        REQUIRE_THAT(captured.str(), ContainsSubstring("\"algorithmCapability\":\"Crypto.Algorithm.XChaCha20Poly1305\""));
        REQUIRE_THAT(captured.str(), ContainsSubstring("\"algorithmProviders\""));
        REQUIRE_THAT(captured.str(), ContainsSubstring("\"backend\":\"libsodium\""));
        REQUIRE_THAT(captured.str(), ContainsSubstring("\"providerPackage\":\"libsodium\""));
        REQUIRE_THAT(captured.str(), ContainsSubstring("\"linkage\":\"Static;Shared\""));
        REQUIRE_THAT(captured.str(), ContainsSubstring("\"runtimeArtifacts\":\"libsodium\""));
    }

    TEST_CASE("crypto explain accepts --algorithm and rejects unknown names")
    {
        const char *argv[] = {"ngin", "crypto", "explain", "--algorithm", "AES-256-GCM"};
        auto args = ParseCommonArgs(5, const_cast<char **>(argv), 3);

        REQUIRE(args.algorithmName.has_value());
        REQUIRE(*args.algorithmName == "AES-256-GCM");

        args.colorMode = OutputColorMode::Never;
        std::ostringstream captured{};
        auto *previous = std::cout.rdbuf(captured.rdbuf());
        const auto exitCode = CmdCryptoExplain(fs::current_path(), args);
        std::cout.rdbuf(previous);

        REQUIRE(exitCode == 0);
        REQUIRE_THAT(captured.str(), ContainsSubstring("aes-256-gcm"));

        ParsedArgs unknown{};
        unknown.algorithmName = "not-an-algorithm";
        REQUIRE_THROWS_WITH(CmdCryptoExplain(fs::current_path(), unknown),
                            ContainsSubstring("unknown crypto algorithm 'not-an-algorithm'"));
    }
}// namespace NGIN::CLI::Tests

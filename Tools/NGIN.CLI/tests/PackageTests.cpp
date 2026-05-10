#include "TestSupport.hpp"

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <thread>

namespace
{
    class LocalHttpFileServer
    {
    public:
        explicit LocalHttpFileServer(fs::path root) : root_(std::move(root))
        {
            socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
            REQUIRE(socket_ >= 0);
            int reuse = 1;
            REQUIRE(::setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == 0);

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = 0;
            REQUIRE(::bind(socket_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
            REQUIRE(::listen(socket_, 16) == 0);

            socklen_t length = sizeof(address);
            REQUIRE(::getsockname(socket_, reinterpret_cast<sockaddr *>(&address), &length) == 0);
            port_ = ntohs(address.sin_port);
            running_ = true;
            thread_ = std::thread([this]() { Serve(); });
        }

        ~LocalHttpFileServer()
        {
            running_ = false;
            const int client = ::socket(AF_INET, SOCK_STREAM, 0);
            if (client >= 0)
            {
                sockaddr_in address{};
                address.sin_family = AF_INET;
                address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                address.sin_port = htons(port_);
                (void)::connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address));
                ::close(client);
            }
            if (thread_.joinable())
            {
                thread_.join();
            }
            if (socket_ >= 0)
            {
                ::close(socket_);
            }
        }

        [[nodiscard]] auto Url(std::string_view path) const -> std::string
        {
            return "http://127.0.0.1:" + std::to_string(port_) + std::string(path);
        }

    private:
        auto Serve() -> void
        {
            while (running_)
            {
                const int client = ::accept(socket_, nullptr, nullptr);
                if (client < 0)
                {
                    continue;
                }
                HandleClient(client);
                ::close(client);
            }
        }

        auto HandleClient(int client) -> void
        {
            char buffer[4096]{};
            const auto read = ::recv(client, buffer, sizeof(buffer) - 1, 0);
            if (read <= 0)
            {
                return;
            }
            const std::string request(buffer, static_cast<std::size_t>(read));
            const auto pathStart = request.find("GET ");
            const auto pathEnd = request.find(" HTTP/", pathStart == std::string::npos ? 0 : pathStart);
            if (pathStart == std::string::npos || pathEnd == std::string::npos)
            {
                Respond(client, "400 Bad Request", {});
                return;
            }
            auto path = request.substr(pathStart + 4, pathEnd - (pathStart + 4));
            if (!path.empty() && path.front() == '/')
            {
                path.erase(path.begin());
            }
            if (path.find("..") != std::string::npos)
            {
                Respond(client, "403 Forbidden", {});
                return;
            }
            const auto file = (root_ / fs::path(path)).lexically_normal();
            if (!fs::exists(file) || !fs::is_regular_file(file))
            {
                Respond(client, "404 Not Found", {});
                return;
            }
            Respond(client, "200 OK", ReadBinary(file));
        }

        static auto Respond(int client, std::string_view status, std::string_view body) -> void
        {
            const auto header = "HTTP/1.1 " + std::string(status) + "\r\nContent-Length: "
                                + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
            (void)::send(client, header.data(), header.size(), 0);
            if (!body.empty())
            {
                (void)::send(client, body.data(), body.size(), 0);
            }
        }

        fs::path root_{};
        int socket_{-1};
        std::uint16_t port_{};
        std::atomic_bool running_{false};
        std::thread thread_{};
    };
}
#endif

TEST_CASE("file URL package source participates in package catalog")
{
    TempDir temp{};
    const auto feedRoot = temp.path() / "feed";
    WriteFile(temp.path() / "Workspace.ngin",
              "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
              "<Workspace SchemaVersion=\"4\" Name=\"FileFeedWorkspace\">\n"
              "  <Projects>\n"
              "    <Project Path=\"App/App.nginproj\" />\n"
              "  </Projects>\n"
              "  <Packages>\n"
              "    <Source Name=\"feed\" Url=\"file://" + feedRoot.generic_string() + "\" />\n"
              "  </Packages>\n"
              "</Workspace>\n");
    WriteFile(feedRoot / "Core/Core.nginpkg",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Core" Version="1.0.0">
  <Library Name="Package.Core">
    <Exports>
      <LibraryTarget Name="Package::Core" />
    </Exports>
  </Library>
</Package>
)xml");
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="FileFeed.App">
  <Application>
    <Uses>
      <Package Name="Package.Core" Version="[1.0.0,2.0.0)" Scope="Target" />
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
    const std::optional<std::string> selectedProfile{};
    const auto &profile = ProfileByName(project, selectedProfile);
    const auto resolved = ResolveLaunch(project, profile);

    REQUIRE_FALSE(resolved.diagnostics.HasErrors());
    REQUIRE(resolved.value.has_value());
    REQUIRE(resolved.value->orderedPackages.size() == 1);
    REQUIRE(resolved.value->orderedPackages.front().manifest.name == "Package.Core");
}

TEST_CASE("static package feed index participates in package restore")
{
    TempDir temp{};
    const auto feedRoot = temp.path() / "feed";
    const auto feedIndex = feedRoot / "index.nginfeed";
    WriteFile(temp.path() / "Workspace.ngin",
              "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
              "<Workspace SchemaVersion=\"4\" Name=\"StaticFeedWorkspace\">\n"
              "  <Projects>\n"
              "    <Project Path=\"App/App.nginproj\" />\n"
              "  </Projects>\n"
              "  <Packages>\n"
              "    <Source Name=\"feed\" Url=\"file://" + feedIndex.generic_string() + "\" />\n"
              "  </Packages>\n"
              "</Workspace>\n");
    const std::string packageManifest = R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Core" Version="1.0.0">
  <Library Name="Package.Core">
    <Exports>
      <LibraryTarget Name="Package::Core" />
    </Exports>
  </Library>
</Package>
)xml";
    WriteNginPack(feedRoot / "Core/Core.nginpack", packageManifest);
    WriteFile(feedIndex,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<PackageFeed SchemaVersion="4">
  <Packages>
    <Package Name="Package.Core" Version="1.0.0" Path="Core/Core.nginpack" />
  </Packages>
</PackageFeed>
)xml");
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="StaticFeed.App">
  <Application>
    <Uses>
      <Package Name="Package.Core" Version="[1.0.0,2.0.0)" Scope="Target" />
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.projectPath = (temp.path() / "App/App.nginproj").string();
    args.outputPath = (temp.path() / "store").string();

    REQUIRE(CmdRestore(temp.path(), args) == 0);
    REQUIRE(fs::exists(temp.path() / "store/Package.Core/1.0.0/Core.nginpack"));
    REQUIRE(fs::exists(temp.path() / "store/Package.Core/1.0.0/package.nginpkg"));
}

TEST_CASE("network package feed index participates in restore and extracts package payload")
{
#ifdef _WIN32
    SKIP("local HTTP feed test is implemented for POSIX test hosts");
#else
    TempDir temp{};
    const auto feedRoot = temp.path() / "feed";
    const auto feedIndex = feedRoot / "index.nginfeed";
    const std::string packageManifest = R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Net" Version="1.0.0">
  <Library Name="Package.Net">
    <Exports>
      <Headers Path="include/**.hpp" />
      <LibraryTarget Name="Package::Net" />
    </Exports>
  </Library>
</Package>
)xml";
    WriteNginPack(
        feedRoot / "Net/Net.nginpack",
        packageManifest,
        std::vector<ZipFileEntry>{
            ZipFileEntry{
                .path = "include/net.hpp",
                .contents = "#pragma once\n",
            },
        });
    WriteFile(feedIndex,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<PackageFeed SchemaVersion="4">
  <Packages>
    <Package Name="Package.Net" Version="1.0.0" Path="Net/Net.nginpack" />
  </Packages>
</PackageFeed>
)xml");

    LocalHttpFileServer server(feedRoot);
    WriteFile(temp.path() / "Workspace.ngin",
              "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
              "<Workspace SchemaVersion=\"4\" Name=\"NetworkFeedWorkspace\">\n"
              "  <Projects>\n"
              "    <Project Path=\"App/App.nginproj\" />\n"
              "  </Projects>\n"
              "  <Packages>\n"
              "    <Source Name=\"feed\" Url=\"" + server.Url("/index.nginfeed") + "\" />\n"
              "  </Packages>\n"
              "</Workspace>\n");
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="NetworkFeed.App">
  <Application>
    <Uses>
      <Package Name="Package.Net" Version="[1.0.0,2.0.0)" Scope="Target" />
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.projectPath = (temp.path() / "App/App.nginproj").string();
    args.outputPath = (temp.path() / "store").string();

    REQUIRE(CmdRestore(temp.path(), args) == 0);
    REQUIRE(fs::exists(temp.path() / "store/Package.Net/1.0.0/Net.nginpack"));
    REQUIRE(fs::exists(temp.path() / "store/Package.Net/1.0.0/package.nginpkg"));
    REQUIRE(fs::exists(temp.path() / "store/Package.Net/1.0.0/include/net.hpp"));
    REQUIRE_THAT(ReadFile(temp.path() / "store/Package.Net/1.0.0/package.nginpkg"),
                 ContainsSubstring(R"(<Package SchemaVersion="4" Name="Package.Net" Version="1.0.0">)"));
#endif
}

TEST_CASE("resolved package scopes flow into graph metadata")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="ScopeWorkspace">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
  </Packages>
</Workspace>
)xml");
    const std::string coreManifest = R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Core" Version="1.0.0">
  <Library Name="Package.Core">
    <Exports>
      <LibraryTarget Name="Package::Core" />
    </Exports>
  </Library>
</Package>
)xml";
    WriteNginPack(temp.path() / "Packages/Core/Core.nginpack", coreManifest);
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Scope.App">
  <Application>
    <Uses>
      <Package Name="Package.Core" Version="[1.0.0,2.0.0)" Scope="Build;Target;Runtime" />
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
    const std::optional<std::string> selectedProfile{};
    const auto &profile = ProfileByName(project, selectedProfile);
    const auto resolved = ResolveLaunch(project, profile);

    const auto diagnostics = DiagnosticMessages(resolved.diagnostics);
    const auto firstDiagnostic = diagnostics.empty() ? std::string{"no diagnostics"} : diagnostics.front();
    INFO(firstDiagnostic);
    REQUIRE_FALSE(resolved.diagnostics.HasErrors());
    REQUIRE(resolved.value.has_value());
    REQUIRE(resolved.value->packageScopes.at("Package.Core") == "Build;Runtime;Target");

    ParsedArgs inspectArgs{};
    inspectArgs.projectPath = (temp.path() / "App/App.nginproj").string();
    inspectArgs.format = "json";
    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdInspect(temp.path(), inspectArgs);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("closures":["Host","Target","Runtime"])"));

    ParsedArgs graphArgs{};
    graphArgs.projectPath = (temp.path() / "App/App.nginproj").string();
    graphArgs.format = "json";
    graphArgs.graphPlan = "package";
    std::ostringstream graphCaptured{};
    previous = std::cout.rdbuf(graphCaptured.rdbuf());
    const auto graphExitCode = CmdGraph(temp.path(), graphArgs);
    std::cout.rdbuf(previous);

    REQUIRE(graphExitCode == 0);
    REQUIRE_THAT(graphCaptured.str(), ContainsSubstring(R"("plan": "package")"));
    REQUIRE_THAT(graphCaptured.str(), ContainsSubstring(R"("name":"Package.Core","version":"1.0.0")"));
    REQUIRE_THAT(graphCaptured.str(), ContainsSubstring(R"("closures":["Host","Target","Runtime"])"));
    REQUIRE_THAT(graphCaptured.str(), ContainsSubstring(R"("reason":"resolved package dependency")"));
}

TEST_CASE("dependency overlays mutate scopes by dependency identity")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="ScopeMutationWorkspace">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
  </Packages>
</Workspace>
)xml");
    const std::string coreManifest = R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Core" Version="1.0.0">
  <Library Name="Package.Core">
    <Exports>
      <LibraryTarget Name="Package::Core" />
    </Exports>
  </Library>
  <Features>
    <Feature Name="Diagnostics" />
  </Features>
</Package>
)xml";
    WriteNginPack(temp.path() / "Packages/Core/Core.nginpack", coreManifest);
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="ScopeMutation.App" DefaultProfile="shipping">
  <Application>
    <Uses>
      <Package Name="Package.Core" Version="[1.0.0,2.0.0)" Scope="Target" />
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
  <Profile Name="shipping">
    <Application>
      <Uses>
        <Package Name="Package.Core" Version="[1.0.0,2.0.0)" AddScope="Runtime" RemoveScope="Target">
          <Feature Name="Diagnostics" />
        </Package>
      </Uses>
    </Application>
  </Profile>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
    const auto resolved = ResolveLaunch(project, ProfileByName(project, "shipping"));

    const auto diagnostics = DiagnosticMessages(resolved.diagnostics);
    const auto firstDiagnostic = diagnostics.empty() ? std::string{"no diagnostics"} : diagnostics.front();
    INFO(firstDiagnostic);
    REQUIRE_FALSE(resolved.diagnostics.HasErrors());
    REQUIRE(resolved.value.has_value());
    REQUIRE(resolved.value->orderedPackages.size() == 1);
    REQUIRE(resolved.value->packageScopes.at("Package.Core") == "Runtime");
    REQUIRE(resolved.value->selectedPackageFeatures.size() == 1);
    REQUIRE(resolved.value->selectedPackageFeatures[0].packageName == "Package.Core");
    REQUIRE(resolved.value->selectedPackageFeatures[0].featureName == "Diagnostics");
}

TEST_CASE("package resolution reports conflicting dependency version ranges")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="ConflictWorkspace">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
  </Packages>
</Workspace>
)xml");
    const std::string coreManifest = R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Core" Version="1.0.0">
  <Library Name="Package.Core">
    <Exports>
      <LibraryTarget Name="Package::Core" />
    </Exports>
  </Library>
</Package>
)xml";
    WriteNginPack(temp.path() / "Packages/Core/Core.nginpack", coreManifest);
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Conflict.App">
  <Application>
    <Uses>
      <Package Name="Package.Core" Version="[1.0.0,2.0.0)" Scope="Target" />
      <Package Name="Package.Core" Version="[2.0.0,3.0.0)" Scope="Runtime" />
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
    const std::optional<std::string> selectedProfile{};
    const auto &profile = ProfileByName(project, selectedProfile);
    const auto resolved = ResolveLaunch(project, profile);
    const auto diagnostics = DiagnosticMessages(resolved.diagnostics);

    REQUIRE(resolved.diagnostics.HasErrors());
    REQUIRE(std::any_of(
        diagnostics.begin(),
        diagnostics.end(),
        [](const std::string &message)
        {
            return message.find("conflicting version ranges") != std::string::npos;
        }));
}

TEST_CASE("package resolution validates later transitive package ranges")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="TransitiveConflictWorkspace">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
  </Packages>
</Workspace>
)xml");
    const std::string coreManifest = R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Core" Version="1.0.0">
  <Library Name="Package.Core">
    <Exports>
      <LibraryTarget Name="Package::Core" />
    </Exports>
  </Library>
</Package>
)xml";
    WriteNginPack(temp.path() / "Packages/Core/Core.nginpack", coreManifest);
    WriteFile(temp.path() / "Packages/Feature/Feature.nginpkg",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Feature" Version="1.0.0">
  <Uses>
    <Package Name="Package.Core" Version="[2.0.0,3.0.0)" Scope="Target" />
  </Uses>
  <Library Name="Package.Feature">
    <Exports>
      <LibraryTarget Name="Package::Feature" />
    </Exports>
  </Library>
</Package>
)xml");
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Transitive.Conflict.App">
  <Application>
    <Uses>
      <Package Name="Package.Core" Version="[1.0.0,2.0.0)" Scope="Target" />
      <Package Name="Package.Feature" Version="[1.0.0,2.0.0)" Scope="Target" />
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
    const std::optional<std::string> selectedProfile{};
    const auto &profile = ProfileByName(project, selectedProfile);
    const auto resolved = ResolveLaunch(project, profile);
    const auto diagnostics = DiagnosticMessages(resolved.diagnostics);

    REQUIRE(resolved.diagnostics.HasErrors());
    REQUIRE(std::any_of(
        diagnostics.begin(),
        diagnostics.end(),
        [](const std::string &message)
        {
            return message.find("does not satisfy later range '[2.0.0,3.0.0)'") != std::string::npos;
        }));
}

TEST_CASE("package provider override is exposed in resolved package metadata")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="ProviderWorkspace">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
    <PackageProvider Name="Package.Core" Path="Providers/Core" />
  </Packages>
</Workspace>
)xml");
    WriteFile(temp.path() / "Packages/Core/Core.nginpkg",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Core" Version="1.0.0">
  <Library Name="Package.Core">
    <Exports>
      <LibraryTarget Name="Package::Core" />
    </Exports>
  </Library>
</Package>
)xml");
    fs::create_directories(temp.path() / "Providers/Core");
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Provider.App">
  <Application>
    <Uses>
      <Package Name="Package.Core" Version="[1.0.0,2.0.0)" Scope="Target" />
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
    const std::optional<std::string> selectedProfile{};
    const auto &profile = ProfileByName(project, selectedProfile);
    const auto resolved = ResolveLaunch(project, profile);

    REQUIRE_FALSE(resolved.diagnostics.HasErrors());
    REQUIRE(resolved.value.has_value());
    REQUIRE(resolved.value->orderedPackages.size() == 1);
    REQUIRE(resolved.value->orderedPackages[0].source == "provider");
    REQUIRE(resolved.value->orderedPackages[0].sourceDirectory == (temp.path() / "Providers/Core").lexically_normal());

    ParsedArgs args{};
    args.projectPath = (temp.path() / "App/App.nginproj").string();
    args.format = "json";
    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdInspect(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("source":"provider")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring((temp.path() / "Providers/Core").lexically_normal().string()));
}

TEST_CASE("restore writes package store and lock file")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="RestoreWorkspace">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
  </Packages>
</Workspace>
)xml");
    const std::string restoreCoreManifest = R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Core" Version="1.0.0">
  <Library Name="Package.Core">
    <Exports>
      <LibraryTarget Name="Package::Core" />
    </Exports>
  </Library>
</Package>
)xml";
    WriteNginPack(
        temp.path() / "Packages/Core/Core.nginpack",
        restoreCoreManifest,
        std::vector<ZipFileEntry>{
            ZipFileEntry{
                .path = "include/core.hpp",
                .contents = "#pragma once\n",
            },
        });
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Restore.App">
  <Application>
    <Uses>
      <Package Name="Package.Core" Version="[1.0.0,2.0.0)" Scope="Target" />
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.projectPath = (temp.path() / "App/App.nginproj").string();
    args.outputPath = (temp.path() / "store").string();

    REQUIRE(CmdRestore(temp.path(), args) == 0);
    REQUIRE(fs::exists(temp.path() / "store/Package.Core/1.0.0/Core.nginpack"));
    REQUIRE(fs::exists(temp.path() / "store/Package.Core/1.0.0/package.nginpkg"));
    REQUIRE(fs::exists(temp.path() / "store/Package.Core/1.0.0/include/core.hpp"));
    REQUIRE_THAT(ReadFile(temp.path() / "store/Package.Core/1.0.0/package.nginpkg"),
                 ContainsSubstring(R"(<Package SchemaVersion="4" Name="Package.Core" Version="1.0.0">)"));
    REQUIRE(fs::exists(temp.path() / "ngin.lock"));
    REQUIRE_THAT(ReadFile(temp.path() / "ngin.lock"), ContainsSubstring(R"(Scope="Target")"));

    ParsedArgs lockedArgs{};
    lockedArgs.projectPath = (temp.path() / "App/App.nginproj").string();
    lockedArgs.outputPath = (temp.path() / "locked-store").string();
    lockedArgs.locked = true;

    REQUIRE(CmdRestore(temp.path(), lockedArgs) == 0);
    REQUIRE(fs::exists(temp.path() / "locked-store/Package.Core/1.0.0/Core.nginpack"));
    REQUIRE(fs::exists(temp.path() / "locked-store/Package.Core/1.0.0/package.nginpkg"));
    REQUIRE(fs::exists(temp.path() / "locked-store/Package.Core/1.0.0/include/core.hpp"));

    WriteFile(temp.path() / "ngin.lock", "<Lock />\n");
    REQUIRE(CmdRestore(temp.path(), lockedArgs) == 1);
}

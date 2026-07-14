#include "TestSupport.hpp"

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <thread>

namespace {
class LocalHttpFileServer {
public:
  explicit LocalHttpFileServer(fs::path root) : root_(std::move(root)) {
    socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(socket_ >= 0);
    int reuse = 1;
    REQUIRE(::setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &reuse,
                         sizeof(reuse)) == 0);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    REQUIRE(::bind(socket_, reinterpret_cast<sockaddr *>(&address),
                   sizeof(address)) == 0);
    REQUIRE(::listen(socket_, 16) == 0);

    socklen_t length = sizeof(address);
    REQUIRE(::getsockname(socket_, reinterpret_cast<sockaddr *>(&address),
                          &length) == 0);
    port_ = ntohs(address.sin_port);
    running_ = true;
    thread_ = std::thread([this]() { Serve(); });
  }

  ~LocalHttpFileServer() {
    running_ = false;
    const int client = ::socket(AF_INET, SOCK_STREAM, 0);
    if (client >= 0) {
      sockaddr_in address{};
      address.sin_family = AF_INET;
      address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      address.sin_port = htons(port_);
      (void)::connect(client, reinterpret_cast<sockaddr *>(&address),
                      sizeof(address));
      ::close(client);
    }
    if (thread_.joinable()) {
      thread_.join();
    }
    if (socket_ >= 0) {
      ::close(socket_);
    }
  }

  [[nodiscard]] auto Url(std::string_view path) const -> std::string {
    return "http://127.0.0.1:" + std::to_string(port_) + std::string(path);
  }

private:
  auto Serve() -> void {
    while (running_) {
      const int client = ::accept(socket_, nullptr, nullptr);
      if (client < 0) {
        continue;
      }
      HandleClient(client);
      ::close(client);
    }
  }

  auto HandleClient(int client) -> void {
    char buffer[4096]{};
    const auto read = ::recv(client, buffer, sizeof(buffer) - 1, 0);
    if (read <= 0) {
      return;
    }
    const std::string request(buffer, static_cast<std::size_t>(read));
    const auto pathStart = request.find("GET ");
    const auto pathEnd =
        request.find(" HTTP/", pathStart == std::string::npos ? 0 : pathStart);
    if (pathStart == std::string::npos || pathEnd == std::string::npos) {
      Respond(client, "400 Bad Request", {});
      return;
    }
    auto path = request.substr(pathStart + 4, pathEnd - (pathStart + 4));
    if (!path.empty() && path.front() == '/') {
      path.erase(path.begin());
    }
    if (path.find("..") != std::string::npos) {
      Respond(client, "403 Forbidden", {});
      return;
    }
    const auto file = (root_ / fs::path(path)).lexically_normal();
    if (!fs::exists(file) || !fs::is_regular_file(file)) {
      Respond(client, "404 Not Found", {});
      return;
    }
    Respond(client, "200 OK", ReadBinary(file));
  }

  static auto Respond(int client, std::string_view status,
                      std::string_view body) -> void {
    const auto header = "HTTP/1.1 " + std::string(status) +
                        "\r\nContent-Length: " + std::to_string(body.size()) +
                        "\r\nConnection: close\r\n\r\n";
    (void)::send(client, header.data(), header.size(), 0);
    if (!body.empty()) {
      (void)::send(client, body.data(), body.size(), 0);
    }
  }

  fs::path root_{};
  int socket_{-1};
  std::uint16_t port_{};
  std::atomic_bool running_{false};
  std::thread thread_{};
};
} // namespace
#endif

TEST_CASE("file URL package source participates in package catalog") {
  TempDir temp{};
  const auto feedRoot = temp.path() / "feed";
  WriteFile(temp.path() / "Workspace.ngin",
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<Workspace SchemaVersion=\"4\" Name=\"FileFeedWorkspace\">\n"
            "  <Projects>\n"
            "    <Project Path=\"App/App.nginproj\" />\n"
            "  </Projects>\n"
            "  <Packages>\n"
            "    <Source Name=\"feed\" Url=\"file://" +
                feedRoot.generic_string() +
                "\" />\n"
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
  REQUIRE(resolved.value->orderedPackages.front().manifest.name ==
          "Package.Core");
}

TEST_CASE("static package feed index participates in package restore") {
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
            "    <Source Name=\"feed\" Url=\"file://" +
                feedIndex.generic_string() +
                "\" />\n"
                "  </Packages>\n"
                "</Workspace>\n");
  const std::string packageManifest =
      R"xml(<?xml version="1.0" encoding="utf-8"?>
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

TEST_CASE("network package feed index participates in restore and extracts "
          "package payload") {
#ifdef _WIN32
  SUCCEED("local HTTP feed test is implemented for POSIX test hosts");
  return;
#else
  TempDir temp{};
  const auto feedRoot = temp.path() / "feed";
  const auto feedIndex = feedRoot / "index.nginfeed";
  const std::string packageManifest =
      R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Net" Version="1.0.0">
  <Library Name="Package.Net">
    <Exports>
      <Headers Path="include/**.hpp" />
      <LibraryTarget Name="Package::Net" />
    </Exports>
  </Library>
</Package>
)xml";
  WriteNginPack(feedRoot / "Net/Net.nginpack", packageManifest,
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
            "    <Source Name=\"feed\" Url=\"" +
                server.Url("/index.nginfeed") +
                "\" />\n"
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
  REQUIRE_THAT(
      ReadFile(temp.path() / "store/Package.Net/1.0.0/package.nginpkg"),
      ContainsSubstring(
          R"(<Package SchemaVersion="4" Name="Package.Net" Version="1.0.0">)"));
#endif
}

TEST_CASE("resolved package scopes flow into graph metadata") {
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
  const auto firstDiagnostic =
      diagnostics.empty() ? std::string{"no diagnostics"} : diagnostics.front();
  INFO(firstDiagnostic);
  REQUIRE_FALSE(resolved.diagnostics.HasErrors());
  REQUIRE(resolved.value.has_value());
  REQUIRE(resolved.value->packageScopes.at("Package.Core") ==
          "Build;Runtime;Target");

  ParsedArgs inspectArgs{};
  inspectArgs.projectPath = (temp.path() / "App/App.nginproj").string();
  inspectArgs.format = "json";
  std::ostringstream captured{};
  auto *previous = std::cout.rdbuf(captured.rdbuf());
  const auto exitCode = CmdInspect(temp.path(), inspectArgs);
  std::cout.rdbuf(previous);

  REQUIRE(exitCode == 0);
  REQUIRE_THAT(captured.str(),
               ContainsSubstring(R"("closures":["Host","Target","Runtime"])"));

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
  REQUIRE_THAT(graphCaptured.str(),
               ContainsSubstring(R"("name":"Package.Core","version":"1.0.0")"));
  REQUIRE_THAT(graphCaptured.str(),
               ContainsSubstring(R"("closures":["Host","Target","Runtime"])"));
  REQUIRE_THAT(graphCaptured.str(),
               ContainsSubstring(R"("reason":"resolved package dependency")"));
}

TEST_CASE("dependency overlays mutate scopes by dependency identity") {
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
  const auto resolved =
      ResolveLaunch(project, ProfileByName(project, "shipping"));

  const auto diagnostics = DiagnosticMessages(resolved.diagnostics);
  const auto firstDiagnostic =
      diagnostics.empty() ? std::string{"no diagnostics"} : diagnostics.front();
  INFO(firstDiagnostic);
  REQUIRE_FALSE(resolved.diagnostics.HasErrors());
  REQUIRE(resolved.value.has_value());
  REQUIRE(resolved.value->orderedPackages.size() == 1);
  REQUIRE(resolved.value->packageScopes.at("Package.Core") == "Runtime");
  REQUIRE(resolved.value->selectedPackageFeatures.size() == 1);
  REQUIRE(resolved.value->selectedPackageFeatures[0].packageName ==
          "Package.Core");
  REQUIRE(resolved.value->selectedPackageFeatures[0].featureName ==
          "Diagnostics");
}

TEST_CASE("package resolution reports conflicting dependency version ranges") {
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
      diagnostics.begin(), diagnostics.end(), [](const std::string &message) {
        return message.find("conflicting version ranges") != std::string::npos;
      }));
}

TEST_CASE("package resolution validates later transitive package ranges") {
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
      diagnostics.begin(), diagnostics.end(), [](const std::string &message) {
        return message.find("does not satisfy later range '[2.0.0,3.0.0)'") !=
               std::string::npos;
      }));
}

TEST_CASE("package provider override is exposed in resolved package metadata") {
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
  REQUIRE(resolved.value->orderedPackages[0].sourceDirectory ==
          (temp.path() / "Providers/Core").lexically_normal());

  ParsedArgs args{};
  args.projectPath = (temp.path() / "App/App.nginproj").string();
  args.format = "json";
  std::ostringstream captured{};
  auto *previous = std::cout.rdbuf(captured.rdbuf());
  const auto exitCode = CmdInspect(temp.path(), args);
  std::cout.rdbuf(previous);

  REQUIRE(exitCode == 0);
  REQUIRE_THAT(captured.str(), ContainsSubstring(R"("source":"provider")"));
  REQUIRE_THAT(captured.str(),
               ContainsSubstring((temp.path() / "Providers/Core")
                                     .lexically_normal()
                                     .generic_string()));
}

TEST_CASE("restore writes package store and lock file") {
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
  const std::string restoreCoreManifest =
      R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Core" Version="1.0.0">
  <Library Name="Package.Core">
    <Exports>
      <LibraryTarget Name="Package::Core" />
    </Exports>
  </Library>
</Package>
)xml";
  WriteNginPack(temp.path() / "Packages/Core/Core.nginpack",
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
  REQUIRE(
      fs::exists(temp.path() / "store/Package.Core/1.0.0/include/core.hpp"));
  REQUIRE_THAT(
      ReadFile(temp.path() / "store/Package.Core/1.0.0/package.nginpkg"),
      ContainsSubstring(
          R"(<Package SchemaVersion="4" Name="Package.Core" Version="1.0.0">)"));
  REQUIRE(fs::exists(temp.path() / "ngin.lock"));
  REQUIRE_THAT(ReadFile(temp.path() / "ngin.lock"),
               ContainsSubstring(R"(Scope="Target")"));

  ParsedArgs lockedArgs{};
  lockedArgs.projectPath = (temp.path() / "App/App.nginproj").string();
  lockedArgs.outputPath = (temp.path() / "locked-store").string();
  lockedArgs.locked = true;

  REQUIRE(CmdRestore(temp.path(), lockedArgs) == 0);
  REQUIRE(fs::exists(temp.path() /
                     "locked-store/Package.Core/1.0.0/Core.nginpack"));
  REQUIRE(fs::exists(temp.path() /
                     "locked-store/Package.Core/1.0.0/package.nginpkg"));
  REQUIRE(fs::exists(temp.path() /
                     "locked-store/Package.Core/1.0.0/include/core.hpp"));

  WriteFile(temp.path() / "ngin.lock", "<Lock />\n");
  REQUIRE(CmdRestore(temp.path(), lockedArgs) == 1);
}

#ifndef _WIN32
TEST_CASE(
    "restore runs vcpkg and conan providers and configure consumes metadata") {
  TempDir temp{};
  const auto vcpkgRoot = temp.path() / "Tools/vcpkg";
  const auto conanRoot = temp.path() / "Tools/conan";
  fs::create_directories(vcpkgRoot / "scripts/buildsystems");
  fs::create_directories(conanRoot);

  const auto vcpkgStateRoot = temp.path() / ".ngin/providers/vcpkg/dev";
  WriteFile(vcpkgRoot / "scripts/buildsystems/vcpkg.cmake",
            "list(APPEND CMAKE_PREFIX_PATH \"" +
                (vcpkgStateRoot / "installed").string() + "\")\n");
  WriteFile(vcpkgRoot / "vcpkg",
            "#!/bin/sh\n"
            "echo raw-vcpkg-output\n"
            "printf '%s\\n' \"$@\" > \"" +
                (temp.path() / "vcpkg.args").string() +
                "\"\n"
                "install=''\n"
                "while [ \"$#\" -gt 0 ]; do\n"
                "  if [ \"$1\" = '--x-install-root' ]; then shift; "
                "install=\"$1\"; fi\n"
                "  shift\n"
                "done\n"
                "mkdir -p \"$install/share/fmt\"\n"
                "cat > \"$install/share/fmt/fmtConfig.cmake\" <<'EOF'\n"
                "if(NOT TARGET fmt::fmt)\n"
                "  add_library(fmt::fmt INTERFACE IMPORTED)\n"
                "endif()\n"
                "EOF\n");
  fs::permissions(vcpkgRoot / "vcpkg", fs::perms::owner_exec |
                                           fs::perms::owner_read |
                                           fs::perms::owner_write);

  WriteFile(
      conanRoot / "conan",
      "#!/bin/sh\n"
      "echo raw-conan-output\n"
      "printf '%s\\n' \"$@\" > \"" +
          (temp.path() / "conan.args").string() +
          "\"\n"
          "out=''\n"
          "while [ \"$#\" -gt 0 ]; do\n"
          "  if [ \"$1\" = '--output-folder' ]; then shift; out=\"$1\"; fi\n"
          "  shift\n"
          "done\n"
          "mkdir -p \"$out\"\n"
          "cat > \"$out/ZLIBConfig.cmake\" <<'EOF'\n"
          "if(NOT TARGET ZLIB::ZLIB)\n"
          "  add_library(ZLIB::ZLIB INTERFACE IMPORTED)\n"
          "endif()\n"
          "EOF\n");
  fs::permissions(conanRoot / "conan", fs::perms::owner_exec |
                                           fs::perms::owner_read |
                                           fs::perms::owner_write);

  WriteFile(temp.path() / "Workspace.ngin",
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="ExternalProviders" DefaultProfile="dev">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
)xml" + std::string{"    <Provider Name=\"vcpkg\" Kind=\"Vcpkg\" Root=\""} +
                fs::relative(vcpkgRoot, temp.path()).string() +
                "\" Triplet=\"x64-linux\" />\n"
                "    <Provider Name=\"conan\" Kind=\"Conan\" Root=\"" +
                fs::relative(conanRoot, temp.path()).string() +
                "\" Profile=\"linux-gcc-debug\" />\n"
                "  </Packages>\n"
                "</Workspace>\n");
  WriteFile(temp.path() / "Packages/fmt/fmt.nginpkg",
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="fmt" Version="10.2.1">
  <Build Backend="CMake" Mode="FindPackage" Provider="vcpkg" ProviderPackage="fmt" ProviderVersion="10.2.1" CMakePackage="fmt" Linkage="Static;Shared" RuntimeDeployment="PackageRuntimeLibraries" RuntimeArtifacts="fmt">
    <Options>
      <Option Name="FMT_PROVIDER_ENABLED" Value="ON" />
    </Options>
  </Build>
  <Library Name="fmt">
    <Exports>
      <LibraryTarget Name="fmt::fmt" />
    </Exports>
  </Library>
</Package>
)xml");
  WriteFile(temp.path() / "Packages/zlib/zlib.nginpkg",
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="zlib" Version="1.3.1">
  <Build Backend="CMake" Mode="FindPackage" Provider="conan" ProviderPackage="zlib" ProviderVersion="1.3.1" CMakePackage="ZLIB" />
  <Library Name="zlib">
    <Exports>
      <LibraryTarget Name="ZLIB::ZLIB" />
    </Exports>
  </Library>
</Package>
)xml");
  WriteFile(temp.path() / "App/App.nginproj",
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Provider.App" DefaultProfile="dev">
  <Application>
    <Uses>
      <Package Name="fmt" Version="[10.0.0,11.0.0)" Scope="Target" />
      <Package Name="zlib" Version="[1.0.0,2.0.0)" Scope="Target" />
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
  <Profile Name="dev">
    <Defaults>
      <Optimization Mode="Off" />
      <DebugSymbols Enabled="true" />
      <LinkTimeOptimization Enabled="false" />
    </Defaults>
  </Profile>
</Project>
)xml");
  WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

  ParsedArgs restoreArgs{};
  restoreArgs.projectPath = (temp.path() / "App/App.nginproj").string();
  restoreArgs.eventOutputMode = EventOutputMode::JsonLines;
  restoreArgs.backendOutputMode = BackendOutputMode::Compact;

  std::ostringstream restoreOutput{};
  auto *previous = std::cout.rdbuf(restoreOutput.rdbuf());
  const auto restoreExitCode = CmdRestore(temp.path(), restoreArgs);
  std::cout.rdbuf(previous);

  REQUIRE(restoreExitCode == 0);
  REQUIRE_THAT(restoreOutput.str(), !ContainsSubstring("raw-vcpkg-output"));
  REQUIRE_THAT(restoreOutput.str(), !ContainsSubstring("raw-conan-output"));
  std::istringstream restoreLines{restoreOutput.str()};
  std::string line{};
  while (std::getline(restoreLines, line)) {
    REQUIRE(line.rfind(R"({"schemaVersion":"1.0","kind":"NGIN.CLI.Event")",
                       0) == 0);
  }

  REQUIRE_THAT(ReadFile(temp.path() / "vcpkg.args"),
               ContainsSubstring("--x-manifest-root"));
  REQUIRE_THAT(ReadFile(temp.path() / "vcpkg.args"),
               ContainsSubstring("--triplet"));
  REQUIRE_THAT(ReadFile(temp.path() / "conan.args"),
               ContainsSubstring("--output-folder"));
  REQUIRE_THAT(ReadFile(temp.path() / "conan.args"),
               ContainsSubstring("--profile"));
  REQUIRE_THAT(ReadFile(vcpkgStateRoot / "vcpkg.json"),
               ContainsSubstring(R"("fmt")"));
  REQUIRE_THAT(
      ReadFile(temp.path() / ".ngin/providers/conan/dev/conanfile.txt"),
      ContainsSubstring("zlib/1.3.1"));
  REQUIRE_THAT(
      ReadFile(temp.path() / ".ngin/providers/vcpkg/dev/ngin-provider.xml"),
      ContainsSubstring(R"(ToolchainFile=)"));
  REQUIRE_THAT(
      ReadFile(temp.path() / ".ngin/providers/conan/dev/ngin-provider.xml"),
      ContainsSubstring(R"(PrefixPath=)"));
  REQUIRE_THAT(ReadFile(temp.path() / "ngin.lock"),
               ContainsSubstring(R"(Provider="vcpkg")"));
  REQUIRE_THAT(ReadFile(temp.path() / "ngin.lock"),
               ContainsSubstring(R"(ProviderKind="Conan")"));
  REQUIRE_THAT(ReadFile(temp.path() / "ngin.lock"),
               ContainsSubstring(R"(Linkage="Static;Shared")"));
  REQUIRE_THAT(ReadFile(temp.path() / "ngin.lock"),
               ContainsSubstring(R"(RuntimeDeployment="PackageRuntimeLibraries")"));
  REQUIRE_THAT(ReadFile(temp.path() / "ngin.lock"),
               ContainsSubstring(R"(RuntimeArtifacts="fmt")"));

  ParsedArgs configureArgs{};
  configureArgs.projectPath = (temp.path() / "App/App.nginproj").string();
  configureArgs.outputPath = (temp.path() / "out").string();
  REQUIRE(CmdConfigure(temp.path(), configureArgs) == 0);

  const auto generatedCMake =
      ReadFile(temp.path() / "out/.ngin/cmake-src/CMakeLists.txt");
  REQUIRE_THAT(generatedCMake,
               ContainsSubstring(
                   R"(set(FMT_PROVIDER_ENABLED "ON" CACHE BOOL "" FORCE))"));
  REQUIRE_THAT(generatedCMake,
               ContainsSubstring(R"(find_package("fmt" CONFIG QUIET))"));
  REQUIRE_THAT(generatedCMake,
               ContainsSubstring(R"(find_package("ZLIB" CONFIG QUIET))"));
}

TEST_CASE("provider package metadata without provider binding does not run "
          "external restore") {
  TempDir temp{};
  WriteFile(temp.path() / "Workspace.ngin",
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="ProviderMetadataOnly">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
    <Provider Name="vcpkg" Kind="Vcpkg" Root="missing-vcpkg-root" Triplet="x64-linux" />
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
         CMakePackage="OpenSSL">
    <Options>
      <Option Name="NGIN_BASE_CRYPTO_WITH_OPENSSL" Value="ON" />
    </Options>
  </Build>
  <Library Name="OpenSSL.Crypto">
    <Exports>
      <LibraryTarget Name="OpenSSL::Crypto" />
    </Exports>
  </Library>
</Package>
)xml");
  WriteFile(temp.path() / "App/App.nginproj",
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="ProviderMetadataOnly.App">
  <Application>
    <Uses>
      <Package Name="OpenSSL" Version="[3.0.0,4.0.0)" Scope="Target" />
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
  args.colorMode = OutputColorMode::Never;

  std::ostringstream captured{};
  auto *previous = std::cout.rdbuf(captured.rdbuf());
  const auto exitCode = CmdRestore(temp.path(), args);
  std::cout.rdbuf(previous);

  REQUIRE(exitCode == 0);
  REQUIRE_FALSE(fs::exists(temp.path() / ".ngin/providers/vcpkg"));
  REQUIRE_THAT(ReadFile(temp.path() / "ngin.lock"),
               ContainsSubstring(R"(Package Name="OpenSSL")"));
  REQUIRE_THAT(ReadFile(temp.path() / "ngin.lock"),
               !ContainsSubstring(R"(Provider="vcpkg")"));
}

TEST_CASE(
    "package graph exposes provider build metadata without provider binding") {
  TempDir temp{};
  WriteFile(temp.path() / "Workspace.ngin",
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="ProviderBuildMetadata">
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
  <Library Name="OpenSSL.Crypto">
    <Exports>
      <LibraryTarget Name="OpenSSL::Crypto" />
    </Exports>
  </Library>
</Package>
)xml");
  WriteFile(temp.path() / "App/App.nginproj",
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="ProviderBuildMetadata.App">
  <Application>
    <Uses>
      <Package Name="OpenSSL" Version="[3.0.0,4.0.0)" Scope="Target" />
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
  args.graphPlan = "package";

  std::ostringstream captured{};
  auto *previous = std::cout.rdbuf(captured.rdbuf());
  const auto exitCode = CmdGraph(temp.path(), args);
  std::cout.rdbuf(previous);

  REQUIRE(exitCode == 0);
  REQUIRE_THAT(captured.str(),
               ContainsSubstring("build-metadata linkage=Static;Shared"));
  REQUIRE_THAT(captured.str(),
               ContainsSubstring("runtimeDeployment=PackageRuntimeLibraries"));
  REQUIRE_THAT(captured.str(), ContainsSubstring("runtimeArtifacts=libcrypto"));
}

TEST_CASE("configure fails for provider package before restore") {
  TempDir temp{};
  WriteFile(temp.path() / "Workspace.ngin",
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="MissingRestore">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
    <Provider Name="vcpkg" Kind="Vcpkg" Root="Tools/vcpkg" Triplet="x64-linux" />
  </Packages>
</Workspace>
)xml");
  WriteFile(temp.path() / "Packages/fmt/fmt.nginpkg",
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="fmt" Version="10.2.1">
  <Build Backend="CMake" Mode="FindPackage" Provider="vcpkg" CMakePackage="fmt" />
  <Library Name="fmt">
    <Exports>
      <LibraryTarget Name="fmt::fmt" />
    </Exports>
  </Library>
</Package>
)xml");
  WriteFile(temp.path() / "App/App.nginproj",
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="MissingRestore.App">
  <Application>
    <Uses>
      <Package Name="fmt" Version="[10.0.0,11.0.0)" Scope="Target" />
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
  args.outputPath = (temp.path() / "out").string();

  std::ostringstream captured{};
  auto *previous = std::cout.rdbuf(captured.rdbuf());
  const auto exitCode = CmdConfigure(temp.path(), args);
  std::cout.rdbuf(previous);

  REQUIRE(exitCode == 1);
  REQUIRE_THAT(captured.str(),
               ContainsSubstring("Run `ngin restore` before configure/build"));
}
#endif

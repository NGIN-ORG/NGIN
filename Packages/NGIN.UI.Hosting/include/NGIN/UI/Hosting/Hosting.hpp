#pragma once

#include <NGIN/Core/Core.hpp>
#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/UI/UI.hpp>
#include <NGIN/Utilities/Callable.hpp>

#include <chrono>
#include <memory>
#include <thread>

namespace NGIN::UI::Hosting {
inline constexpr auto UIApplicationServiceName = "NGIN.UI.IApplication";
inline constexpr auto UIWindowManagerServiceName = "NGIN.UI.IWindowManager";
inline constexpr auto UIDispatcherServiceName = "NGIN.UI.IUIDispatcher";
inline constexpr auto UIPlatformBackendServiceName = "NGIN.UI.IPlatformBackend";
inline constexpr auto UIRenderBackendServiceName = "NGIN.UI.IRenderBackend";
inline constexpr auto UIRuntimeModuleName = "NGIN.UI.Runtime";

struct UIHostingCreateInfo final {
  ApplicationCreateInfo application{};
  NativeTextCreateInfo text{};
  std::chrono::milliseconds maximumWait{250};
};

class HostedUIRuntime final {
public:
  [[nodiscard]] static auto Create(UIHostingCreateInfo info) noexcept
      -> UIResult<NGIN::Memory::Shared<HostedUIRuntime>>;

  HostedUIRuntime(std::unique_ptr<Application> application,
                  std::unique_ptr<NativeTextSystem> text) noexcept;

  [[nodiscard]] auto UI() noexcept -> Application &;
  [[nodiscard]] auto Text() noexcept -> NativeTextSystem &;

private:
  std::unique_ptr<Application> m_application{};
  std::unique_ptr<NativeTextSystem> m_text{};
};

class PlatformBackendReference final {
public:
  explicit PlatformBackendReference(IPlatformBackend &backend) noexcept;
  [[nodiscard]] auto Get() const noexcept -> IPlatformBackend &;

private:
  IPlatformBackend *m_backend{nullptr};
};

class RenderBackendReference final {
public:
  explicit RenderBackendReference(IRenderBackend &backend) noexcept;
  [[nodiscard]] auto Get() const noexcept -> IRenderBackend &;

private:
  IRenderBackend *m_backend{nullptr};
};

class IUIDispatcher {
public:
  virtual ~IUIDispatcher() = default;

  virtual auto Post(NGIN::Utilities::Callable<void()> callback) noexcept
      -> Core::CoreResult<void> = 0;
  virtual auto Drain() noexcept -> Core::CoreResult<void> = 0;
  virtual void Wake() noexcept = 0;
  virtual void BindCurrentThread() noexcept = 0;
  [[nodiscard]] virtual auto IsCurrentThread() const noexcept -> bool = 0;
};

class UIDispatcher final : public IUIDispatcher {
public:
  explicit UIDispatcher(NGIN::Memory::Shared<HostedUIRuntime> runtime);
  ~UIDispatcher() override;

  auto Post(NGIN::Utilities::Callable<void()> callback) noexcept
      -> Core::CoreResult<void> override;
  auto Drain() noexcept -> Core::CoreResult<void> override;
  void Wake() noexcept override;
  void BindCurrentThread() noexcept override;
  [[nodiscard]] auto IsCurrentThread() const noexcept -> bool override;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

class UIHostRunLoop final : public Core::IHostRunLoop {
public:
  UIHostRunLoop(NGIN::Memory::Shared<HostedUIRuntime> runtime,
                NGIN::Memory::Shared<IUIDispatcher> dispatcher,
                std::chrono::milliseconds maximumWait) noexcept;

  auto Run(Core::IApplicationHost &host) noexcept
      -> Core::CoreResult<void> override;
  void Wake() noexcept override;

private:
  NGIN::Memory::Shared<HostedUIRuntime> m_runtime{};
  NGIN::Memory::Shared<IUIDispatcher> m_dispatcher{};
  std::chrono::milliseconds m_maximumWait{250};
};

class UIModule final : public Core::IModule {
public:
  UIModule(NGIN::Memory::Shared<HostedUIRuntime> runtime,
           NGIN::Memory::Shared<IUIDispatcher> dispatcher) noexcept;

  auto OnRegister(Core::ModuleContext &context) noexcept
      -> Core::CoreResult<void> override;
  auto OnStart(Core::ModuleContext &context) noexcept
      -> Core::CoreResult<void> override;
  auto OnStop(Core::ModuleContext &context) noexcept
      -> Core::CoreResult<void> override;
  auto OnShutdown(Core::ModuleContext &context) noexcept
      -> Core::CoreResult<void> override;

private:
  NGIN::Memory::Shared<HostedUIRuntime> m_runtime{};
  NGIN::Memory::Shared<IUIDispatcher> m_dispatcher{};
};

struct UIHostingRegistration final {
  NGIN::Memory::Shared<HostedUIRuntime> runtime{};
  NGIN::Memory::Shared<IUIDispatcher> dispatcher{};
  std::shared_ptr<UIHostRunLoop> runLoop{};
};

[[nodiscard]] auto ConfigureUIHosting(Core::ApplicationBuilder &builder,
                                      UIHostingCreateInfo info) noexcept
    -> UIResult<UIHostingRegistration>;
} // namespace NGIN::UI::Hosting

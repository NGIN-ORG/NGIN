#pragma once

/// @file Errors.hpp
/// @brief Structured error model for core host operations.

#include <NGIN/Primitives.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace NGIN::Core
{
    /// @brief Core host error code catalog.
    enum class KernelErrorCode : NGIN::UInt16
    {
        None = 0,
        InvalidArgument,
        InvalidState,
        NotFound,
        AlreadyExists,
        IncompatibleHostType,
        IncompatiblePlatform,
        IncompatibleVersion,
        ReflectionRequired,
        MissingRequiredDependency,
        DependencyCycle,
        StageOrderingViolation,
        LayerConstraintViolation,
        ModuleFactoryFailure,
        ModuleLifecycleFailure,
        ServiceRegistrationFailure,
        EventDispatchFailure,
        TaskSubmissionFailure,
        ConfigFailure,
        DynamicPluginUnsupported,
        ThreadPolicyViolation,
        SchemaValidationFailure,
        InternalError
    };

    struct KernelError;

    /// @brief Structured core host error payload.
    struct KernelError
    {
        KernelErrorCode                     code {KernelErrorCode::None};
        std::string                         subsystem {};
        std::string                         module {};
        std::string                         message {};
        std::string                         dependencyPath {};
        std::shared_ptr<const KernelError>  cause {};
    };

    template<typename T>
    using CoreResult = NGIN::Utilities::Expected<T, KernelError>;

    [[nodiscard]] inline auto MakeKernelError(
        const KernelErrorCode code,
        std::string subsystem,
        std::string module,
        std::string message,
        std::string dependencyPath = {},
        std::shared_ptr<const KernelError> cause = {}) noexcept -> KernelError
    {
        return KernelError {
            .code = code,
            .subsystem = std::move(subsystem),
            .module = std::move(module),
            .message = std::move(message),
            .dependencyPath = std::move(dependencyPath),
            .cause = std::move(cause)};
    }

    [[nodiscard]] constexpr auto ToString(const KernelErrorCode value) noexcept -> std::string_view
    {
        switch (value)
        {
            case KernelErrorCode::None: return "None";
            case KernelErrorCode::InvalidArgument: return "InvalidArgument";
            case KernelErrorCode::InvalidState: return "InvalidState";
            case KernelErrorCode::NotFound: return "NotFound";
            case KernelErrorCode::AlreadyExists: return "AlreadyExists";
            case KernelErrorCode::IncompatibleHostType: return "IncompatibleHostType";
            case KernelErrorCode::IncompatiblePlatform: return "IncompatiblePlatform";
            case KernelErrorCode::IncompatibleVersion: return "IncompatibleVersion";
            case KernelErrorCode::ReflectionRequired: return "ReflectionRequired";
            case KernelErrorCode::MissingRequiredDependency: return "MissingRequiredDependency";
            case KernelErrorCode::DependencyCycle: return "DependencyCycle";
            case KernelErrorCode::StageOrderingViolation: return "StageOrderingViolation";
            case KernelErrorCode::LayerConstraintViolation: return "LayerConstraintViolation";
            case KernelErrorCode::ModuleFactoryFailure: return "ModuleFactoryFailure";
            case KernelErrorCode::ModuleLifecycleFailure: return "ModuleLifecycleFailure";
            case KernelErrorCode::ServiceRegistrationFailure: return "ServiceRegistrationFailure";
            case KernelErrorCode::EventDispatchFailure: return "EventDispatchFailure";
            case KernelErrorCode::TaskSubmissionFailure: return "TaskSubmissionFailure";
            case KernelErrorCode::ConfigFailure: return "ConfigFailure";
            case KernelErrorCode::DynamicPluginUnsupported: return "DynamicPluginUnsupported";
            case KernelErrorCode::ThreadPolicyViolation: return "ThreadPolicyViolation";
            case KernelErrorCode::SchemaValidationFailure: return "SchemaValidationFailure";
            case KernelErrorCode::InternalError: return "InternalError";
        }
        return "Unknown";
    }
}

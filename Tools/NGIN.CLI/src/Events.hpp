#pragma once

#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace NGIN::CLI
{
    enum class EventOutputMode
    {
        None,
        JsonLines,
    };

    enum class CliEventType
    {
        CommandStarted,
        CommandSelection,
        PhaseStarted,
        PhaseCompleted,
        PhaseFailed,
        BackendOutput,
        ToolRunStarted,
        ToolProgress,
        Diagnostic,
        EditProposed,
        Metric,
        GateEvaluated,
        CacheStatus,
        ToolRunCompleted,
        ArtifactProduced,
        Summary,
        CommandCompleted,
    };

    [[nodiscard]] auto CliEventTypeName(CliEventType type) -> std::string_view;

    class EventData
    {
    public:
        using Value = std::variant<std::nullptr_t, std::string, std::int64_t, double, bool, std::vector<std::string>>;

        auto AddNull(std::string name) -> EventData &;
        auto AddString(std::string name, std::string value) -> EventData &;
        auto AddNumber(std::string name, std::int64_t value) -> EventData &;
        auto AddDecimal(std::string name, double value) -> EventData &;
        auto AddBool(std::string name, bool value) -> EventData &;
        auto AddStringArray(std::string name, std::vector<std::string> value) -> EventData &;

        [[nodiscard]] auto Empty() const -> bool;
        [[nodiscard]] auto String(std::string_view name) const -> std::optional<std::string>;
        [[nodiscard]] auto Number(std::string_view name) const -> std::optional<std::int64_t>;
        [[nodiscard]] auto Bool(std::string_view name) const -> std::optional<bool>;
        [[nodiscard]] auto StringArray(std::string_view name) const -> std::optional<std::vector<std::string>>;
        auto WriteJson(std::ostream &out) const -> void;

    private:
        struct Field
        {
            std::string name;
            Value value;
        };

        std::vector<Field> fields_{};
    };

    struct CliEvent
    {
        std::uint64_t sequence{};
        std::chrono::system_clock::time_point timestamp{};
        CliEventType type{};
        std::string command{};
        std::string project{};
        std::string profile{};
        EventData data{};
    };

    class ICliEventSink
    {
    public:
        virtual ~ICliEventSink() = default;
        virtual auto Emit(const CliEvent &event) -> void = 0;
    };

    class NullCliEventSink final : public ICliEventSink
    {
    public:
        auto Emit(const CliEvent &event) -> void override;
    };

    class JsonLinesCliEventSink final : public ICliEventSink
    {
    public:
        explicit JsonLinesCliEventSink(std::ostream &out);
        auto Emit(const CliEvent &event) -> void override;

    private:
        std::ostream *out_{};
    };

    class RecordingCliEventSink final : public ICliEventSink
    {
    public:
        auto Emit(const CliEvent &event) -> void override;
        [[nodiscard]] auto Events() const -> const std::vector<CliEvent> &;

    private:
        std::vector<CliEvent> events_{};
    };

    class CompositeCliEventSink final : public ICliEventSink
    {
    public:
        auto Add(ICliEventSink &sink) -> void;
        auto Emit(const CliEvent &event) -> void override;

    private:
        std::vector<ICliEventSink *> sinks_{};
    };

    class CliEventEmitter
    {
    public:
        explicit CliEventEmitter(ICliEventSink *sink = nullptr);

        auto SetCommand(std::string command) -> void;
        auto SetSelection(std::string project, std::string profile) -> void;
        [[nodiscard]] auto Command() const -> const std::string &;
        [[nodiscard]] auto Project() const -> const std::string &;
        [[nodiscard]] auto Profile() const -> const std::string &;
        [[nodiscard]] auto Enabled() const -> bool;

        auto Emit(CliEventType type, EventData data = {}) -> void;

    private:
        mutable std::mutex mutex_{};
        ICliEventSink *sink_{};
        std::uint64_t nextSequence_{1};
        std::string command_{};
        std::string project_{};
        std::string profile_{};
    };

    [[nodiscard]] auto JsonString(const std::string &value) -> std::string;
}

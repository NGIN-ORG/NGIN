#include "Events.hpp"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <type_traits>

namespace NGIN::CLI
{
    namespace
    {
        [[nodiscard]] auto FormatTimestamp(std::chrono::system_clock::time_point value) -> std::string
        {
            const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(value);
            const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(value - seconds).count();
            const auto time = std::chrono::system_clock::to_time_t(seconds);

            std::tm utc{};
#if defined(_WIN32)
            gmtime_s(&utc, &time);
#else
            gmtime_r(&time, &utc);
#endif

            std::ostringstream out{};
            out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S")
                << "." << std::setw(3) << std::setfill('0') << millis << "Z";
            return out.str();
        }

        auto WriteJsonValue(std::ostream &out, const EventData::Value &value) -> void
        {
            std::visit(
                [&out](const auto &typed)
                {
                    using T = std::decay_t<decltype(typed)>;
                    if constexpr (std::is_same_v<T, std::nullptr_t>)
                    {
                        out << "null";
                    }
                    else if constexpr (std::is_same_v<T, std::string>)
                    {
                        out << JsonString(typed);
                    }
                    else if constexpr (std::is_same_v<T, std::int64_t>)
                    {
                        out << typed;
                    }
                    else if constexpr (std::is_same_v<T, double>)
                    {
                        out << typed;
                    }
                    else if constexpr (std::is_same_v<T, bool>)
                    {
                        out << (typed ? "true" : "false");
                    }
                    else if constexpr (std::is_same_v<T, std::vector<std::string>>)
                    {
                        out << "[";
                        for (std::size_t index = 0; index < typed.size(); ++index)
                        {
                            if (index != 0)
                            {
                                out << ",";
                            }
                            out << JsonString(typed[index]);
                        }
                        out << "]";
                    }
                },
                value);
        }
    }

    auto CliEventTypeName(CliEventType type) -> std::string_view
    {
        switch (type)
        {
        case CliEventType::CommandStarted: return "command.started";
        case CliEventType::CommandSelection: return "command.selection";
        case CliEventType::PhaseStarted: return "phase.started";
        case CliEventType::PhaseCompleted: return "phase.completed";
        case CliEventType::PhaseFailed: return "phase.failed";
        case CliEventType::BackendOutput: return "backend.output";
        case CliEventType::ToolRunStarted: return "tool.run.started";
        case CliEventType::ToolProgress: return "tool.progress";
        case CliEventType::Diagnostic: return "diagnostic";
        case CliEventType::EditProposed: return "edit.proposed";
        case CliEventType::Metric: return "metric";
        case CliEventType::GateEvaluated: return "gate.evaluated";
        case CliEventType::CacheStatus: return "tool.cache";
        case CliEventType::ToolRunCompleted: return "tool.run.completed";
        case CliEventType::ArtifactProduced: return "artifact.produced";
        case CliEventType::Summary: return "summary";
        case CliEventType::CommandCompleted: return "command.completed";
        }
        return "unknown";
    }

    auto EventData::AddNull(std::string name) -> EventData &
    {
        fields_.push_back(Field{.name = std::move(name), .value = nullptr});
        return *this;
    }

    auto EventData::AddString(std::string name, std::string value) -> EventData &
    {
        fields_.push_back(Field{.name = std::move(name), .value = std::move(value)});
        return *this;
    }

    auto EventData::AddNumber(std::string name, std::int64_t value) -> EventData &
    {
        fields_.push_back(Field{.name = std::move(name), .value = value});
        return *this;
    }

    auto EventData::AddDecimal(std::string name, double value) -> EventData &
    {
        fields_.push_back(Field{.name = std::move(name), .value = value});
        return *this;
    }

    auto EventData::AddBool(std::string name, bool value) -> EventData &
    {
        fields_.push_back(Field{.name = std::move(name), .value = value});
        return *this;
    }

    auto EventData::AddStringArray(std::string name, std::vector<std::string> value) -> EventData &
    {
        fields_.push_back(Field{.name = std::move(name), .value = std::move(value)});
        return *this;
    }

    auto EventData::Empty() const -> bool
    {
        return fields_.empty();
    }

    auto EventData::String(std::string_view name) const -> std::optional<std::string>
    {
        for (const auto &field : fields_)
        {
            if (field.name == name)
            {
                if (const auto *value = std::get_if<std::string>(&field.value); value != nullptr)
                {
                    return *value;
                }
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    auto EventData::Number(std::string_view name) const -> std::optional<std::int64_t>
    {
        for (const auto &field : fields_)
        {
            if (field.name == name)
            {
                if (const auto *value = std::get_if<std::int64_t>(&field.value); value != nullptr)
                {
                    return *value;
                }
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    auto EventData::Bool(std::string_view name) const -> std::optional<bool>
    {
        for (const auto &field : fields_)
        {
            if (field.name == name)
            {
                if (const auto *value = std::get_if<bool>(&field.value); value != nullptr)
                {
                    return *value;
                }
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    auto EventData::StringArray(std::string_view name) const -> std::optional<std::vector<std::string>>
    {
        for (const auto &field : fields_)
        {
            if (field.name == name)
            {
                if (const auto *value = std::get_if<std::vector<std::string>>(&field.value); value != nullptr)
                {
                    return *value;
                }
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    auto EventData::WriteJson(std::ostream &out) const -> void
    {
        out << "{";
        for (std::size_t index = 0; index < fields_.size(); ++index)
        {
            if (index != 0)
            {
                out << ",";
            }
            out << JsonString(fields_[index].name) << ":";
            WriteJsonValue(out, fields_[index].value);
        }
        out << "}";
    }

    auto NullCliEventSink::Emit(const CliEvent &event) -> void
    {
        (void)event;
    }

    JsonLinesCliEventSink::JsonLinesCliEventSink(std::ostream &out) : out_(&out)
    {
    }

    auto JsonLinesCliEventSink::Emit(const CliEvent &event) -> void
    {
        if (out_ == nullptr)
        {
            return;
        }

        *out_ << "{\"schemaVersion\":\"1.0\","
              << "\"kind\":\"NGIN.CLI.Event\","
              << "\"sequence\":" << event.sequence << ","
              << "\"timestamp\":" << JsonString(FormatTimestamp(event.timestamp)) << ","
              << "\"type\":" << JsonString(std::string{CliEventTypeName(event.type)});
        if (!event.command.empty())
        {
            *out_ << ",\"command\":" << JsonString(event.command);
        }
        if (!event.project.empty())
        {
            *out_ << ",\"project\":" << JsonString(event.project);
        }
        if (!event.profile.empty())
        {
            *out_ << ",\"profile\":" << JsonString(event.profile);
        }
        *out_ << ",\"data\":";
        event.data.WriteJson(*out_);
        *out_ << "}\n" << std::flush;
    }

    auto RecordingCliEventSink::Emit(const CliEvent &event) -> void
    {
        events_.push_back(event);
    }

    auto RecordingCliEventSink::Events() const -> const std::vector<CliEvent> &
    {
        return events_;
    }

    auto CompositeCliEventSink::Add(ICliEventSink &sink) -> void
    {
        sinks_.push_back(&sink);
    }

    auto CompositeCliEventSink::Emit(const CliEvent &event) -> void
    {
        for (auto *sink : sinks_)
        {
            if (sink != nullptr)
            {
                sink->Emit(event);
            }
        }
    }

    CliEventEmitter::CliEventEmitter(ICliEventSink *sink) : sink_(sink)
    {
    }

    auto CliEventEmitter::SetCommand(std::string command) -> void
    {
        command_ = std::move(command);
    }

    auto CliEventEmitter::SetSelection(std::string project, std::string profile) -> void
    {
        project_ = std::move(project);
        profile_ = std::move(profile);
    }

    auto CliEventEmitter::Command() const -> const std::string &
    {
        return command_;
    }

    auto CliEventEmitter::Project() const -> const std::string &
    {
        return project_;
    }

    auto CliEventEmitter::Profile() const -> const std::string &
    {
        return profile_;
    }

    auto CliEventEmitter::Enabled() const -> bool
    {
        return sink_ != nullptr;
    }

    auto CliEventEmitter::Emit(CliEventType type, EventData data) -> void
    {
        const std::lock_guard lock{mutex_};
        if (sink_ == nullptr)
        {
            return;
        }
        sink_->Emit(CliEvent{
            .sequence = nextSequence_++,
            .timestamp = std::chrono::system_clock::now(),
            .type = type,
            .command = command_,
            .project = project_,
            .profile = profile_,
            .data = std::move(data),
        });
    }

    auto JsonString(const std::string &value) -> std::string
    {
        std::string escaped{"\""};
        for (const unsigned char ch : value)
        {
            switch (ch)
            {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (ch < 0x20)
                {
                    constexpr char hex[] = "0123456789abcdef";
                    escaped += "\\u00";
                    escaped += hex[(ch >> 4) & 0x0f];
                    escaped += hex[ch & 0x0f];
                }
                else
                {
                    escaped += static_cast<char>(ch);
                }
                break;
            }
        }
        escaped += "\"";
        return escaped;
    }
}

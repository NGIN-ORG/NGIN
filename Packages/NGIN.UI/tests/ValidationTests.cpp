#include <catch2/catch_test_macros.hpp>

#include <NGIN/Async/Completion.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/UI/Command.hpp>
#include <NGIN/UI/Validation.hpp>

namespace {
using NGIN::UI::ValidationIssue;
using NGIN::UI::ValidationSeverity;

[[nodiscard]] auto
Issue(const char *id, const char *field, const char *message,
      const ValidationSeverity severity = ValidationSeverity::Error)
    -> ValidationIssue {
  return ValidationIssue{
      .id = NGIN::Text::String{id},
      .field = NGIN::Text::String{field},
      .message = NGIN::Text::String{message},
      .severity = severity,
  };
}

[[nodiscard]] auto ValidateNameAsync(NGIN::Async::TaskContext &context,
                                     NGIN::Text::String value)
    -> NGIN::Async::Task<std::vector<ValidationIssue>, ValidationIssue> {
  co_await context.YieldNow();
  if (value == NGIN::Text::String{"taken"}) {
    co_return std::vector{
        Issue("name-taken", "name", "That name is already in use")};
  }
  co_return std::vector<ValidationIssue>{};
}
} // namespace

TEST_CASE("validation fields support immediate and deferred policies") {
  using namespace NGIN::UI;

  State<int> age{10};
  ValidationField immediate{age.AsReadOnly(), ValidationTrigger::Immediate};
  immediate.AddSyncValidator([](const int value) {
    if (value < 18) {
      return std::vector{Issue("age-young", "age", "Must be 18 or older")};
    }
    return std::vector<ValidationIssue>{};
  });
  REQUIRE_FALSE(immediate.IsValid().Get());
  REQUIRE(immediate.Issues().Get().size() == 1);

  static_cast<void>(age.Set(21));
  REQUIRE(immediate.IsValid().Get());
  REQUIRE(immediate.Issues().Get().empty());

  State<int> score{2};
  ValidationField deferred{score.AsReadOnly(), ValidationTrigger::Deferred};
  deferred.AddSyncValidator([](const int value) {
    return value >= 5
               ? std::vector<ValidationIssue>{}
               : std::vector{Issue("score-low", "score", "Enter 5 or more")};
  });
  REQUIRE_FALSE(deferred.IsValid().Get());
  REQUIRE(deferred.Issues().Get().empty());
  deferred.Validate();
  REQUIRE(deferred.Issues().Get().size() == 1);
  static_cast<void>(score.Set(8));
  REQUIRE_FALSE(deferred.IsValid().Get());
  deferred.Validate();
  REQUIRE(deferred.IsValid().Get());
}

TEST_CASE("submit validation follows later edits") {
  using namespace NGIN::UI;

  State<int> count{0};
  ValidationField field{count.AsReadOnly(), ValidationTrigger::Submit};
  field.AddSyncValidator([](const int value) {
    return value > 0
               ? std::vector<ValidationIssue>{}
               : std::vector{Issue("count-required", "count", "Enter a count")};
  });
  REQUIRE(field.Issues().Get().empty());
  field.Validate();
  REQUIRE(field.Issues().Get().size() == 1);
  static_cast<void>(count.Set(2));
  REQUIRE(field.IsValid().Get());
}

TEST_CASE("stale asynchronous validation cannot replace newer input") {
  using namespace NGIN::UI;

  NGIN::Execution::CooperativeScheduler scheduler;
  NGIN::Async::TaskContext context{scheduler};
  State<NGIN::Text::String> name{NGIN::Text::String{"taken"}};
  ValidationField field{name.AsReadOnly(), ValidationTrigger::Immediate};
  field.SetAsyncValidator(context, ValidateNameAsync);

  REQUIRE(field.IsValidating().Get());
  static_cast<void>(name.Set(NGIN::Text::String{"available"}));
  scheduler.RunUntilIdle();

  REQUIRE_FALSE(field.IsValidating().Get());
  REQUIRE(field.IsValid().Get());
  REQUIRE(field.Issues().Get().empty());
}

TEST_CASE("canceling asynchronous validation keeps the field unavailable") {
  using namespace NGIN::UI;

  NGIN::Execution::CooperativeScheduler scheduler;
  NGIN::Async::TaskContext context{scheduler};
  State<NGIN::Text::String> name{NGIN::Text::String{"available"}};
  ValidationField field{name.AsReadOnly(), ValidationTrigger::Immediate};
  field.SetAsyncValidator(context, ValidateNameAsync);

  REQUIRE(field.IsValidating().Get());
  field.Cancel();
  REQUIRE_FALSE(field.IsValidating().Get());
  REQUIRE_FALSE(field.IsValid().Get());
  scheduler.RunUntilIdle();
  REQUIRE(field.Issues().Get().empty());
}

TEST_CASE("form summaries preserve field and validator issue order") {
  using namespace NGIN::UI;

  State<int> firstValue{0};
  State<int> secondValue{0};
  ValidationField first{firstValue.AsReadOnly(), ValidationTrigger::Immediate};
  ValidationField second{secondValue.AsReadOnly(),
                         ValidationTrigger::Immediate};
  first.AddSyncValidator([](const int &) {
    return std::vector{
        Issue("first-a", "first", "First A"),
        Issue("first-b", "first", "First B", ValidationSeverity::Warning)};
  });
  second.AddSyncValidator([](const int &) {
    return std::vector{Issue("second-a", "second", "Second A")};
  });
  ValidationForm form{{first.AsBinding(), second.AsBinding()}};

  REQUIRE(form.Summary().Get().size() == 3);
  CHECK(form.Summary().Get()[0].id == NGIN::Text::String{"first-a"});
  CHECK(form.Summary().Get()[1].id == NGIN::Text::String{"first-b"});
  CHECK(form.Summary().Get()[2].id == NGIN::Text::String{"second-a"});
  REQUIRE_FALSE(form.IsValid().Get());
}

TEST_CASE("derived form validity can gate a command without ownership cycles") {
  using namespace NGIN::UI;

  State<int> amount{0};
  ValidationField field{amount.AsReadOnly(), ValidationTrigger::Immediate};
  field.AddSyncValidator([](const int value) {
    return value > 0 ? std::vector<ValidationIssue>{}
                     : std::vector{Issue("amount-required", "amount",
                                         "Enter an amount")};
  });
  ValidationForm form{{field.AsBinding()}};
  int executions = 0;
  Command save{[&] { ++executions; }};
  save.BindEnabled(form.IsValid());

  REQUIRE_FALSE(save.Status().canExecute);
  static_cast<void>(amount.Set(10));
  REQUIRE(save.Status().canExecute);
  REQUIRE(save.Execute() == CommandInvocation::Started);
  REQUIRE(executions == 1);
}

TEST_CASE("destroying validation ignores late asynchronous completion") {
  using namespace NGIN::UI;

  NGIN::Execution::CooperativeScheduler scheduler;
  NGIN::Async::TaskContext context{scheduler};
  State<NGIN::Text::String> name{NGIN::Text::String{"taken"}};
  auto observed = std::vector<ValidationIssue>{};
  {
    ValidationField field{name.AsReadOnly(), ValidationTrigger::Immediate};
    field.SetAsyncValidator(context, ValidateNameAsync);
    observed = field.Issues().Get();
  }
  scheduler.RunUntilIdle();
  REQUIRE(observed.empty());
}

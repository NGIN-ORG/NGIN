#include "Player.hpp"

#include <NGIN/Reflection/Reflection.hpp>

#include <cstdint>
#include <iostream>
#include <span>
#include <string>

int main() {
  auto type = NGIN::Reflection::GetType("Demo::Player");
  if (!type.has_value()) {
    std::cerr << "reflection type lookup failed\n";
    return 1;
  }

  auto reflectedPlayer = type->Construct();
  if (!reflectedPlayer.has_value()) {
    std::cerr << "reflection construction failed\n";
    return 1;
  }

  auto displayNameField = type->GetField("display_name");
  auto privateStateField = type->GetField("private_state");
  auto scoreProperty = type->GetProperty("score");
  auto addExperienceMethod = type->GetMethod("add_experience");
  auto startingScoreMethod = type->GetMethod("starting_score");
  auto stateType = NGIN::Reflection::GetType("Demo::PlayerState");
  if (!displayNameField.has_value() || !privateStateField.has_value() ||
      !scoreProperty.has_value() || !addExperienceMethod.has_value() ||
      !startingScoreMethod.has_value() || !stateType.has_value() ||
      stateType->EnumValueCount() != 2 || type->ConstructorCount() != 1 ||
      type->MethodCount() != 2) {
    std::cerr << "reflection member lookup failed\n";
    return 1;
  }

  auto displayName = displayNameField->Read(*reflectedPlayer);
  auto score = scoreProperty->Read(*reflectedPlayer);
  if (!displayName.has_value() ||
      displayName->TryAs<std::string>() == nullptr || !score.has_value() ||
      score->TryAs<std::int64_t>() == nullptr) {
    std::cerr << "reflection member access failed\n";
    return 1;
  }

  NGIN::Reflection::Value updatedScore{std::int64_t{84}};
  if (!scoreProperty->Write(*reflectedPlayer, updatedScore).has_value()) {
    std::cerr << "reflection property write failed\n";
    return 1;
  }

  auto changedScore = scoreProperty->Read(*reflectedPlayer);
  if (!changedScore.has_value() ||
      changedScore->TryAs<std::int64_t>() == nullptr) {
    std::cerr << "reflection property reread failed\n";
    return 1;
  }

  const NGIN::Reflection::Value experienceArguments[]{
      NGIN::Reflection::Value{std::int64_t{5}}};
  auto addedExperience =
      addExperienceMethod->Invoke(*reflectedPlayer, experienceArguments);
  auto startingScore = startingScoreMethod->Invoke(
      *reflectedPlayer, std::span<const NGIN::Reflection::Value>{});
  if (!addedExperience.has_value() || !startingScore.has_value() ||
      startingScore->TryAs<std::int64_t>() == nullptr ||
      *startingScore->TryAs<std::int64_t>() != 70) {
    std::cerr << "reflection method invocation failed\n";
    return 1;
  }

  std::cout << "Reflected type: " << type->QualifiedName() << std::endl
            << "fields=" << type->FieldCount() << std::endl
            << "properties=" << type->PropertyCount() << std::endl
            << "methods=" << type->MethodCount() << std::endl
            << "constructors=" << type->ConstructorCount() << std::endl
            << "enum_values=" << stateType->EnumValueCount() << std::endl
            << "display_name=" << *displayName->TryAs<std::string>()
            << std::endl
            << "score=" << *score->TryAs<std::int64_t>() << std::endl
            << "updated_score=" << *changedScore->TryAs<std::int64_t>()
            << std::endl;

  return 0;
}

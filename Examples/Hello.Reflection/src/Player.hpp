#pragma once

#include <NGIN/Reflection/Annotations.hpp>

#include <string>
#include <string_view>

namespace Demo {
struct NGIN_REFLECT(Name = "Demo::Entity") Entity {
  // Public fields are reflected automatically.
  int id{7};
};

struct NGIN_REFLECT(Name = "Demo::Player",
                    Attributes = (Demo::Category = "Gameplay")) Player
    : Entity {
  NGIN_GENERATED_BODY()

  NGIN_FIELD(Name = "display_name", Attributes = (Serialize::Required = true))
  std::string displayName{"Ada"};

  NGIN_IGNORE
  int transientDebugCounter{0};

  NGIN_IGNORE
  int score{70};

private:
  NGIN_FIELD(Name = "private_state")
  int privateState{9};

public:
  NGIN_CTOR()
  Player() = default;

  NGIN_PROPERTY(Name = "score")
  int GetScore() const { return score; }

  NGIN_PROPERTY(Name = "score")
  void SetScore(int value) { score = value; }

  NGIN_METHOD(Name = "add_experience")
  void AddExperience(int) const & noexcept {}

  void AddExperience(std::string_view) {}

  NGIN_METHOD(Name = "starting_score")
  static int StartingScore() noexcept { return 70; }
};

enum class NGIN_REFLECT(Name = "Demo::PlayerState") PlayerState {
  Ready,
  NGIN_ENUM_VALUE(Name = "active") Active,
  NGIN_IGNORE Hidden,
};
} // namespace Demo

#include "Player.hpp"

#include <NGIN/Reflection/Reflection.hpp>

#include <cstdint>
#include <iostream>
#include <string>

int main()
{
    Demo::Player player{};

    auto type = NGIN::Reflection::GetType("Demo::Player");
    if (!type.has_value())
    {
        std::cerr << "reflection type lookup failed\n";
        return 1;
    }

    auto reflectedPlayer = type->Construct();
    if (!reflectedPlayer.has_value())
    {
        std::cerr << "reflection construction failed\n";
        return 1;
    }

    auto displayNameField = type->GetField("display_name");
    auto scoreMethod = type->GetMethod("score");
    if (!displayNameField.has_value() || !scoreMethod.has_value())
    {
        std::cerr << "reflection member lookup failed\n";
        return 1;
    }

    auto displayName = displayNameField->Read(*reflectedPlayer);
    auto score = scoreMethod->Invoke(*reflectedPlayer, {});
    if (!displayName.has_value() || displayName->TryAs<std::string>() == nullptr ||
        !score.has_value() || score->TryAs<std::int64_t>() == nullptr)
    {
        std::cerr << "reflection member access failed\n";
        return 1;
    }

    std::cout << "Reflected type: " << type->QualifiedName() << std::endl
              << "fields=" << type->FieldCount() << std::endl
              << "methods=" << type->MethodCount() << std::endl
              << "display_name=" << *displayName->TryAs<std::string>() << std::endl
              << "score=" << *score->TryAs<std::int64_t>() << std::endl;

    return 0;
}

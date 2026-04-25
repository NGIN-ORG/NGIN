#pragma once

#include <NGIN/MetaGen/Annotations.hpp>

#include <string>

namespace Demo
{
    struct NGIN_REFLECT(name = "Demo::Entity") Entity
    {
        NGIN_FIELD(name = "id")
        int id {7};
    };

    struct NGIN_REFLECT(name = "Demo::Player") Player : Entity
    {
        NGIN_FIELD(name = "display_name")
        std::string displayName {"Ada"};

        NGIN_IGNORE
        int transientDebugCounter {0};

        NGIN_CTOR()
        Player() = default;

        NGIN_METHOD(name = "score")
        int Score() const
        {
            return id * 10;
        }
    };
}

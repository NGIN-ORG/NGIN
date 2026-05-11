#include <iostream>
#include <string_view>

namespace
{
    int WarningProbe()
    {
        return 42;
    }

    constexpr std::string_view Message()
    {
        return "Hello.Analyzer running";
    }
}

int main()
{
    std::cout << Message() << " with analyzer probe " << WarningProbe() << "\n";
    return 0;
}

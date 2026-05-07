#include <iostream>

int main(int argc, char **argv)
{
    std::cout << "Hello.Native running";
    if (argc > 1)
    {
        std::cout << " with " << (argc - 1) << " argument(s)";
    }
    std::cout << "\n";
    return 0;
}

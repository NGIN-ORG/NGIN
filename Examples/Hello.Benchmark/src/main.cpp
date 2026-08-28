#include <chrono>

int main()
{
    const auto started = std::chrono::steady_clock::now();
    return std::chrono::steady_clock::now() < started ? 1 : 0;
}

#include <iostream>

int GameEngineValue();

int main()
{
    std::cout << "Game.Client using engine value " << GameEngineValue() << "\n";
    return GameEngineValue() == 42 ? 0 : 1;
}

#include <iostream>
#include <string_view>

namespace
{
constexpr std::string_view Message(){return "Hello.Formatter running";}
}

int main(){std::cout<<Message()<<'\n';return 0;}

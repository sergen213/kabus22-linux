#include "setup.hpp"
#include <iostream>
int main() {
    try { k22::setup::selfCheck(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

#include <iostream>

#include "app/BackprojComparison.hpp"

int main(int argc, char** argv) {
    return app::runBackprojComparison(argc, argv, std::cout, std::cerr);
}

#include "lib2.hpp"

int Lib2::getValueFromLib1() const {
    Lib1 lib1;
    return lib1.getValue();
}
#include "lib1.hpp"
#include "lib2.hpp"
#include "header.h"



extern "C" int getValueFromLib2() {
    Lib2 lib2;
    return lib2.getValueFromLib1();
}

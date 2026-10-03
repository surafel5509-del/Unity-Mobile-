#include "prism_test.h"
int main(int argc, char** argv) {
    return prism::test::run_all(argc > 1 ? argv[1] : nullptr);
}

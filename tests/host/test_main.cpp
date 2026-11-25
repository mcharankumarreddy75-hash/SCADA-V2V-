#include "test.h"

int g_checks = 0;
int g_case_failures = 0;

int main() {
    int passed = 0;
    int failed = 0;
    for (const TestEntry &t : test_registry()) {
        g_case_failures = 0;
        t.fn();
        if (g_case_failures == 0) {
            ++passed;
            std::printf("[PASS] %s\n", t.name);
        } else {
            ++failed;
            std::printf("[FAIL] %s (%d failed checks)\n", t.name, g_case_failures);
        }
    }
    std::printf("\n%d tests passed, %d failed, %d checks total\n", passed, failed, g_checks);
    return failed ? 1 : 0;
}

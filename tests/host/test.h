#pragma once
// Tiny single-header test framework: TEST() self-registers, CHECK* record failures.
#include <cmath>
#include <cstdio>
#include <vector>

struct TestEntry {
    const char *name;
    void (*fn)();
};

inline std::vector<TestEntry> &test_registry() {
    static std::vector<TestEntry> registry;
    return registry;
}

struct TestRegistrar {
    TestRegistrar(const char *name, void (*fn)()) {
        test_registry().push_back(TestEntry{name, fn});
    }
};

extern int g_checks;          // defined in test_main.cpp
extern int g_case_failures;

#define TEST(name)                                  \
    static void name();                             \
    static TestRegistrar reg_##name(#name, &name);  \
    static void name()

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_case_failures;                                                   \
            std::printf("    line %d: CHECK(%s) failed\n", __LINE__, #cond);     \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                           \
    do {                                                                         \
        ++g_checks;                                                              \
        long long va_ = static_cast<long long>(a);                               \
        long long vb_ = static_cast<long long>(b);                               \
        if (va_ != vb_) {                                                        \
            ++g_case_failures;                                                   \
            std::printf("    line %d: %s == %s failed (got %lld, want %lld)\n",  \
                        __LINE__, #a, #b, va_, vb_);                             \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                    \
    do {                                                                         \
        ++g_checks;                                                              \
        double va_ = static_cast<double>(a);                                     \
        double vb_ = static_cast<double>(b);                                     \
        if (!(std::fabs(va_ - vb_) <= static_cast<double>(tol))) {               \
            ++g_case_failures;                                                   \
            std::printf("    line %d: %s ~= %s failed (got %.6f, want %.6f)\n",  \
                        __LINE__, #a, #b, va_, vb_);                             \
        }                                                                        \
    } while (0)

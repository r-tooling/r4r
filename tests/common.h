#ifndef TESTS_COMMON_H
#define TESTS_COMMON_H

#ifdef CODE_COVERAGE_ENABLED
#define SKIP_ON_COVERAGE(message) GTEST_SKIP() << message
#else
#define SKIP_ON_COVERAGE(message) (void)0
#endif

#ifdef __aarch64__
#define SKIP_ON_AARCH64(message) GTEST_SKIP() << message
#else
#define SKIP_ON_AARCH64(message) (void)0
#endif

#define SKIP_ON_CI(message)                                                    \
    if (std::getenv("GITHUB_ACTIONS") != nullptr) {                            \
        GTEST_SKIP() << message;                                               \
    }

#define SKIP_ON_ROOT(message)                                                  \
    if (geteuid() == 0) {                                                      \
        GTEST_SKIP() << message;                                               \
    }

#endif // TESTS_COMMON_H

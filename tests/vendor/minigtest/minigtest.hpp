#pragma once
// Minimal GTest-compatible single-header test runner.
// Subset of the GTest macro API; both EXPECT_ and ASSERT_ abort the current
// test via exception (only the first failure per test is reported).

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace minigtest {

struct TestFailure : std::exception {
    std::string message;
    explicit TestFailure(std::string msg) : message(std::move(msg)) {}
    const char* what() const noexcept override { return message.c_str(); }
};

struct TestCase {
    std::string suite;
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* suite, const char* name, std::function<void()> fn) {
        registry().push_back({suite, name, std::move(fn)});
    }
};

inline int run_all_tests(int /*argc*/, char** /*argv*/) {
    int failures = 0;
    const int total = static_cast<int>(registry().size());
    std::cout << "[==========] Running " << total << " test(s).\n";
    for (const auto& tc : registry()) {
        std::cout << "[ RUN      ] " << tc.suite << "." << tc.name << "\n";
        try {
            tc.fn();
            std::cout << "[       OK ] " << tc.suite << "." << tc.name << "\n";
        } catch (const TestFailure& e) {
            std::cout << "[  FAILED  ] " << tc.suite << "." << tc.name
                      << "\n" << e.message << "\n";
            ++failures;
        } catch (const std::exception& e) {
            std::cout << "[  FAILED  ] " << tc.suite << "." << tc.name
                      << "\n  Unexpected exception: " << e.what() << "\n";
            ++failures;
        } catch (...) {
            std::cout << "[  FAILED  ] " << tc.suite << "." << tc.name
                      << "\n  Unknown exception.\n";
            ++failures;
        }
    }
    std::cout << "[==========]\n";
    if (failures == 0)
        std::cout << "[  PASSED  ] " << total << " test(s).\n";
    else
        std::cout << "[  FAILED  ] " << failures << " of " << total << " test(s).\n";
    return failures;
}

// Portable value-to-string helper: streams if the type supports operator<<.
namespace detail {
template<typename T, typename = void>
struct is_streamable : std::false_type {};

template<typename T>
struct is_streamable<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<T>())>>
    : std::true_type {};

template<typename T>
std::string to_str(const T& v) {
    if constexpr (is_streamable<T>::value) {
        std::ostringstream oss;
        oss << v;
        return oss.str();
    } else {
        (void)v;
        return "?";
    }
}
} // namespace detail

} // namespace minigtest

// ---------------------------------------------------------------------------
// Registration macro
// ---------------------------------------------------------------------------
#define TEST(Suite, Name)                                                          \
    static void minigtest_fn_##Suite##_##Name();                                   \
    static ::minigtest::Registrar minigtest_reg_##Suite##_##Name{                  \
        #Suite, #Name, minigtest_fn_##Suite##_##Name};                             \
    static void minigtest_fn_##Suite##_##Name()

// ---------------------------------------------------------------------------
// Assertion macros — all abort the test on failure via TestFailure exception.
// ---------------------------------------------------------------------------
#define MINIGTEST_FAIL_(msg)                                                       \
    do {                                                                           \
        std::ostringstream _oss;                                                   \
        _oss << __FILE__ << ":" << __LINE__ << ": " << (msg);                     \
        throw ::minigtest::TestFailure{_oss.str()};                                \
    } while (false)

#define EXPECT_TRUE(expr)                                                          \
    do {                                                                           \
        if (!(expr)) MINIGTEST_FAIL_("EXPECT_TRUE(" #expr ") failed");            \
    } while (false)

#define EXPECT_FALSE(expr)                                                         \
    do {                                                                           \
        if (!!(expr)) MINIGTEST_FAIL_("EXPECT_FALSE(" #expr ") failed");          \
    } while (false)

#define EXPECT_EQ(a, b)                                                            \
    do {                                                                           \
        const auto& _a = (a);                                                      \
        const auto& _b = (b);                                                      \
        if (!(_a == _b)) {                                                         \
            std::ostringstream _msg;                                               \
            _msg << "EXPECT_EQ(" #a ", " #b ") failed: "                          \
                 << ::minigtest::detail::to_str(_a)                                \
                 << " != "                                                         \
                 << ::minigtest::detail::to_str(_b);                               \
            MINIGTEST_FAIL_(_msg.str());                                           \
        }                                                                          \
    } while (false)

#define EXPECT_NE(a, b)                                                            \
    do {                                                                           \
        const auto& _a = (a);                                                      \
        const auto& _b = (b);                                                      \
        if (_a == _b) {                                                            \
            std::ostringstream _msg;                                               \
            _msg << "EXPECT_NE(" #a ", " #b ") failed: both == "                  \
                 << ::minigtest::detail::to_str(_a);                               \
            MINIGTEST_FAIL_(_msg.str());                                           \
        }                                                                          \
    } while (false)

#define ASSERT_EQ(a, b)   EXPECT_EQ(a, b)
#define ASSERT_NE(a, b)   EXPECT_NE(a, b)
#define ASSERT_TRUE(expr) EXPECT_TRUE(expr)
#define ASSERT_FALSE(e)   EXPECT_FALSE(e)

// EXPECT_THROW: passes TestFailure through so nested assertions work correctly.
#define EXPECT_THROW(stmt, exc_type)                                               \
    do {                                                                           \
        bool _threw = false;                                                       \
        try { (void)(stmt); }                                                      \
        catch (::minigtest::TestFailure&) { throw; }                               \
        catch (const exc_type&) { _threw = true; }                                 \
        catch (...) {}                                                             \
        if (!_threw)                                                               \
            MINIGTEST_FAIL_("EXPECT_THROW(" #stmt ", " #exc_type ") did not throw"); \
    } while (false)

#define EXPECT_NO_THROW(stmt)                                                      \
    do {                                                                           \
        try { (void)(stmt); }                                                      \
        catch (::minigtest::TestFailure&) { throw; }                               \
        catch (const std::exception& _e) {                                         \
            std::ostringstream _msg;                                               \
            _msg << "EXPECT_NO_THROW(" #stmt ") threw: " << _e.what();            \
            MINIGTEST_FAIL_(_msg.str());                                           \
        }                                                                          \
        catch (...) { MINIGTEST_FAIL_("EXPECT_NO_THROW(" #stmt ") threw"); }      \
    } while (false)

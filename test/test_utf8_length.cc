#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "utils/Utf8Length.h"

// Standalone so the compatibility helper can also be checked without Drogon.
#define CHECK_UTF8(condition) do { \
    if (!(condition)) { \
        std::cerr << "Failed: " #condition << " at line " << __LINE__ << '\n'; \
        return EXIT_FAILURE; \
    } \
} while (false)
int main() {
    using gnp::utils::utf8Length;
    CHECK_UTF8(utf8Length("") == 0);
    CHECK_UTF8(utf8Length("hello") == 5);
    CHECK_UTF8(utf8Length("\xc3\xa9\xe6\x97\xa5\xf0\x9f\x98\x80") == 3);
    CHECK_UTF8(utf8Length("a\xcc\x81") == 2);  // Code points, not graphemes.
    CHECK_UTF8(utf8Length("\xe6\x97") == 2);   // Truncated sequence.
    CHECK_UTF8(utf8Length("\xe6" "a" "\x80") == 3);
    CHECK_UTF8(utf8Length(std::string_view("a\0b", 3)) == 3);
}

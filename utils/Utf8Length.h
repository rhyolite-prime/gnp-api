/*
MIT License

Copyright (c) 2019-2023 An Tao

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#pragma once

#include <cstddef>
#include <string_view>

// Matches drogon::utils::utf8Length introduced after Drogon 1.9.13.
// Invalid bytes are counted individually, as in Drogon. Keep this local so
// generated models compile against both the pinned and newer Drogon releases.
// Adapted from Drogon (MIT), commit 9784c612762b106adf70a713cf68384d909d2141.
namespace gnp::utils {
inline std::size_t utf8Length(std::string_view str)
{
    std::size_t length = 0;

    for (std::size_t i = 0; i < str.size();)
    {
        unsigned char ch = static_cast<unsigned char>(str[i]);

        if ((ch & 0b10000000) == 0)
        {
            ++i;
        }
        else if ((ch & 0b11100000) == 0b11000000 && i + 1 < str.size() &&
                 (static_cast<unsigned char>(str[i + 1]) & 0b11000000) ==
                     0b10000000)
        {
            i += 2;
        }
        else if ((ch & 0b11110000) == 0b11100000 && i + 2 < str.size() &&
                 (static_cast<unsigned char>(str[i + 1]) & 0b11000000) ==
                     0b10000000 &&
                 (static_cast<unsigned char>(str[i + 2]) & 0b11000000) ==
                     0b10000000)
        {
            i += 3;
        }
        else if ((ch & 0b11111000) == 0b11110000 && i + 3 < str.size() &&
                 (static_cast<unsigned char>(str[i + 1]) & 0b11000000) ==
                     0b10000000 &&
                 (static_cast<unsigned char>(str[i + 2]) & 0b11000000) ==
                     0b10000000 &&
                 (static_cast<unsigned char>(str[i + 3]) & 0b11000000) ==
                     0b10000000)
        {
            i += 4;
        }
        else
        {
            ++i;
        }

        ++length;
    }

    return length;
}

} // namespace gnp::utils

/*

Copyright (c) 2003, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef LIBED2K_ESCAPE_STRING_HPP_INCLUDED
#define LIBED2K_ESCAPE_STRING_HPP_INCLUDED

#include <string>
#include <boost/limits.hpp>
#include <boost/array.hpp>
#include <libed2k/config.hpp>
#include <libed2k/size_type.hpp>
#include <libed2k/error_code.hpp>

namespace libed2k {
boost::array<char, 4 + std::numeric_limits<size_type>::digits10> to_string(size_type n);

std::string unescape_string(std::string const& s, error_code& ec);
// replaces all disallowed URL characters by their %-encoding
std::string escape_string(const char* str, int len);
// same as escape_string but does not encode '/'
std::string escape_path(const char* str, int len);
// if the url does not appear to be encoded, and it contains illegal url characters
// it will be encoded
std::string maybe_url_encode(std::string const& url);

bool need_encoding(char const* str, int len);

// encodes a string using the base64 scheme
std::string base64encode(std::string const& s);
std::string base64decode(std::string const& s);
// encodes a string using the base32 scheme
std::string base32encode(std::string const& s);
std::string base32decode(std::string const& s);

std::string url_has_argument(std::string const& url, std::string argument, std::string::size_type* out_pos = 0);

// replaces \ with /
void convert_path_to_posix(std::string& path);

std::string read_until(char const*& str, char delim, char const* end);
std::string to_hex(std::string const& s);
bool is_hex(char const* in, int len);
void to_hex(char const* in, int len, char* out);
bool from_hex(char const* in, int len, char* out);

#if defined LIBED2K_WINDOWS && LIBED2K_USE_WSTRING
std::wstring convert_to_wstring(std::string const& s);
std::string convert_from_wstring(std::wstring const& s);
#endif

#if LIBED2K_USE_ICONV || LIBED2K_USE_LOCALE
std::string convert_to_native(std::string const& s);
std::string convert_from_native(std::string const& s);
#else
inline std::string const& convert_to_native(std::string const& s) { return s; }
inline std::string const& convert_from_native(std::string const& s) { return s; }
#endif
}

#endif  // LIBED2K_ESCAPE_STRING_HPP_INCLUDED

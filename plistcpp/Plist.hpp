//
//   PlistCpp Property List (plist) serialization and parsing library.
//
//   https://github.com/animetrics/PlistCpp
//
//   Copyright (c) 2011 Animetrics Inc. (marc@animetrics.com)
//   
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//   
//   The above copyright notice and this permission notice shall be included in
//   all copies or substantial portions of the Software.
//   
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//   THE SOFTWARE.

#ifndef __PLIST_H__
#define __PLIST_H__

#include <boost/any.hpp>
#include <boost/cstdint.hpp>

#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <fstream>
#include <stdexcept>

#include "PlistDate.hpp"

namespace Plist {
// Plist value types and their corresponding c++ types

typedef std::string string_type;
typedef int64_t integer_type;
typedef double real_type;
typedef std::map<std::string, boost::any> dictionary_type;
typedef std::vector<boost::any> array_type;
typedef Date date_type;
typedef std::string data_type;
typedef bool boolean_type;

// Public read methods.  Plist type (binary or xml) automatically detected.

void readPlist(const char* byteArrayTemp, int64_t size, boost::any& message);
template <typename T>
void readPlist(const char* byteArray, int64_t size, T& message);
template <typename T>
void readPlist(const std::string& filename, T& message);

class Error : public std::runtime_error {
 public:
#if __cplusplus >= 201103L
  using std::runtime_error::runtime_error;
#else
  inline Error(const std::string& what) : runtime_error(what) {}
#endif
};
};

template <typename T>
void Plist::readPlist(const std::string& filename, T& message) {
  std::ifstream stream(filename, std::ios::binary);
  if (!stream)
    throw Error("Can't open file.");
  readPlist(stream, message);
}

template <typename T>
void Plist::readPlist(const char* byteArrayTemp, int64_t size, T& message) {
  boost::any tmp_message;
  readPlist(byteArrayTemp, size, tmp_message);
  message = boost::any_cast<T>(tmp_message);
}

#endif

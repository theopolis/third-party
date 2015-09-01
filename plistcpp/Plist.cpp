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

#include "Plist.hpp"

#include <boost/locale/encoding_utf.hpp>

#include <list>
#include <sstream>

#include "pugixml.hpp"

namespace Plist {

struct PlistHelperData {
 public:
  // binary helper data
  std::vector<int32_t> _offsetTable;
  std::vector<unsigned char> _objectTable;
  int32_t _offsetByteSize;
  int64_t _offsetTableOffset;

  int32_t _objRefSize;
  int32_t _refCount;
};

// helper functions

// msvc <= 2005 doesn't have std::vector::data() method

template <typename T>
T* vecData(std::vector<T>& vec) {
  return (vec.size() > 0) ? &vec[0] : 0;
  // if(vec.size() > 0)
  //		return &vec[0];
  // else
  //		throw Error("vecData trying to get pointer to empty
  //std::vector");
}

template <typename T>
const T* vecData(const std::vector<T>& vec) {
  return (vec.size() > 0) ? &vec[0] : 0;
  // if(vec.size() > 0)
  //		return &vec[0];
  // else
  //		throw Error("vecData trying to get pointer to empty
  //std::vector");
}

// xml helper functions

template <typename T>
std::string stringFromValue(const T& value);

// xml parsing

static dictionary_type parseDictionary(pugi::xml_node& node);
static array_type parseArray(pugi::xml_node& node);
static Date parseDate(pugi::xml_node& node);
static boost::any parse(pugi::xml_node& doc);

// binary helper functions

template <typename IntegerType>
IntegerType bytesToInt(const unsigned char* bytes, bool littleEndian);
static double bytesToDouble(const unsigned char* bytes, bool littleEndian);
template <typename IntegerType>
std::vector<unsigned char> intToBytes(IntegerType val, bool littleEndian);
static std::vector<unsigned char> getRange(const unsigned char* origBytes,
                                    int64_t index,
                                    int64_t size);
static std::vector<unsigned char> getRange(const std::vector<unsigned char>& origBytes,
                                    int64_t index,
                                    int64_t size);

// binary parsing

static int countAny(const boost::any& object);
static int countDictionary(const dictionary_type& dictionary);
static int countArray(const array_type& array);

static boost::any parseBinary(const PlistHelperData& d, int objRef);
static dictionary_type parseBinaryDictionary(const PlistHelperData& d, int objRef);
static array_type parseBinaryArray(const PlistHelperData& d, int objRef);
static std::vector<int32_t> getRefsForContainers(const PlistHelperData& d, int objRef);
static int64_t parseBinaryInt(const PlistHelperData& d,
                       int headerPosition,
                       int& intByteCount);
static double parseBinaryReal(const PlistHelperData& d, int headerPosition);
static Date parseBinaryDate(const PlistHelperData& d, int headerPosition);
static bool parseBinaryBool(const PlistHelperData& d, int headerPosition);
static std::string parseBinaryString(const PlistHelperData& d, int objRef);
static std::string parseBinaryUnicode(const PlistHelperData& d, int headerPosition);
static data_type parseBinaryByteArray(const PlistHelperData& d, int headerPosition);
static std::vector<unsigned char> regulateNullBytes(
    const std::vector<unsigned char>& origBytes, unsigned int minBytes);
static void parseTrailer(PlistHelperData& d,
                  const std::vector<unsigned char>& trailer);
static void parseOffsetTable(PlistHelperData& d,
                      const std::vector<unsigned char>& offsetTableBytes);
static int32_t getCount(const PlistHelperData& d,
                 int bytePosition,
                 unsigned char headerByte,
                 int& startOffset);

static inline bool hostLittleEndian() {
  union {
    uint32_t x;
    uint8_t c[4];
  } u;
  u.x = 0xab0000cd;
  return u.c[0] == 0xcd;
}

} // namespace Plist

namespace Plist {

static int countAny(const boost::any& object) {
  using namespace std;
  static boost::any dict = dictionary_type();
  static boost::any array = array_type();

  int count = 0;
  if (object.type() == dict.type())
    count += countDictionary(boost::any_cast<dictionary_type>(object)) + 1;
  else if (object.type() == array.type())
    count += countArray(boost::any_cast<array_type>(object)) + 1;
  else
    ++count;

  return count;
}

static int countDictionary(const dictionary_type& dictionary) {
  using namespace std;

  int count = 0;
  for (dictionary_type::const_iterator it = dictionary.begin();
       it != dictionary.end();
       ++it) {
    ++count;
    count += countAny(it->second);
  }

  return count;
}

static int countArray(const array_type& array) {
  using namespace std;
  int count = 0;
  for (array_type::const_iterator it = array.begin(); it != array.end(); ++it)
    count += countAny(*it);

  return count;
}

void readPlist(const char* byteArrayTemp, int64_t size, boost::any& message) {
  using namespace std;
  const unsigned char* byteArray = (const unsigned char*)byteArrayTemp;
  if (!byteArray || (size == 0))
    throw Error("Plist: Empty plist data");

  // infer plist type from header.  If it has the bplist00 header as first 8
  // bytes, then it's a binary plist.  Otherwise, assume it's XML

  std::string magicHeader((const char*)byteArray, 8);
  if (magicHeader == "bplist00") {
    PlistHelperData d;
    parseTrailer(d, getRange(byteArray, size - 32, 32));

    d._objectTable = getRange(byteArray, 0, d._offsetTableOffset);
    std::vector<unsigned char> offsetTableBytes = getRange(
        byteArray, d._offsetTableOffset, size - d._offsetTableOffset - 32);

    parseOffsetTable(d, offsetTableBytes);

    message = parseBinary(d, 0);
  } else {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_buffer(byteArray, (size_t)size);
    if (!result)
      throw Error((string("Plist: XML parsed with error ") +
                   result.description()).c_str());

    pugi::xml_node rootNode = doc.child("plist").first_child();
    message = parse(rootNode);
  }
}

static dictionary_type parseDictionary(pugi::xml_node& node) {
  using namespace std;

  dictionary_type dict;
  for (pugi::xml_node_iterator it = node.begin(); it != node.end(); ++it) {
    if (string("key") != it->name())
      throw Error("Plist: XML dictionary key expected but not found");

    string key(it->first_child().value());
    ++it;

    if (it == node.end())
      throw Error("Plist: XML dictionary value expected for key " + key +
                  "but not found");
    else if (string("key") == it->name())
      throw Error("Plist: XML dictionary value expected for key " + key +
                  "but found another key node");

    dict[key] = parse(*it);
  }

  return dict;
}

static array_type parseArray(pugi::xml_node& node) {
  using namespace std;

  array_type array;
  for (pugi::xml_node_iterator it = node.begin(); it != node.end(); ++it)
    array.push_back(parse(*it));

  return array;
}

static Date parseDate(pugi::xml_node& node) {
  Date date;
  date.setTimeFromXMLConvention(node.first_child().value());

  return date;
}

static boost::any parse(pugi::xml_node& node) {
  using namespace std;

  string nodeName = node.name();

  boost::any result;
  if ("dict" == nodeName)
    result = parseDictionary(node);
  else if ("array" == nodeName)
    result = parseArray(node);
  else if ("string" == nodeName)
    result = string(node.first_child().value());
  else if ("integer" == nodeName)
    result = (int64_t)atoll(node.first_child().value());
  else if ("real" == nodeName)
    result = atof(node.first_child().value());
  else if ("false" == nodeName)
    result = bool(false);
  else if ("true" == nodeName)
    result = bool(true);
  else if ("data" == nodeName)
    result = string(node.first_child().value());
  else if ("date" == nodeName)
    result = parseDate(node);
  else
    throw Error(string("Plist: XML unknown node type " + nodeName));

  return result;
}

static void parseOffsetTable(
    PlistHelperData& d, const std::vector<unsigned char>& offsetTableBytes) {
  for (unsigned int i = 0; i < offsetTableBytes.size();
       i += d._offsetByteSize) {
    std::vector<unsigned char> temp =
        getRange(offsetTableBytes, i, d._offsetByteSize);
    std::reverse(temp.begin(), temp.end());
    d._offsetTable.push_back(bytesToInt<int32_t>(
        vecData(regulateNullBytes(temp, 4)), hostLittleEndian()));
  }
}

static void parseTrailer(PlistHelperData& d,
                         const std::vector<unsigned char>& trailer) {
  d._offsetByteSize = bytesToInt<int32_t>(
      vecData(regulateNullBytes(getRange(trailer, 6, 1), 4)),
      hostLittleEndian());
  d._objRefSize = bytesToInt<int32_t>(
      vecData(regulateNullBytes(getRange(trailer, 7, 1), 4)),
      hostLittleEndian());

  std::vector<unsigned char> refCountBytes = getRange(trailer, 12, 4);
  //	std::reverse(refCountBytes.begin(), refCountBytes.end());
  d._refCount = bytesToInt<int32_t>(vecData(refCountBytes), false);

  std::vector<unsigned char> offsetTableOffsetBytes = getRange(trailer, 24, 8);
  //	std::reverse(offsetTableOffsetBytes.begin(),
  //offsetTableOffsetBytes.end());
  d._offsetTableOffset =
      bytesToInt<int64_t>(vecData(offsetTableOffsetBytes), false);
}

static std::vector<unsigned char> regulateNullBytes(
    const std::vector<unsigned char>& origBytes, unsigned int minBytes) {

  std::vector<unsigned char> bytes(origBytes);
  while ((bytes.back() == 0) && (bytes.size() > minBytes))
    bytes.pop_back();

  while (bytes.size() < minBytes)
    bytes.push_back(0);

  return bytes;
}

static boost::any parseBinary(const PlistHelperData& d, int objRef) {
  unsigned char header = d._objectTable[d._offsetTable[objRef]];
  switch (header & 0xF0) {
  case 0x00: {
    return parseBinaryBool(d, d._offsetTable[objRef]);
  }
  case 0x10: {
    int intByteCount;
    return parseBinaryInt(d, d._offsetTable[objRef], intByteCount);
  }
  case 0x20: {
    return parseBinaryReal(d, d._offsetTable[objRef]);
  }
  case 0x30: {
    return parseBinaryDate(d, d._offsetTable[objRef]);
  }
  case 0x40: {
    return parseBinaryByteArray(d, d._offsetTable[objRef]);
  }
  case 0x50: {
    return parseBinaryString(d, d._offsetTable[objRef]);
  }
  case 0x60: {
    return parseBinaryUnicode(d, d._offsetTable[objRef]);
  }
  case 0xD0: {
    return parseBinaryDictionary(d, objRef);
  }
  case 0xA0: {
    return parseBinaryArray(d, objRef);
  }
  }
  throw Error("This type is not supported");
}

static std::vector<int32_t> getRefsForContainers(const PlistHelperData& d,
                                                 int objRef) {
  using namespace std;
  int32_t refCount = 0;
  int refStartPosition;
  refCount = getCount(d,
                      d._offsetTable[objRef],
                      d._objectTable[d._offsetTable[objRef]],
                      refStartPosition);
  refStartPosition += d._offsetTable[objRef];

  vector<int32_t> refs;
  int mult = 1;
  if ((((unsigned char)d._objectTable[d._offsetTable[objRef]]) & 0xF0) == 0xD0)
    mult = 2;
  for (int i = refStartPosition;
       i < refStartPosition + refCount * mult * d._objRefSize;
       i += d._objRefSize) {
    std::vector<unsigned char> refBuffer =
        getRange(d._objectTable, i, d._objRefSize);
    reverse(refBuffer.begin(), refBuffer.end());
    refs.push_back(bytesToInt<int32_t>(vecData(regulateNullBytes(refBuffer, 4)),
                                       hostLittleEndian()));
  }

  return refs;
}

static array_type parseBinaryArray(const PlistHelperData& d, int objRef) {
  using namespace std;
  vector<int32_t> refs = getRefsForContainers(d, objRef);
  int32_t refCount = refs.size();

  array_type array;
  for (int i = 0; i < refCount; ++i)
    array.push_back(parseBinary(d, refs[i]));

  return array;
}

static dictionary_type parseBinaryDictionary(const PlistHelperData& d,
                                             int objRef) {
  using namespace std;
  vector<int32_t> refs = getRefsForContainers(d, objRef);
  int32_t refCount = refs.size() / 2;

  dictionary_type dict;
  for (int i = 0; i < refCount; i++) {
    boost::any keyAny = parseBinary(d, refs[i]);

    try {
      std::string& key = boost::any_cast<std::string&>(keyAny);
      dict[key] = parseBinary(d, refs[i + refCount]);
    } catch (boost::bad_any_cast&) {
      throw Error("Error parsing dictionary.  Key can't be parsed as a string");
    }
  }

  return dict;
}

static std::string parseBinaryString(const PlistHelperData& d,
                                     int headerPosition) {
  unsigned char headerByte = d._objectTable[headerPosition];
  int charStartPosition;
  int32_t charCount =
      getCount(d, headerPosition, headerByte, charStartPosition);
  charStartPosition += headerPosition;

  std::vector<unsigned char> characterBytes =
      getRange(d._objectTable, charStartPosition, charCount);
  std::string buffer =
      std::string((char*)vecData(characterBytes), characterBytes.size());
  return buffer;
}

static std::string parseBinaryUnicode(const PlistHelperData& d,
                                      int headerPosition) {
  unsigned char headerByte = d._objectTable[headerPosition];
  int charStartPosition;
  int32_t charCount =
      getCount(d, headerPosition, headerByte, charStartPosition);
  charStartPosition += headerPosition;

  std::vector<unsigned char> characterBytes =
      getRange(d._objectTable, charStartPosition, charCount * 2);
  if (hostLittleEndian()) {
    if (!characterBytes.empty()) {
      for (std::size_t i = 0, n = characterBytes.size(); i < n - 1; i += 2)
        std::swap(characterBytes[i], characterBytes[i + 1]);
    }
  }

  int16_t* u16chars = (int16_t*)vecData(characterBytes);
  std::size_t u16len = characterBytes.size() / 2;
  std::string result = boost::locale::conv::utf_to_utf<char, int16_t>(
      u16chars, u16chars + u16len, boost::locale::conv::stop);
  return result;
}

static int64_t parseBinaryInt(const PlistHelperData& d,
                              int headerPosition,
                              int& intByteCount) {
  unsigned char header = d._objectTable[headerPosition];
  intByteCount = 1 << (header & 0xf);
  std::vector<unsigned char> buffer =
      getRange(d._objectTable, headerPosition + 1, intByteCount);
  reverse(buffer.begin(), buffer.end());

  return bytesToInt<int64_t>(vecData(regulateNullBytes(buffer, 8)),
                             hostLittleEndian());
}

static double parseBinaryReal(const PlistHelperData& d, int headerPosition) {
  unsigned char header = d._objectTable[headerPosition];
  int byteCount = 1 << (header & 0xf);
  std::vector<unsigned char> buffer =
      getRange(d._objectTable, headerPosition + 1, byteCount);
  reverse(buffer.begin(), buffer.end());

  return bytesToDouble(vecData(regulateNullBytes(buffer, 8)),
                       hostLittleEndian());
}

static bool parseBinaryBool(const PlistHelperData& d, int headerPosition) {
  unsigned char header = d._objectTable[headerPosition];
  bool value;
  if (header == 0x09)
    value = true;
  else if (header == 0x08)
    value = false;
  else if (header == 0x00) {
    // null byte, not sure yet what to do with this.  It's in the spec but we
    // have never encountered it.

    throw Error("Plist: null byte encountered, unsure how to parse");
  } else if (header == 0x0F) {
    // fill byte, not sure yet what to do with this.  It's in the spec but we
    // have never encountered it.

    throw Error("Plist: fill byte encountered, unsure how to parse");
  } else {
    std::stringstream ss;
    ss << "Plist: unknown header " << header;
    throw Error(ss.str());
  }

  return value;
}

static Date parseBinaryDate(const PlistHelperData& d, int headerPosition) {
  // date always an 8 byte float starting after full byte header
  std::vector<unsigned char> buffer =
      getRange(d._objectTable, headerPosition + 1, 8);

  Date date;

  // Date is stored as Apple Epoch and big endian.
  date.setTimeFromAppleEpoch(bytesToDouble(vecData(buffer), false));

  return date;
}

static data_type parseBinaryByteArray(const PlistHelperData& d,
                                      int headerPosition) {
  unsigned char headerByte = d._objectTable[headerPosition];
  int byteStartPosition;
  int32_t byteCount =
      getCount(d, headerPosition, headerByte, byteStartPosition);
  byteStartPosition += headerPosition;

  return std::string((const char*)vecData(d._objectTable) + byteStartPosition,
                     byteCount);
}

static int32_t getCount(const PlistHelperData& d,
                        int bytePosition,
                        unsigned char headerByte,
                        int& startOffset) {
  unsigned char headerByteTrail = headerByte & 0xf;
  if (headerByteTrail < 15) {
    startOffset = 1;
    return headerByteTrail;
  } else {
    int32_t count = (int32_t)parseBinaryInt(d, bytePosition + 1, startOffset);
    startOffset += 2;
    return count;
  }
}

template <typename T>
std::string stringFromValue(const T& value) {
  std::stringstream ss;
  ss << value;
  return ss.str();
}

template <typename IntegerType>
IntegerType bytesToInt(const unsigned char* bytes, bool littleEndian) {
  IntegerType result = 0;
  if (littleEndian)
    for (int n = sizeof(result) - 1; n >= 0; n--)
      result = (result << 8) + bytes[n];
  else
    for (unsigned n = 0; n < sizeof(result); n++)
      result = (result << 8) + bytes[n];
  return result;
}

static double bytesToDouble(const unsigned char* bytes, bool littleEndian) {
  double result;
  int numBytes = sizeof(double);
  if (littleEndian)
    memcpy(&result, bytes, numBytes);
  else {
    std::vector<unsigned char> bytesReverse(numBytes);
    std::reverse_copy(bytes, bytes + numBytes, bytesReverse.begin());
    memcpy(&result, vecData(bytesReverse), numBytes);
  }
  return result;
}

template <typename IntegerType>
std::vector<unsigned char> intToBytes(IntegerType val, bool littleEndian) {
  unsigned int numBytes = sizeof(val);
  std::vector<unsigned char> bytes(numBytes);

  for (unsigned n = 0; n < numBytes; ++n)
    if (littleEndian)
      bytes[n] = (val >> 8 * n) & 0xff;
    else
      bytes[numBytes - 1 - n] = (val >> 8 * n) & 0xff;

  return bytes;
}

static std::vector<unsigned char> getRange(const unsigned char* origBytes,
                                           int64_t index,
                                           int64_t size) {
  std::vector<unsigned char> result(
      (std::vector<unsigned char>::size_type)size);
  std::copy(origBytes + index, origBytes + index + size, result.begin());
  return result;
}

static std::vector<unsigned char> getRange(
    const std::vector<unsigned char>& origBytes, int64_t index, int64_t size) {
  if ((index + size) > (int64_t)origBytes.size())
    throw Error("Out of bounds getRange");
  return getRange(vecData(origBytes), index, size);
}

} // namespace Plist

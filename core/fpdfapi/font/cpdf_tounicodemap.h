// Copyright 2017 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef CORE_FPDFAPI_FONT_CPDF_TOUNICODEMAP_H_
#define CORE_FPDFAPI_FONT_CPDF_TOUNICODEMAP_H_

#include <map>
#include <optional>
#include <set>
#include <vector>

#include "core/fxcrt/fx_string.h"
#include "core/fxcrt/retain_ptr.h"
#include "core/fxcrt/unowned_ptr.h"

class CPDF_CID2UnicodeMap;
class CPDF_SimpleParser;
class CPDF_Stream;

class CPDF_ToUnicodeMap {
 public:
  explicit CPDF_ToUnicodeMap(RetainPtr<const CPDF_Stream> pStream);
  ~CPDF_ToUnicodeMap();

  WideString Lookup(uint32_t charcode) const;
  uint32_t ReverseLookup(wchar_t unicode) const;

  size_t GetUnicodeCountByCharcodeForTesting(uint32_t charcode) const;

 private:
  friend class CPDFToUnicodeMapTest_StringToCode_Test;
  friend class CPDFToUnicodeMapTest_StringToWideString_Test;

  static std::optional<uint32_t> StringToCode(ByteStringView str);
  static WideString StringToWideString(ByteStringView str);

  void Load(RetainPtr<const CPDF_Stream> pStream);

  // `previous_word` is the most recent word that `parser` returned.
  // Returns the last word `parser` encountered.
  ByteStringView HandleBeginBFChar(CPDF_SimpleParser& parser,
                                   ByteStringView previous_word);
  ByteStringView HandleBeginBFRange(CPDF_SimpleParser& parser,
                                    ByteStringView previous_word);

  uint32_t GetMultiCharIndexIndicator() const;
  void SetCode(uint32_t srccode, WideString destcode);

  // Inserts a new entry which hasn't not been inserted into `multimap_`
  // before.
  void InsertIntoMultimap(uint32_t code, uint32_t destcode);

  std::map<uint32_t, std::set<uint32_t>> multimap_;
  UnownedPtr<const CPDF_CID2UnicodeMap> base_map_;
  std::vector<WideString> multi_char_vec_;
};

#endif  // CORE_FPDFAPI_FONT_CPDF_TOUNICODEMAP_H_

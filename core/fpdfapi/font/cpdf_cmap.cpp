// Copyright 2017 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fpdfapi/font/cpdf_cmap.h"

#include <array>
#include <functional>
#include <utility>
#include <vector>

#include "core/fpdfapi/cmaps/fpdf_cmaps.h"
#include "core/fpdfapi/font/cpdf_cmapparser.h"
#include "core/fpdfapi/font/cpdf_fontglobals.h"
#include "core/fpdfapi/parser/cpdf_simple_parser.h"
#include "core/fxcrt/check.h"
#include "core/fxcrt/fx_memcpy_wrappers.h"
#include "core/fxcrt/notreached.h"

namespace {

struct ByteRange {
  uint8_t first_;
  uint8_t last_;  // Inclusive.
};

struct PredefinedCMap {
  const char* name_;  // Raw, POD struct.
  CIDSet charset_;
  CIDCoding coding_;
  CPDF_CMap::CodingScheme coding_scheme_;
  ByteRange leading_segs_[2];
};

constexpr PredefinedCMap kPredefinedCMaps[] = {
    {"GB-EUC",
     CIDSET_GB1,
     CIDCoding::kGB,
     CPDF_CMap::MixedTwoBytes,
     {{0xa1, 0xfe}}},
    {"GBpc-EUC",
     CIDSET_GB1,
     CIDCoding::kGB,
     CPDF_CMap::MixedTwoBytes,
     {{0xa1, 0xfc}}},
    {"GBK-EUC",
     CIDSET_GB1,
     CIDCoding::kGB,
     CPDF_CMap::MixedTwoBytes,
     {{0x81, 0xfe}}},
    {"GBKp-EUC",
     CIDSET_GB1,
     CIDCoding::kGB,
     CPDF_CMap::MixedTwoBytes,
     {{0x81, 0xfe}}},
    {"GBK2K-EUC",
     CIDSET_GB1,
     CIDCoding::kGB,
     CPDF_CMap::MixedTwoBytes,
     {{0x81, 0xfe}}},
    {"GBK2K",
     CIDSET_GB1,
     CIDCoding::kGB,
     CPDF_CMap::MixedTwoBytes,
     {{0x81, 0xfe}}},
    {"UniGB-UCS2", CIDSET_GB1, CIDCoding::kUCS2, CPDF_CMap::TwoBytes, {}},
    {"UniGB-UTF16", CIDSET_GB1, CIDCoding::kUTF16, CPDF_CMap::TwoBytes, {}},
    {"B5pc",
     CIDSET_CNS1,
     CIDCoding::kBIG5,
     CPDF_CMap::MixedTwoBytes,
     {{0xa1, 0xfc}}},
    {"HKscs-B5",
     CIDSET_CNS1,
     CIDCoding::kBIG5,
     CPDF_CMap::MixedTwoBytes,
     {{0x88, 0xfe}}},
    {"ETen-B5",
     CIDSET_CNS1,
     CIDCoding::kBIG5,
     CPDF_CMap::MixedTwoBytes,
     {{0xa1, 0xfe}}},
    {"ETenms-B5",
     CIDSET_CNS1,
     CIDCoding::kBIG5,
     CPDF_CMap::MixedTwoBytes,
     {{0xa1, 0xfe}}},
    {"UniCNS-UCS2", CIDSET_CNS1, CIDCoding::kUCS2, CPDF_CMap::TwoBytes, {}},
    {"UniCNS-UTF16", CIDSET_CNS1, CIDCoding::kUTF16, CPDF_CMap::TwoBytes, {}},
    {"83pv-RKSJ",
     CIDSET_JAPAN1,
     CIDCoding::kJIS,
     CPDF_CMap::MixedTwoBytes,
     {{0x81, 0x9f}, {0xe0, 0xfc}}},
    {"90ms-RKSJ",
     CIDSET_JAPAN1,
     CIDCoding::kJIS,
     CPDF_CMap::MixedTwoBytes,
     {{0x81, 0x9f}, {0xe0, 0xfc}}},
    {"90msp-RKSJ",
     CIDSET_JAPAN1,
     CIDCoding::kJIS,
     CPDF_CMap::MixedTwoBytes,
     {{0x81, 0x9f}, {0xe0, 0xfc}}},
    {"90pv-RKSJ",
     CIDSET_JAPAN1,
     CIDCoding::kJIS,
     CPDF_CMap::MixedTwoBytes,
     {{0x81, 0x9f}, {0xe0, 0xfc}}},
    {"Add-RKSJ",
     CIDSET_JAPAN1,
     CIDCoding::kJIS,
     CPDF_CMap::MixedTwoBytes,
     {{0x81, 0x9f}, {0xe0, 0xfc}}},
    {"EUC",
     CIDSET_JAPAN1,
     CIDCoding::kJIS,
     CPDF_CMap::MixedTwoBytes,
     {{0x8e, 0x8e}, {0xa1, 0xfe}}},
    {"H", CIDSET_JAPAN1, CIDCoding::kJIS, CPDF_CMap::TwoBytes, {{0x21, 0x7e}}},
    {"V", CIDSET_JAPAN1, CIDCoding::kJIS, CPDF_CMap::TwoBytes, {{0x21, 0x7e}}},
    {"Ext-RKSJ",
     CIDSET_JAPAN1,
     CIDCoding::kJIS,
     CPDF_CMap::MixedTwoBytes,
     {{0x81, 0x9f}, {0xe0, 0xfc}}},
    {"UniJIS-UCS2", CIDSET_JAPAN1, CIDCoding::kUCS2, CPDF_CMap::TwoBytes, {}},
    {"UniJIS-UCS2-HW",
     CIDSET_JAPAN1,
     CIDCoding::kUCS2,
     CPDF_CMap::TwoBytes,
     {}},
    {"UniJIS-UTF16", CIDSET_JAPAN1, CIDCoding::kUTF16, CPDF_CMap::TwoBytes, {}},
    {"KSC-EUC",
     CIDSET_KOREA1,
     CIDCoding::kKOREA,
     CPDF_CMap::MixedTwoBytes,
     {{0xa1, 0xfe}}},
    {"KSCms-UHC",
     CIDSET_KOREA1,
     CIDCoding::kKOREA,
     CPDF_CMap::MixedTwoBytes,
     {{0x81, 0xfe}}},
    {"KSCms-UHC-HW",
     CIDSET_KOREA1,
     CIDCoding::kKOREA,
     CPDF_CMap::MixedTwoBytes,
     {{0x81, 0xfe}}},
    {"KSCpc-EUC",
     CIDSET_KOREA1,
     CIDCoding::kKOREA,
     CPDF_CMap::MixedTwoBytes,
     {{0xa1, 0xfd}}},
    {"UniKS-UCS2", CIDSET_KOREA1, CIDCoding::kUCS2, CPDF_CMap::TwoBytes, {}},
    {"UniKS-UTF16", CIDSET_KOREA1, CIDCoding::kUTF16, CPDF_CMap::TwoBytes, {}},
};

const PredefinedCMap* GetPredefinedCMap(ByteStringView cmapid) {
  if (cmapid.GetLength() > 2) {
    cmapid = cmapid.First(cmapid.GetLength() - 2);
  }
  for (const auto& map : kPredefinedCMaps) {
    if (cmapid == map.name_) {
      return &map;
    }
  }
  return nullptr;
}

std::vector<bool> LoadLeadingSegments(const PredefinedCMap& map) {
  std::vector<bool> segments(256);
  const auto seg_span = pdfium::span(map.leading_segs_);
  for (const ByteRange& seg : seg_span) {
    if (seg.first_ == 0 && seg.last_ == 0) {
      break;
    }
    for (int b = seg.first_; b <= seg.last_; ++b) {
      segments[b] = true;
    }
  }
  return segments;
}

int CheckFourByteCodeRange(pdfium::span<uint8_t> codes,
                           pdfium::span<const CPDF_CMap::CodeRange> ranges) {
  for (size_t i = ranges.size(); i > 0; i--) {
    const auto& range = ranges[i - 1];
    if (range.char_size_ < codes.size()) {
      continue;
    }
    size_t iChar = 0;
    while (iChar < codes.size()) {
      if (codes[iChar] < range.lower_[iChar] ||
          codes[iChar] > range.upper_[iChar]) {
        break;
      }
      ++iChar;
    }
    if (iChar == range.char_size_) {
      return 2;
    }
    if (iChar) {
      return (codes.size() == range.char_size_) ? 2 : 1;
    }
  }
  return 0;
}

size_t GetFourByteCharSizeImpl(
    uint32_t charcode,
    pdfium::span<const CPDF_CMap::CodeRange> ranges) {
  if (ranges.empty()) {
    return 1;
  }

  std::array<uint8_t, 4> codes = {{
      0x00,
      0x00,
      static_cast<uint8_t>(charcode >> 8 & 0xFF),
      static_cast<uint8_t>(charcode),
  }};
  for (size_t offset = 0; offset < 4; offset++) {
    size_t size = 4 - offset;
    for (size_t j = 0; j < ranges.size(); j++) {
      size_t iSeg = (ranges.size() - 1) - j;
      if (ranges[iSeg].char_size_ < size) {
        continue;
      }
      size_t iChar = 0;
      while (iChar < size) {
        if (codes[offset + iChar] < ranges[iSeg].lower_[iChar] ||
            codes[offset + iChar] > ranges[iSeg].upper_[iChar]) {
          break;
        }
        ++iChar;
      }
      if (iChar == ranges[iSeg].char_size_) {
        return size;
      }
    }
  }
  return 1;
}

const fxcmap::CMap* FindEmbeddedCMap(pdfium::span<const fxcmap::CMap> pCMaps,
                                     ByteStringView bsName) {
  for (size_t i = 0; i < pCMaps.size(); i++) {
    if (bsName == pCMaps[i].name_) {
      return &pCMaps[i];
    }
  }
  return nullptr;
}

}  // namespace

CPDF_CMap::CPDF_CMap(ByteStringView bsPredefinedName)
    : vertical_(bsPredefinedName.Back() == 'V') {
  if (bsPredefinedName == "Identity-H" || bsPredefinedName == "Identity-V") {
    coding_ = CIDCoding::kCID;
    loaded_ = true;
    return;
  }

  const PredefinedCMap* map = GetPredefinedCMap(bsPredefinedName);
  if (!map) {
    return;
  }

  charset_ = map->charset_;
  coding_ = map->coding_;
  coding_scheme_ = map->coding_scheme_;
  if (coding_scheme_ == MixedTwoBytes) {
    mixed_two_byte_leading_bytes_ = LoadLeadingSegments(*map);
  }
  embed_map_ = FindEmbeddedCMap(
      CPDF_FontGlobals::GetInstance()->GetEmbeddedCharset(charset_),
      bsPredefinedName);
  if (!embed_map_) {
    return;
  }

  loaded_ = true;
}

CPDF_CMap::CPDF_CMap(pdfium::span<const uint8_t> spEmbeddedData)
    : direct_charcode_to_cidtable_(
          FixedSizeDataVector<uint16_t>::Zeroed(kDirectMapTableSize)) {
  CPDF_CMapParser parser(this);
  CPDF_SimpleParser syntax(spEmbeddedData);
  while (true) {
    ByteStringView word = syntax.GetWord();
    if (word.IsEmpty()) {
      break;
    }
    parser.ParseWord(word);
  }
}

CPDF_CMap::~CPDF_CMap() = default;

uint16_t CPDF_CMap::CIDFromCharCode(uint32_t charcode) const {
  if (coding_ == CIDCoding::kCID) {
    return static_cast<uint16_t>(charcode);
  }
  if (embed_map_) {
    return fxcmap::CIDFromCharCode(embed_map_, charcode);
  }
  if (direct_charcode_to_cidtable_.empty()) {
    return static_cast<uint16_t>(charcode);
  }

  auto table_span = direct_charcode_to_cidtable_.span();
  if (charcode < table_span.size()) {
    return table_span[charcode];
  }

  auto it =
      std::ranges::lower_bound(additional_charcode_to_cidmappings_, charcode,
                               std::less<>{}, &CPDF_CMap::CIDRange::end_code_);

  if (it == additional_charcode_to_cidmappings_.end() ||
      it->start_code_ > charcode) {
    return 0;
  }
  return it->start_cid_ + charcode - it->start_code_;
}

uint32_t CPDF_CMap::GetNextChar(ByteStringView pString, size_t* pOffset) const {
  size_t& offset = *pOffset;
  auto pBytes = pString.unsigned_span();
  switch (coding_scheme_) {
    case OneByte: {
      return offset < pBytes.size() ? pBytes[offset++] : 0;
    }
    case TwoBytes: {
      uint8_t byte1 = offset < pBytes.size() ? pBytes[offset++] : 0;
      uint8_t byte2 = offset < pBytes.size() ? pBytes[offset++] : 0;
      return 256 * byte1 + byte2;
    }
    case MixedTwoBytes: {
      uint8_t byte1 = offset < pBytes.size() ? pBytes[offset++] : 0;
      if (!mixed_two_byte_leading_bytes_[byte1]) {
        return byte1;
      }
      uint8_t byte2 = offset < pBytes.size() ? pBytes[offset++] : 0;
      return 256 * byte1 + byte2;
    }
    case MixedFourBytes: {
      std::array<uint8_t, 4> codes;
      size_t char_size = 1;
      codes[0] = offset < pBytes.size() ? pBytes[offset++] : 0;
      while (true) {
        int ret = CheckFourByteCodeRange(pdfium::span(codes).first(char_size),
                                         mixed_four_byte_leading_ranges_);
        if (ret == 0) {
          return 0;
        }
        if (ret == 2) {
          uint32_t charcode = 0;
          for (size_t i = 0; i < char_size; i++) {
            charcode = (charcode << 8) + codes[i];
          }
          return charcode;
        }
        if (char_size == 4 || offset == pBytes.size()) {
          return 0;
        }
        codes[char_size++] = pBytes[offset++];
      }
    }
  }
  NOTREACHED();
}

int CPDF_CMap::GetCharSize(uint32_t charcode) const {
  switch (coding_scheme_) {
    case OneByte:
      return 1;
    case TwoBytes:
      return 2;
    case MixedTwoBytes:
      if (charcode < 0x100) {
        return 1;
      }
      return 2;
    case MixedFourBytes:
      if (charcode < 0x100) {
        return 1;
      }
      if (charcode < 0x10000) {
        return 2;
      }
      if (charcode < 0x1000000) {
        return 3;
      }
      return 4;
  }
  NOTREACHED();
}

size_t CPDF_CMap::CountChar(ByteStringView pString) const {
  switch (coding_scheme_) {
    case OneByte:
      return pString.GetLength();
    case TwoBytes:
      return (pString.GetLength() + 1) / 2;
    case MixedTwoBytes: {
      size_t count = 0;
      for (size_t i = 0; i < pString.GetLength(); i++) {
        count++;
        if (mixed_two_byte_leading_bytes_[pString[i]]) {
          i++;
        }
      }
      return count;
    }
    case MixedFourBytes: {
      size_t count = 0;
      size_t offset = 0;
      while (offset < pString.GetLength()) {
        GetNextChar(pString, &offset);
        count++;
      }
      return count;
    }
  }
  NOTREACHED();
}

void CPDF_CMap::AppendChar(ByteString* str, uint32_t charcode) const {
  switch (coding_scheme_) {
    case OneByte:
      *str += static_cast<char>(charcode);
      return;
    case TwoBytes:
      *str += static_cast<char>(charcode / 256);
      *str += static_cast<char>(charcode % 256);
      return;
    case MixedTwoBytes:
      if (charcode < 0x100 && !mixed_two_byte_leading_bytes_[charcode]) {
        *str += static_cast<char>(charcode);
        return;
      }
      *str += static_cast<char>(charcode >> 8);
      *str += static_cast<char>(charcode);
      return;
    case MixedFourBytes:
      if (charcode < 0x100) {
        int iSize = static_cast<int>(
            GetFourByteCharSizeImpl(charcode, mixed_four_byte_leading_ranges_));
        int pad = iSize != 0 ? iSize - 1 : 0;
        for (int i = 0; i < pad; ++i) {
          *str += static_cast<char>(0);
        }
        *str += static_cast<char>(charcode);
        return;
      }
      if (charcode < 0x10000) {
        *str += static_cast<char>(charcode >> 8);
        *str += static_cast<char>(charcode);
        return;
      }
      if (charcode < 0x1000000) {
        *str += static_cast<char>(charcode >> 16);
        *str += static_cast<char>(charcode >> 8);
        *str += static_cast<char>(charcode);
        return;
      }
      *str += static_cast<char>(charcode >> 24);
      *str += static_cast<char>(charcode >> 16);
      *str += static_cast<char>(charcode >> 8);
      *str += static_cast<char>(charcode);
      return;
  }
  NOTREACHED();
}

void CPDF_CMap::SetAdditionalMappings(std::vector<CIDRange> mappings) {
  DCHECK(additional_charcode_to_cidmappings_.empty());
  if (coding_scheme_ != MixedFourBytes || mappings.empty()) {
    return;
  }

  std::sort(
      mappings.begin(), mappings.end(),
      [](const CPDF_CMap::CIDRange& arg1, const CPDF_CMap::CIDRange& arg2) {
        return arg1.end_code_ < arg2.end_code_;
      });
  additional_charcode_to_cidmappings_ = std::move(mappings);
}

void CPDF_CMap::SetMixedFourByteLeadingRanges(std::vector<CodeRange> ranges) {
  mixed_four_byte_leading_ranges_ = std::move(ranges);
}

void CPDF_CMap::SetDirectCharcodeToCIDTableRange(uint32_t start_code,
                                                 uint32_t end_code,
                                                 uint16_t start_cid) {
  pdfium::span<uint16_t> span = direct_charcode_to_cidtable_.span();
  for (uint32_t code = start_code; code <= end_code; ++code) {
    span[code] = static_cast<uint16_t>(start_cid + code - start_code);
  }
}

// Copyright 2017 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "xfa/fgas/font/cfgas_pdffontmgr.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <utility>

#include "core/fpdfapi/font/cpdf_font.h"
#include "core/fpdfapi/page/cpdf_docpagedata.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/fpdf_parser_utility.h"
#include "core/fxcrt/check.h"
#include "core/fxge/fx_font.h"
#include "xfa/fgas/font/cfgas_fontmgr.h"
#include "xfa/fgas/font/cfgas_gefont.h"

namespace {

// The 5 names per entry are: PsName, Normal, Bold, Italic, BoldItalic.
using FontNameEntry = std::array<const char*, 5>;
constexpr auto kXFAPDFFontNameTable = std::to_array<const FontNameEntry>({
    {{"Adobe PI Std", "AdobePIStd", "AdobePIStd", "AdobePIStd", "AdobePIStd"}},
    {{"Myriad Pro Light", "MyriadPro-Light", "MyriadPro-Semibold",
      "MyriadPro-LightIt", "MyriadPro-SemiboldIt"}},
});

ByteString PsNameToFontName(const ByteString& strPsName,
                            bool bBold,
                            bool bItalic) {
  for (const auto& entry : kXFAPDFFontNameTable) {
    if (strPsName == entry[0]) {
      size_t index = 1;
      if (bBold) {
        ++index;
      }
      if (bItalic) {
        index += 2;
      }
      return entry[index];
    }
  }
  return strPsName;
}

bool PsNameMatchDRFontName(ByteStringView bsPsName,
                           bool bBold,
                           bool bItalic,
                           const ByteString& bsDRFontName,
                           bool bStrictMatch) {
  ByteString bsDRName = bsDRFontName;
  bsDRName.Remove('-');
  size_t iPsLen = bsPsName.GetLength();
  auto nIndex = bsDRName.Find(bsPsName);
  if (nIndex.has_value() && !bStrictMatch) {
    return true;
  }

  if (!nIndex.has_value() || nIndex.value() != 0) {
    return false;
  }

  size_t iDifferLength = bsDRName.GetLength() - iPsLen;
  if (iDifferLength > 1 || (bBold || bItalic)) {
    auto iBoldIndex = bsDRName.Find("Bold");
    if (bBold != iBoldIndex.has_value()) {
      return false;
    }

    if (iBoldIndex.has_value()) {
      iDifferLength = std::min(iDifferLength - 4,
                               bsDRName.GetLength() - iBoldIndex.value() - 4);
    }
    bool bItalicFont = true;
    if (bsDRName.Contains("Italic")) {
      iDifferLength -= 6;
    } else if (bsDRName.Contains("It")) {
      iDifferLength -= 2;
    } else if (bsDRName.Contains("Oblique")) {
      iDifferLength -= 7;
    } else {
      bItalicFont = false;
    }

    if (bItalic != bItalicFont) {
      return false;
    }

    if (iDifferLength > 1) {
      ByteString bsDRTailer = bsDRName.Last(iDifferLength);
      if (bsDRTailer == "MT" || bsDRTailer == "PSMT" ||
          bsDRTailer == "Regular" || bsDRTailer == "Reg") {
        return true;
      }
      if (iBoldIndex.has_value() || bItalicFont) {
        return false;
      }

      bool bMatch = false;
      switch (bsPsName[iPsLen - 1]) {
        case 'L':
          if (bsDRName.Last(5) == "Light") {
            bMatch = true;
          }

          break;
        case 'R':
          if (bsDRName.Last(7) == "Regular" || bsDRName.Last(3) == "Reg") {
            bMatch = true;
          }

          break;
        case 'M':
          if (bsDRName.Last(5) == "Medium") {
            bMatch = true;
          }
          break;
        default:
          break;
      }
      return bMatch;
    }
  }
  return true;
}

}  // namespace

CFGAS_PDFFontMgr::CFGAS_PDFFontMgr(const CPDF_Document* doc) : doc_(doc) {
  DCHECK(doc);
}

CFGAS_PDFFontMgr::~CFGAS_PDFFontMgr() = default;

RetainPtr<CFGAS_GEFont> CFGAS_PDFFontMgr::FindFont(const ByteString& strPsName,
                                                   bool bBold,
                                                   bool bItalic,
                                                   bool bStrictMatch) {
  RetainPtr<const CPDF_Dictionary> font_set_dict =
      doc_->GetRoot()->GetDictFor("AcroForm")->GetDictFor("DR");
  if (!font_set_dict) {
    return nullptr;
  }

  font_set_dict = font_set_dict->GetDictFor("Font");
  if (!font_set_dict) {
    return nullptr;
  }

  ByteString name = strPsName;
  name.Remove(' ');

  auto* pData = CPDF_DocPageData::FromDocument(doc_);
  CPDF_DictionaryLocker locker(font_set_dict);
  for (const auto& it : locker) {
    const ByteString& key = it.first;
    const RetainPtr<CPDF_Object>& pObj = it.second;
    if (!PsNameMatchDRFontName(name.AsStringView(), bBold, bItalic, key,
                               bStrictMatch)) {
      continue;
    }
    RetainPtr<CPDF_Dictionary> font_dict =
        ToDictionary(pObj->GetMutableDirect());
    if (!ValidateDictType(font_dict.Get(), "Font")) {
      return nullptr;
    }

    RetainPtr<CPDF_Font> pPDFFont = pData->GetFont(font_dict);
    if (!pPDFFont || !pPDFFont->IsEmbedded()) {
      return nullptr;
    }

    return CFGAS_GEFont::LoadFont(std::move(pPDFFont));
  }
  return nullptr;
}

RetainPtr<CFGAS_GEFont> CFGAS_PDFFontMgr::GetFont(
    const WideString& wsFontFamily,
    uint32_t dwFontStyles,
    bool bStrictMatch) {
  auto key = std::make_pair(wsFontFamily, dwFontStyles);
  auto it = font_map_.find(key);
  if (it != font_map_.end()) {
    return it->second;
  }

  ByteString bsPsName = WideString(wsFontFamily).ToDefANSI();
  bool bBold = FontStyleIsForceBold(dwFontStyles);
  bool bItalic = FontStyleIsItalic(dwFontStyles);
  ByteString strFontName = PsNameToFontName(bsPsName, bBold, bItalic);
  RetainPtr<CFGAS_GEFont> font =
      FindFont(strFontName, bBold, bItalic, bStrictMatch);
  if (!font) {
    return nullptr;
  }

  font_map_[key] = font;
  return font;
}

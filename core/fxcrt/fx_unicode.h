// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef CORE_FXCRT_FX_UNICODE_H_
#define CORE_FXCRT_FX_UNICODE_H_

#include "core/fxcrt/fx_system.h"

// As defined in http://www.unicode.org/reports/tr14
enum class FX_BREAKPROPERTY : uint8_t {
  kOP = 0,
  kCL = 1,
  kQU = 2,
  kGL = 3,
  kNS = 4,
  kEX = 5,
  kSY = 6,
  kIS = 7,
  kPR = 8,
  kPO = 9,
  kNU = 10,
  kAL = 11,
  kID = 12,
  kIN = 13,
  kHY = 14,
  kBA = 15,
  kBB = 16,
  kB2 = 17,
  kZW = 18,
  kCM = 19,
  kWJ = 20,
  kH2 = 21,
  kH3 = 22,
  kJL = 23,
  kJV = 24,
  kJT = 25,
  kBK = 26,
  kCR = 27,
  kLF = 28,
  kNL = 29,
  kSA = 30,
  kSG = 31,
  kCB = 32,
  kXX = 33,
  kAI = 34,
  kSP = 35,
  kNONE = 36,
  kTB = 37,
};

uint32_t FX_GetUnicodeProperties(wchar_t wch);
wchar_t FX_GetMirrorChar(wchar_t wch);

#ifdef PDF_ENABLE_XFA

constexpr uint32_t FX_CHARTYPEBITS = 11;

enum FX_CHARTYPE {
  FX_CHARTYPE_Unknown = 0,
  FX_CHARTYPE_Tab = (1 << FX_CHARTYPEBITS),
  FX_CHARTYPE_Space = (2 << FX_CHARTYPEBITS),
  FX_CHARTYPE_Control = (3 << FX_CHARTYPEBITS),
  FX_CHARTYPE_Combination = (4 << FX_CHARTYPEBITS),
  FX_CHARTYPE_Numeric = (5 << FX_CHARTYPEBITS),
  FX_CHARTYPE_Normal = (6 << FX_CHARTYPEBITS),
  FX_CHARTYPE_ArabicAlef = (7 << FX_CHARTYPEBITS),
  FX_CHARTYPE_ArabicSpecial = (8 << FX_CHARTYPEBITS),
  FX_CHARTYPE_ArabicDistortion = (9 << FX_CHARTYPEBITS),
  FX_CHARTYPE_ArabicNormal = (10 << FX_CHARTYPEBITS),
  FX_CHARTYPE_ArabicForm = (11 << FX_CHARTYPEBITS),
  FX_CHARTYPE_Arabic = (12 << FX_CHARTYPEBITS),
};

FX_CHARTYPE GetCharTypeFromProp(uint32_t prop);

// Analagous to ULineBreak in icu's uchar.h, but permuted order, and a
// subset lacking some more recent additions.
FX_BREAKPROPERTY GetBreakPropertyFromProp(uint32_t prop);

wchar_t FX_GetMirrorChar(wchar_t wch, uint32_t dwProps);

#endif  // PDF_ENABLE_XFA

#endif  // CORE_FXCRT_FX_UNICODE_H_

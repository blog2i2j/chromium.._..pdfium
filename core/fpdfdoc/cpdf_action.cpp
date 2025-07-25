// Copyright 2016 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fpdfdoc/cpdf_action.h"

#include <array>
#include <iterator>
#include <utility>

#include "constants/stream_dict_common.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_name.h"
#include "core/fpdfapi/parser/fpdf_parser_utility.h"
#include "core/fpdfdoc/cpdf_filespec.h"

namespace {

constexpr auto kActionTypeStrings = std::to_array<const char*>({
    "GoTo",
    "GoToR",
    "GoToE",
    "Launch",
    "Thread",
    "URI",
    "Sound",
    "Movie",
    "Hide",
    "Named",
    "SubmitForm",
    "ResetForm",
    "ImportData",
    "JavaScript",
    "SetOCGState",
    "Rendition",
    "Trans",
    "GoTo3DView",
});

}  // namespace

CPDF_Action::CPDF_Action(RetainPtr<const CPDF_Dictionary> dict)
    : dict_(std::move(dict)) {}

CPDF_Action::CPDF_Action(const CPDF_Action& that) = default;

CPDF_Action::~CPDF_Action() = default;

CPDF_Action::Type CPDF_Action::GetType() const {
  // See ISO 32000-1:2008 spec, table 193.
  if (!ValidateDictOptionalType(dict_.Get(), "Action")) {
    return Type::kUnknown;
  }

  ByteString csType = dict_->GetNameFor("S");
  if (csType.IsEmpty()) {
    return Type::kUnknown;
  }

  static_assert(
      std::size(kActionTypeStrings) == static_cast<size_t>(Type::kLast),
      "Type mismatch");
  for (size_t i = 0; i < std::size(kActionTypeStrings); ++i) {
    if (csType == kActionTypeStrings[i]) {
      return static_cast<Type>(i + 1);
    }
  }
  return Type::kUnknown;
}

CPDF_Dest CPDF_Action::GetDest(CPDF_Document* doc) const {
  Type type = GetType();
  if (type != Type::kGoTo && type != Type::kGoToR && type != Type::kGoToE) {
    return CPDF_Dest(nullptr);
  }
  return CPDF_Dest::Create(doc, dict_->GetDirectObjectFor("D"));
}

WideString CPDF_Action::GetFilePath() const {
  Type type = GetType();
  if (type != Type::kGoToR && type != Type::kGoToE && type != Type::kLaunch &&
      type != Type::kSubmitForm && type != Type::kImportData) {
    return WideString();
  }

  RetainPtr<const CPDF_Object> pFile =
      dict_->GetDirectObjectFor(pdfium::stream::kF);
  if (pFile) {
    return CPDF_FileSpec(std::move(pFile)).GetFileName();
  }

  if (type != Type::kLaunch) {
    return WideString();
  }

  RetainPtr<const CPDF_Dictionary> pWinDict = dict_->GetDictFor("Win");
  if (!pWinDict) {
    return WideString();
  }

  return WideString::FromDefANSI(
      pWinDict->GetByteStringFor(pdfium::stream::kF).AsStringView());
}

ByteString CPDF_Action::GetURI(const CPDF_Document* doc) const {
  if (GetType() != Type::kURI) {
    return ByteString();
  }

  ByteString csURI = dict_->GetByteStringFor("URI");
  RetainPtr<const CPDF_Dictionary> pURI = doc->GetRoot()->GetDictFor("URI");
  if (pURI) {
    auto result = csURI.Find(":");
    if (!result.has_value() || result.value() == 0) {
      RetainPtr<const CPDF_Object> pBase = pURI->GetDirectObjectFor("Base");
      if (pBase && (pBase->IsString() || pBase->IsStream())) {
        csURI = pBase->GetString() + csURI;
      }
    }
  }
  return csURI;
}

bool CPDF_Action::GetHideStatus() const {
  return dict_->GetBooleanFor("H", true);
}

ByteString CPDF_Action::GetNamedAction() const {
  return dict_->GetByteStringFor("N");
}

uint32_t CPDF_Action::GetFlags() const {
  return dict_->GetIntegerFor("Flags");
}

bool CPDF_Action::HasFields() const {
  return dict_->KeyExist("Fields");
}

std::vector<RetainPtr<const CPDF_Object>> CPDF_Action::GetAllFields() const {
  std::vector<RetainPtr<const CPDF_Object>> result;
  if (!dict_) {
    return result;
  }

  ByteString csType = dict_->GetByteStringFor("S");
  RetainPtr<const CPDF_Object> pFields = csType == "Hide"
                                             ? dict_->GetDirectObjectFor("T")
                                             : dict_->GetArrayFor("Fields");
  if (!pFields) {
    return result;
  }

  if (pFields->IsDictionary() || pFields->IsString()) {
    result.push_back(std::move(pFields));
    return result;
  }

  const CPDF_Array* pArray = pFields->AsArray();
  if (!pArray) {
    return result;
  }

  for (size_t i = 0; i < pArray->size(); ++i) {
    RetainPtr<const CPDF_Object> pObj = pArray->GetDirectObjectAt(i);
    if (pObj) {
      result.push_back(std::move(pObj));
    }
  }
  return result;
}

std::optional<WideString> CPDF_Action::MaybeGetJavaScript() const {
  RetainPtr<const CPDF_Object> pObject = GetJavaScriptObject();
  if (!pObject) {
    return std::nullopt;
  }
  return pObject->GetUnicodeText();
}

WideString CPDF_Action::GetJavaScript() const {
  RetainPtr<const CPDF_Object> pObject = GetJavaScriptObject();
  return pObject ? pObject->GetUnicodeText() : WideString();
}

size_t CPDF_Action::GetSubActionsCount() const {
  if (!dict_ || !dict_->KeyExist("Next")) {
    return 0;
  }

  RetainPtr<const CPDF_Object> pNext = dict_->GetDirectObjectFor("Next");
  if (!pNext) {
    return 0;
  }
  if (pNext->IsDictionary()) {
    return 1;
  }
  const CPDF_Array* pArray = pNext->AsArray();
  return pArray ? pArray->size() : 0;
}

CPDF_Action CPDF_Action::GetSubAction(size_t iIndex) const {
  if (!dict_ || !dict_->KeyExist("Next")) {
    return CPDF_Action(nullptr);
  }

  RetainPtr<const CPDF_Object> pNext = dict_->GetDirectObjectFor("Next");
  if (!pNext) {
    return CPDF_Action(nullptr);
  }

  if (const CPDF_Array* pArray = pNext->AsArray()) {
    return CPDF_Action(pArray->GetDictAt(iIndex));
  }

  if (const CPDF_Dictionary* dict = pNext->AsDictionary()) {
    if (iIndex == 0) {
      return CPDF_Action(pdfium::WrapRetain(dict));
    }
  }
  return CPDF_Action(nullptr);
}

RetainPtr<const CPDF_Object> CPDF_Action::GetJavaScriptObject() const {
  if (!dict_) {
    return nullptr;
  }

  RetainPtr<const CPDF_Object> pJS = dict_->GetDirectObjectFor("JS");
  return (pJS && (pJS->IsString() || pJS->IsStream())) ? pJS : nullptr;
}

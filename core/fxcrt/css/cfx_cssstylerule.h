// Copyright 2017 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef CORE_FXCRT_CSS_CFX_CSSSTYLERULE_H_
#define CORE_FXCRT_CSS_CFX_CSSSTYLERULE_H_

#include <memory>
#include <vector>

#include "core/fxcrt/css/cfx_cssdeclaration.h"
#include "core/fxcrt/css/cfx_cssselector.h"

class CFX_CSSStyleRule {
 public:
  CFX_CSSStyleRule();
  ~CFX_CSSStyleRule();

  size_t CountSelectorLists() const;
  CFX_CSSSelector* GetSelectorList(size_t index) const;
  CFX_CSSDeclaration* GetDeclaration();

  void SetSelector(std::vector<std::unique_ptr<CFX_CSSSelector>>* list);

 private:
  CFX_CSSDeclaration declaration_;
  std::vector<std::unique_ptr<CFX_CSSSelector>> selector_;
};

#endif  // CORE_FXCRT_CSS_CFX_CSSSTYLERULE_H_

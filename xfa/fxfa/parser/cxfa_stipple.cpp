// Copyright 2017 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "xfa/fxfa/parser/cxfa_stipple.h"

#include "fxjs/xfa/cjx_node.h"
#include "xfa/fgas/graphics/cfgas_gegraphics.h"
#include "xfa/fxfa/parser/cxfa_color.h"
#include "xfa/fxfa/parser/cxfa_document.h"

namespace {

const CXFA_Node::PropertyData kStipplePropertyData[] = {
    {XFA_Element::Color, 1, {}},
    {XFA_Element::Extras, 1, {}},
};

const CXFA_Node::AttributeData kStippleAttributeData[] = {
    {XFA_Attribute::Id, XFA_AttributeType::CData, nullptr},
    {XFA_Attribute::Use, XFA_AttributeType::CData, nullptr},
    {XFA_Attribute::Rate, XFA_AttributeType::Integer, (void*)50},
    {XFA_Attribute::Usehref, XFA_AttributeType::CData, nullptr},
};

}  // namespace

CXFA_Stipple::CXFA_Stipple(CXFA_Document* doc, XFA_PacketType packet)
    : CXFA_Node(doc,
                packet,
                {XFA_XDPPACKET::kTemplate, XFA_XDPPACKET::kForm},
                XFA_ObjectType::Node,
                XFA_Element::Stipple,
                kStipplePropertyData,
                kStippleAttributeData,
                cppgc::MakeGarbageCollected<CJX_Node>(
                    doc->GetHeap()->GetAllocationHandle(),
                    this)) {}

CXFA_Stipple::~CXFA_Stipple() = default;

CXFA_Color* CXFA_Stipple::GetColorIfExists() {
  return GetChild<CXFA_Color>(0, XFA_Element::Color, false);
}

int32_t CXFA_Stipple::GetRate() {
  return JSObject()
      ->TryInteger(XFA_Attribute::Rate, true)
      .value_or(GetDefaultRate());
}

void CXFA_Stipple::Draw(CFGAS_GEGraphics* pGS,
                        const CFGAS_GEPath& fillPath,
                        const CFX_RectF& rtFill,
                        const CFX_Matrix& matrix) {
  int32_t iRate = GetRate();
  if (iRate == 0) {
    iRate = 100;
  }

  CXFA_Color* pColor = GetColorIfExists();
  FX_ARGB crColor = pColor ? pColor->GetValue() : CXFA_Color::kBlackColor;

  auto [alpha, colorref] = ArgbToAlphaAndColorRef(crColor);
  FX_ARGB cr = AlphaAndColorRefToArgb(iRate * alpha / 100, colorref);

  CFGAS_GEGraphics::StateRestorer restorer(pGS);
  pGS->SetFillColor(CFGAS_GEColor(cr));
  pGS->FillPath(fillPath, CFX_FillRenderOptions::FillType::kWinding, matrix);
}

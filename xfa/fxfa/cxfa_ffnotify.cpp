// Copyright 2014 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "xfa/fxfa/cxfa_ffnotify.h"

#include <utility>

#include "core/fxcrt/check.h"
#include "xfa/fxfa/cxfa_ffapp.h"
#include "xfa/fxfa/cxfa_ffarc.h"
#include "xfa/fxfa/cxfa_ffbarcode.h"
#include "xfa/fxfa/cxfa_ffcheckbutton.h"
#include "xfa/fxfa/cxfa_ffcombobox.h"
#include "xfa/fxfa/cxfa_ffdatetimeedit.h"
#include "xfa/fxfa/cxfa_ffdoc.h"
#include "xfa/fxfa/cxfa_ffdocview.h"
#include "xfa/fxfa/cxfa_ffexclgroup.h"
#include "xfa/fxfa/cxfa_fffield.h"
#include "xfa/fxfa/cxfa_ffimage.h"
#include "xfa/fxfa/cxfa_ffimageedit.h"
#include "xfa/fxfa/cxfa_ffline.h"
#include "xfa/fxfa/cxfa_fflistbox.h"
#include "xfa/fxfa/cxfa_ffnumericedit.h"
#include "xfa/fxfa/cxfa_ffpageview.h"
#include "xfa/fxfa/cxfa_ffpasswordedit.h"
#include "xfa/fxfa/cxfa_ffpushbutton.h"
#include "xfa/fxfa/cxfa_ffrectangle.h"
#include "xfa/fxfa/cxfa_ffsignature.h"
#include "xfa/fxfa/cxfa_fftext.h"
#include "xfa/fxfa/cxfa_ffwidget.h"
#include "xfa/fxfa/cxfa_ffwidgethandler.h"
#include "xfa/fxfa/cxfa_fwladapterwidgetmgr.h"
#include "xfa/fxfa/cxfa_textlayout.h"
#include "xfa/fxfa/cxfa_textprovider.h"
#include "xfa/fxfa/layout/cxfa_layoutprocessor.h"
#include "xfa/fxfa/parser/cxfa_barcode.h"
#include "xfa/fxfa/parser/cxfa_binditems.h"
#include "xfa/fxfa/parser/cxfa_button.h"
#include "xfa/fxfa/parser/cxfa_checkbutton.h"
#include "xfa/fxfa/parser/cxfa_node.h"
#include "xfa/fxfa/parser/cxfa_passwordedit.h"

CXFA_FFNotify::CXFA_FFNotify(CXFA_FFDoc* doc) : doc_(doc) {}

CXFA_FFNotify::~CXFA_FFNotify() = default;

void CXFA_FFNotify::Trace(cppgc::Visitor* visitor) const {
  visitor->Trace(doc_);
}

void CXFA_FFNotify::OnPageViewEvent(CXFA_ViewLayoutItem* pSender,
                                    CXFA_FFDoc::PageViewEvent eEvent) {
  CXFA_FFDocView* pDocView = doc_->GetDocView(pSender->GetLayout());
  if (pDocView) {
    pDocView->OnPageViewEvent(pSender, eEvent);
  }
}

void CXFA_FFNotify::OnWidgetListItemAdded(CXFA_Node* pSender,
                                          const WideString& wsLabel,
                                          int32_t iIndex) {
  if (pSender->GetFFWidgetType() != XFA_FFWidgetType::kChoiceList) {
    return;
  }

  CXFA_FFWidget* pWidget = doc_->GetDocView()->GetWidgetForNode(pSender);
  for (; pWidget; pWidget = pWidget->GetNextFFWidget()) {
    if (pWidget->IsLoaded()) {
      ToDropDown(ToField(pWidget))->InsertItem(wsLabel, iIndex);
    }
  }
}

void CXFA_FFNotify::OnWidgetListItemRemoved(CXFA_Node* pSender,
                                            int32_t iIndex) {
  if (pSender->GetFFWidgetType() != XFA_FFWidgetType::kChoiceList) {
    return;
  }

  CXFA_FFWidget* pWidget = doc_->GetDocView()->GetWidgetForNode(pSender);
  for (; pWidget; pWidget = pWidget->GetNextFFWidget()) {
    if (pWidget->IsLoaded()) {
      ToDropDown(ToField(pWidget))->DeleteItem(iIndex);
    }
  }
}

CXFA_FFPageView* CXFA_FFNotify::OnCreateViewLayoutItem(CXFA_Node* pNode) {
  if (pNode->GetElementType() != XFA_Element::PageArea) {
    return nullptr;
  }

  auto* pLayout = CXFA_LayoutProcessor::FromDocument(doc_->GetXFADoc());
  return cppgc::MakeGarbageCollected<CXFA_FFPageView>(
      doc_->GetHeap()->GetAllocationHandle(), doc_->GetDocView(pLayout), pNode);
}

CXFA_FFWidget* CXFA_FFNotify::OnCreateContentLayoutItem(CXFA_Node* pNode) {
  DCHECK(pNode->GetElementType() != XFA_Element::ContentArea);
  DCHECK(pNode->GetElementType() != XFA_Element::PageArea);

  // We only need to create the widget for certain types of objects.
  if (!pNode->HasCreatedUIWidget()) {
    return nullptr;
  }

  CXFA_FFWidget* pWidget = nullptr;
  switch (pNode->GetFFWidgetType()) {
    case XFA_FFWidgetType::kBarcode: {
      auto* child = CXFA_Barcode::FromNode(pNode->GetUIChildNode());
      if (!child) {
        return nullptr;
      }

      pWidget = cppgc::MakeGarbageCollected<CXFA_FFBarcode>(
          doc_->GetHeap()->GetAllocationHandle(), pNode, child);
      break;
    }
    case XFA_FFWidgetType::kButton: {
      auto* child = CXFA_Button::FromNode(pNode->GetUIChildNode());
      if (!child) {
        return nullptr;
      }

      pWidget = cppgc::MakeGarbageCollected<CXFA_FFPushButton>(
          doc_->GetHeap()->GetAllocationHandle(), pNode, child);
      break;
    }
    case XFA_FFWidgetType::kCheckButton: {
      auto* child = CXFA_CheckButton::FromNode(pNode->GetUIChildNode());
      if (!child) {
        return nullptr;
      }

      pWidget = cppgc::MakeGarbageCollected<CXFA_FFCheckButton>(
          doc_->GetHeap()->GetAllocationHandle(), pNode, child);
      break;
    }
    case XFA_FFWidgetType::kChoiceList: {
      if (pNode->IsListBox()) {
        pWidget = cppgc::MakeGarbageCollected<CXFA_FFListBox>(
            doc_->GetHeap()->GetAllocationHandle(), pNode);
      } else {
        pWidget = cppgc::MakeGarbageCollected<CXFA_FFComboBox>(
            doc_->GetHeap()->GetAllocationHandle(), pNode);
      }
      break;
    }
    case XFA_FFWidgetType::kDateTimeEdit:
      pWidget = cppgc::MakeGarbageCollected<CXFA_FFDateTimeEdit>(
          doc_->GetHeap()->GetAllocationHandle(), pNode);
      break;
    case XFA_FFWidgetType::kImageEdit:
      pWidget = cppgc::MakeGarbageCollected<CXFA_FFImageEdit>(
          doc_->GetHeap()->GetAllocationHandle(), pNode);
      break;
    case XFA_FFWidgetType::kNumericEdit:
      pWidget = cppgc::MakeGarbageCollected<CXFA_FFNumericEdit>(
          doc_->GetHeap()->GetAllocationHandle(), pNode);
      break;
    case XFA_FFWidgetType::kPasswordEdit: {
      auto* child = CXFA_PasswordEdit::FromNode(pNode->GetUIChildNode());
      if (!child) {
        return nullptr;
      }

      pWidget = cppgc::MakeGarbageCollected<CXFA_FFPasswordEdit>(
          doc_->GetHeap()->GetAllocationHandle(), pNode, child);
      break;
    }
    case XFA_FFWidgetType::kSignature:
      pWidget = cppgc::MakeGarbageCollected<CXFA_FFSignature>(
          doc_->GetHeap()->GetAllocationHandle(), pNode);
      break;
    case XFA_FFWidgetType::kTextEdit:
      pWidget = cppgc::MakeGarbageCollected<CXFA_FFTextEdit>(
          doc_->GetHeap()->GetAllocationHandle(), pNode);
      break;
    case XFA_FFWidgetType::kArc:
      pWidget = cppgc::MakeGarbageCollected<CXFA_FFArc>(
          doc_->GetHeap()->GetAllocationHandle(), pNode);
      break;
    case XFA_FFWidgetType::kLine:
      pWidget = cppgc::MakeGarbageCollected<CXFA_FFLine>(
          doc_->GetHeap()->GetAllocationHandle(), pNode);
      break;
    case XFA_FFWidgetType::kRectangle:
      pWidget = cppgc::MakeGarbageCollected<CXFA_FFRectangle>(
          doc_->GetHeap()->GetAllocationHandle(), pNode);
      break;
    case XFA_FFWidgetType::kText:
      pWidget = cppgc::MakeGarbageCollected<CXFA_FFText>(
          doc_->GetHeap()->GetAllocationHandle(), pNode);
      break;
    case XFA_FFWidgetType::kImage:
      pWidget = cppgc::MakeGarbageCollected<CXFA_FFImage>(
          doc_->GetHeap()->GetAllocationHandle(), pNode);
      break;
    case XFA_FFWidgetType::kSubform:
      pWidget = cppgc::MakeGarbageCollected<CXFA_FFWidget>(
          doc_->GetHeap()->GetAllocationHandle(), pNode);
      break;
    case XFA_FFWidgetType::kExclGroup:
      pWidget = cppgc::MakeGarbageCollected<CXFA_FFExclGroup>(
          doc_->GetHeap()->GetAllocationHandle(), pNode);
      break;
    case XFA_FFWidgetType::kNone:
      return nullptr;
  }
  auto* pLayout = CXFA_LayoutProcessor::FromDocument(doc_->GetXFADoc());
  pWidget->SetDocView(doc_->GetDocView(pLayout));
  return pWidget;
}

void CXFA_FFNotify::StartFieldDrawLayout(CXFA_Node* pItem,
                                         float* pCalcWidth,
                                         float* pCalcHeight) {
  pItem->StartWidgetLayout(doc_.Get(), pCalcWidth, pCalcHeight);
}

bool CXFA_FFNotify::RunScript(CXFA_Script* script, CXFA_Node* item) {
  CXFA_FFDocView* pDocView = doc_->GetDocView();
  if (!pDocView) {
    return false;
  }

  CXFA_EventParam EventParam(XFA_EVENT_Unknown);
  EventParam.targeted_ = false;

  CXFA_Node::BoolScriptResult result =
      item->ExecuteBoolScript(pDocView, script, &EventParam);

  return result.xfa_event_result == XFA_EventError::kSuccess &&
         result.script_result;
}

XFA_EventError CXFA_FFNotify::ExecEventByDeepFirst(CXFA_Node* pFormNode,
                                                   XFA_EVENTTYPE eEventType,
                                                   bool bIsFormReady,
                                                   bool bRecursive) {
  CXFA_FFDocView* pDocView = doc_->GetDocView();
  if (!pDocView) {
    return XFA_EventError::kNotExist;
  }
  return pDocView->ExecEventActivityByDeepFirst(pFormNode, eEventType,
                                                bIsFormReady, bRecursive);
}

void CXFA_FFNotify::AddCalcValidate(CXFA_Node* pNode) {
  CXFA_FFDocView* pDocView = doc_->GetDocView();
  if (!pDocView) {
    return;
  }

  pDocView->AddCalculateNode(pNode);
  pDocView->AddValidateNode(pNode);
}

CXFA_FFApp::CallbackIface* CXFA_FFNotify::GetAppProvider() {
  return doc_->GetApp()->GetAppProvider();
}

void CXFA_FFNotify::HandleWidgetEvent(CXFA_Node* pNode,
                                      CXFA_EventParam* pParam) {
  CXFA_FFDocView* pDocView = doc_->GetDocView();
  if (!pDocView) {
    return;
  }

  CXFA_FFWidgetHandler* pHandler = pDocView->GetWidgetHandler();
  if (!pHandler) {
    return;
  }

  pHandler->ProcessEvent(pNode, pParam);
}

void CXFA_FFNotify::OpenDropDownList(CXFA_Node* pNode) {
  auto* pDocLayout = CXFA_LayoutProcessor::FromDocument(doc_->GetXFADoc());
  CXFA_LayoutItem* pLayoutItem = pDocLayout->GetLayoutItem(pNode);
  if (!pLayoutItem) {
    return;
  }

  CXFA_FFWidget* hWidget = CXFA_FFWidget::FromLayoutItem(pLayoutItem);
  if (!hWidget) {
    return;
  }

  CXFA_FFDoc* hDoc = GetFFDoc();
  hDoc->SetFocusWidget(hWidget);
  if (hWidget->GetNode()->GetFFWidgetType() != XFA_FFWidgetType::kChoiceList) {
    return;
  }

  if (!hWidget->IsLoaded()) {
    return;
  }

  CXFA_FFDropDown* pDropDown = ToDropDown(ToField(hWidget));
  CXFA_FFComboBox* pComboBox = ToComboBox(pDropDown);
  if (!pComboBox) {
    return;
  }

  CXFA_FFDocView::UpdateScope scope(doc_->GetDocView());
  pComboBox->OpenDropDownList();
}

void CXFA_FFNotify::ResetData(CXFA_Node* pNode) {
  CXFA_FFDocView* pDocView = doc_->GetDocView();
  if (!pDocView) {
    return;
  }

  pDocView->ResetNode(pNode);
}

CXFA_FFDocView::LayoutStatus CXFA_FFNotify::GetLayoutStatus() {
  CXFA_FFDocView* pDocView = doc_->GetDocView();
  return pDocView ? pDocView->GetLayoutStatus()
                  : CXFA_FFDocView::LayoutStatus::kNone;
}

void CXFA_FFNotify::RunNodeInitialize(CXFA_Node* pNode) {
  CXFA_FFDocView* pDocView = doc_->GetDocView();
  if (!pDocView) {
    return;
  }

  pDocView->AddNewFormNode(pNode);
}

void CXFA_FFNotify::RunSubformIndexChange(CXFA_Subform* pSubformNode) {
  CXFA_FFDocView* pDocView = doc_->GetDocView();
  if (!pDocView) {
    return;
  }

  pDocView->AddIndexChangedSubform(pSubformNode);
}

CXFA_Node* CXFA_FFNotify::GetFocusWidgetNode() {
  CXFA_FFDocView* pDocView = doc_->GetDocView();
  return pDocView ? pDocView->GetFocusNode() : nullptr;
}

void CXFA_FFNotify::SetFocusWidgetNode(CXFA_Node* pNode) {
  CXFA_FFDocView* pDocView = doc_->GetDocView();
  if (!pDocView) {
    return;
  }
  pDocView->SetFocusNode(pNode);
}

void CXFA_FFNotify::OnNodeReady(CXFA_Node* pNode) {
  CXFA_FFDocView* pDocView = doc_->GetDocView();
  if (!pDocView) {
    return;
  }

  if (pNode->HasCreatedUIWidget()) {
    pNode->SetWidgetReady();
    return;
  }

  switch (pNode->GetElementType()) {
    case XFA_Element::BindItems:
      pDocView->AddBindItem(static_cast<CXFA_BindItems*>(pNode));
      break;
    case XFA_Element::Validate:
      pNode->SetFlag(XFA_NodeFlag::kNeedsInitApp);
      break;
    default:
      break;
  }
}

void CXFA_FFNotify::OnValueChanging(CXFA_Node* pSender, XFA_Attribute eAttr) {
  if (eAttr != XFA_Attribute::Presence) {
    return;
  }
  if (pSender->GetPacketType() == XFA_PacketType::Datasets) {
    return;
  }
  if (!pSender->IsFormContainer()) {
    return;
  }

  CXFA_FFDocView* pDocView = doc_->GetDocView();
  if (!pDocView) {
    return;
  }
  if (pDocView->GetLayoutStatus() != CXFA_FFDocView::LayoutStatus::kEnd) {
    return;
  }

  CXFA_FFWidget* pWidget = doc_->GetDocView()->GetWidgetForNode(pSender);
  for (; pWidget; pWidget = pWidget->GetNextFFWidget()) {
    if (pWidget->IsLoaded()) {
      pWidget->InvalidateRect();
    }
  }
}

void CXFA_FFNotify::OnValueChanged(CXFA_Node* pSender,
                                   XFA_Attribute eAttr,
                                   CXFA_Node* pParentNode,
                                   CXFA_Node* pWidgetNode) {
  CXFA_FFDocView* pDocView = doc_->GetDocView();
  if (!pDocView) {
    return;
  }

  if (pSender->GetPacketType() != XFA_PacketType::Form) {
    if (eAttr == XFA_Attribute::Value) {
      pDocView->AddCalculateNodeNotify(pSender);
    }
    return;
  }

  XFA_Element eType = pParentNode->GetElementType();
  bool bIsContainerNode = pParentNode->IsContainerNode();
  bool bUpdateProperty = false;
  pDocView->SetChangeMark();
  switch (eType) {
    case XFA_Element::Caption: {
      CXFA_TextLayout* pCapOut = pWidgetNode->GetCaptionTextLayout();
      if (!pCapOut) {
        return;
      }

      pCapOut->Unload();
      break;
    }
    case XFA_Element::Ui:
    case XFA_Element::Para:
      bUpdateProperty = true;
      break;
    default:
      break;
  }
  if (bIsContainerNode && eAttr == XFA_Attribute::Access) {
    bUpdateProperty = true;
  }

  if (eAttr == XFA_Attribute::Value) {
    pDocView->AddCalculateNodeNotify(pSender);
    if (eType == XFA_Element::Value || bIsContainerNode) {
      if (bIsContainerNode) {
        doc_->GetDocView()->UpdateUIDisplay(pWidgetNode, nullptr);
        pDocView->AddCalculateNode(pWidgetNode);
        pDocView->AddValidateNode(pWidgetNode);
      } else if (pWidgetNode->GetParent()->GetElementType() ==
                 XFA_Element::ExclGroup) {
        doc_->GetDocView()->UpdateUIDisplay(pWidgetNode, nullptr);
      }
      return;
    }
  }

  CXFA_FFWidget* pWidget = doc_->GetDocView()->GetWidgetForNode(pWidgetNode);
  for (; pWidget; pWidget = pWidget->GetNextFFWidget()) {
    if (!pWidget->IsLoaded()) {
      continue;
    }

    if (bUpdateProperty) {
      pWidget->UpdateWidgetProperty();
    }
    pWidget->PerformLayout();
    pWidget->InvalidateRect();
  }
}

void CXFA_FFNotify::OnContainerChanged() {
  doc_->GetXFADoc()->GetLayoutProcessor()->SetHasChangedContainer();
}

void CXFA_FFNotify::OnChildAdded(CXFA_Node* pSender) {
  if (!pSender->IsFormContainer()) {
    return;
  }

  CXFA_FFDocView* pDocView = doc_->GetDocView();
  if (!pDocView) {
    return;
  }

  const bool bLayoutReady =
      !pDocView->InLayoutStatus() &&
      pDocView->GetLayoutStatus() == CXFA_FFDocView::LayoutStatus::kEnd;
  if (bLayoutReady) {
    doc_->SetChangeMark();
  }
}

void CXFA_FFNotify::OnChildRemoved() {
  CXFA_FFDocView* pDocView = doc_->GetDocView();
  if (!pDocView) {
    return;
  }

  const bool bLayoutReady =
      !pDocView->InLayoutStatus() &&
      pDocView->GetLayoutStatus() == CXFA_FFDocView::LayoutStatus::kEnd;
  if (bLayoutReady) {
    doc_->SetChangeMark();
  }
}

void CXFA_FFNotify::OnLayoutItemAdded(CXFA_LayoutProcessor* pLayout,
                                      CXFA_LayoutItem* pSender,
                                      int32_t iPageIdx,
                                      Mask<XFA_WidgetStatus> dwStatus) {
  CXFA_FFDocView* pDocView = doc_->GetDocView(pLayout);
  if (!pDocView) {
    return;
  }

  CXFA_FFWidget* pWidget = CXFA_FFWidget::FromLayoutItem(pSender);
  if (!pWidget) {
    return;
  }

  CXFA_FFPageView* pNewPageView = pDocView->GetPageView(iPageIdx);
  static constexpr Mask<XFA_WidgetStatus> kRemove{XFA_WidgetStatus::kVisible,
                                                  XFA_WidgetStatus::kViewable,
                                                  XFA_WidgetStatus::kPrintable};
  pWidget->ModifyStatus(dwStatus, kRemove);
  CXFA_FFPageView* pPrePageView = pWidget->GetPageView();
  if (pPrePageView != pNewPageView ||
      dwStatus.TestAll(
          {XFA_WidgetStatus::kVisible, XFA_WidgetStatus::kViewable})) {
    pWidget->SetPageView(pNewPageView);
    doc_->WidgetPostAdd(pWidget);
  }
  if (pDocView->GetLayoutStatus() != CXFA_FFDocView::LayoutStatus::kEnd ||
      !(dwStatus & XFA_WidgetStatus::kVisible)) {
    return;
  }
  if (pWidget->IsLoaded()) {
    if (pWidget->GetWidgetRect() != pWidget->RecacheWidgetRect()) {
      pWidget->PerformLayout();
    }
  } else {
    pWidget->LoadWidget();
  }
  pWidget->InvalidateRect();
}

void CXFA_FFNotify::OnLayoutItemRemoving(CXFA_LayoutProcessor* pLayout,
                                         CXFA_LayoutItem* pSender) {
  CXFA_FFDocView* pDocView = doc_->GetDocView(pLayout);
  if (!pDocView) {
    return;
  }

  CXFA_FFWidget* pWidget = CXFA_FFWidget::FromLayoutItem(pSender);
  if (!pWidget) {
    return;
  }

  pDocView->DeleteLayoutItem(pWidget);
  doc_->WidgetPreRemove(pWidget);
  pWidget->InvalidateRect();
}

// Copyright 2014 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "xfa/fxfa/cxfa_ffdocview.h"

#include <set>
#include <utility>

#include "core/fxcrt/check_op.h"
#include "core/fxcrt/containers/contains.h"
#include "core/fxcrt/fx_extension.h"
#include "core/fxcrt/stl_util.h"
#include "core/fxcrt/xml/cfx_xmlparser.h"
#include "fxjs/gc/container_trace.h"
#include "fxjs/xfa/cfxjse_engine.h"
#include "fxjs/xfa/cjx_object.h"
#include "xfa/fxfa/cxfa_ffapp.h"
#include "xfa/fxfa/cxfa_ffbarcode.h"
#include "xfa/fxfa/cxfa_ffcheckbutton.h"
#include "xfa/fxfa/cxfa_ffdoc.h"
#include "xfa/fxfa/cxfa_ffexclgroup.h"
#include "xfa/fxfa/cxfa_fffield.h"
#include "xfa/fxfa/cxfa_ffimage.h"
#include "xfa/fxfa/cxfa_ffimageedit.h"
#include "xfa/fxfa/cxfa_ffpageview.h"
#include "xfa/fxfa/cxfa_ffpushbutton.h"
#include "xfa/fxfa/cxfa_ffsignature.h"
#include "xfa/fxfa/cxfa_fftext.h"
#include "xfa/fxfa/cxfa_ffwidget.h"
#include "xfa/fxfa/cxfa_ffwidgethandler.h"
#include "xfa/fxfa/cxfa_fwladapterwidgetmgr.h"
#include "xfa/fxfa/cxfa_readynodeiterator.h"
#include "xfa/fxfa/cxfa_textprovider.h"
#include "xfa/fxfa/layout/cxfa_layoutprocessor.h"
#include "xfa/fxfa/parser/cxfa_acrobat.h"
#include "xfa/fxfa/parser/cxfa_binditems.h"
#include "xfa/fxfa/parser/cxfa_calculate.h"
#include "xfa/fxfa/parser/cxfa_pageset.h"
#include "xfa/fxfa/parser/cxfa_present.h"
#include "xfa/fxfa/parser/cxfa_subform.h"
#include "xfa/fxfa/parser/cxfa_validate.h"
#include "xfa/fxfa/parser/xfa_utils.h"

namespace {

bool IsValidXMLNameString(const WideString& str) {
  bool first = true;
  for (const auto ch : str) {
    if (!CFX_XMLParser::IsXMLNameChar(ch, first)) {
      return false;
    }
    first = false;
  }
  return true;
}

const XFA_AttributeValue kXFAEventActivityData[] = {
    XFA_AttributeValue::Click,      XFA_AttributeValue::Change,
    XFA_AttributeValue::DocClose,   XFA_AttributeValue::DocReady,
    XFA_AttributeValue::Enter,      XFA_AttributeValue::Exit,
    XFA_AttributeValue::Full,       XFA_AttributeValue::IndexChange,
    XFA_AttributeValue::Initialize, XFA_AttributeValue::MouseDown,
    XFA_AttributeValue::MouseEnter, XFA_AttributeValue::MouseExit,
    XFA_AttributeValue::MouseUp,    XFA_AttributeValue::PostExecute,
    XFA_AttributeValue::PostOpen,   XFA_AttributeValue::PostPrint,
    XFA_AttributeValue::PostSave,   XFA_AttributeValue::PostSign,
    XFA_AttributeValue::PostSubmit, XFA_AttributeValue::PreExecute,
    XFA_AttributeValue::PreOpen,    XFA_AttributeValue::PrePrint,
    XFA_AttributeValue::PreSave,    XFA_AttributeValue::PreSign,
    XFA_AttributeValue::PreSubmit,  XFA_AttributeValue::Ready,
    XFA_AttributeValue::Unknown,
};

}  // namespace

const pdfium::span<const XFA_AttributeValue> kXFAEventActivity{
    kXFAEventActivityData};

CXFA_FFDocView::UpdateScope::UpdateScope(CXFA_FFDocView* pDocView)
    : m_pDocView(pDocView) {
  m_pDocView->LockUpdate();
}

CXFA_FFDocView::UpdateScope::~UpdateScope() {
  m_pDocView->UnlockUpdate();
  m_pDocView->UpdateDocView();
}

CXFA_FFDocView::CXFA_FFDocView(CXFA_FFDoc* pDoc) : m_pDoc(pDoc) {}

CXFA_FFDocView::~CXFA_FFDocView() = default;

void CXFA_FFDocView::Trace(cppgc::Visitor* visitor) const {
  visitor->Trace(m_pDoc);
  visitor->Trace(m_pWidgetHandler);
  visitor->Trace(m_pFocusNode);
  visitor->Trace(m_pFocusWidget);
  ContainerTrace(visitor, m_ValidateNodes);
  ContainerTrace(visitor, m_CalculateNodes);
  ContainerTrace(visitor, m_NewAddedNodes);
  ContainerTrace(visitor, m_BindItems);
  ContainerTrace(visitor, m_IndexChangedSubforms);
}

void CXFA_FFDocView::InitLayout(CXFA_Node* pNode) {
  RunBindItems();
  ExecEventActivityByDeepFirst(pNode, XFA_EVENT_Initialize, false, true);
  ExecEventActivityByDeepFirst(pNode, XFA_EVENT_IndexChange, false, true);
}

int32_t CXFA_FFDocView::StartLayout() {
  m_iStatus = LayoutStatus::kStart;
  m_pDoc->GetXFADoc()->DoProtoMerge();
  m_pDoc->GetXFADoc()->DoDataMerge();

  int32_t iStatus = GetLayoutProcessor()->StartLayout();
  if (iStatus < 0)
    return iStatus;

  CXFA_Node* pRootItem =
      ToNode(m_pDoc->GetXFADoc()->GetXFAObject(XFA_HASHCODE_Form));
  if (!pRootItem)
    return iStatus;

  InitLayout(pRootItem);
  InitCalculate(pRootItem);
  InitValidate(pRootItem);

  ExecEventActivityByDeepFirst(pRootItem, XFA_EVENT_Ready, true, true);
  m_iStatus = LayoutStatus::kStart;
  return iStatus;
}

int32_t CXFA_FFDocView::DoLayout() {
  int32_t iStatus = GetLayoutProcessor()->DoLayout();
  if (iStatus != 100)
    return iStatus;

  m_iStatus = LayoutStatus::kDoing;
  return iStatus;
}

void CXFA_FFDocView::StopLayout() {
  CXFA_Node* pRootItem =
      ToNode(m_pDoc->GetXFADoc()->GetXFAObject(XFA_HASHCODE_Form));
  if (!pRootItem)
    return;

  CXFA_Subform* pSubformNode =
      pRootItem->GetChild<CXFA_Subform>(0, XFA_Element::Subform, false);
  if (!pSubformNode)
    return;

  CXFA_PageSet* pPageSetNode =
      pSubformNode->GetFirstChildByClass<CXFA_PageSet>(XFA_Element::PageSet);
  if (!pPageSetNode)
    return;

  RunCalculateWidgets();
  RunValidate();

  InitLayout(pPageSetNode);
  InitCalculate(pPageSetNode);
  InitValidate(pPageSetNode);

  ExecEventActivityByDeepFirst(pPageSetNode, XFA_EVENT_Ready, true, true);
  ExecEventActivityByDeepFirst(pRootItem, XFA_EVENT_Ready, false, true);
  ExecEventActivityByDeepFirst(pRootItem, XFA_EVENT_DocReady, false, true);

  RunCalculateWidgets();
  RunValidate();

  if (RunLayout())
    ExecEventActivityByDeepFirst(pRootItem, XFA_EVENT_Ready, false, true);

  m_CalculateNodes.clear();
  if (m_pFocusNode && !m_pFocusWidget)
    SetFocusNode(m_pFocusNode);

  m_iStatus = LayoutStatus::kEnd;
}

void CXFA_FFDocView::AddNullTestMsg(const WideString& msg) {
  m_NullTestMsgArray.push_back(msg);
}

void CXFA_FFDocView::ShowNullTestMsg() {
  int32_t iCount = fxcrt::CollectionSize<int32_t>(m_NullTestMsgArray);
  CXFA_FFApp* pApp = m_pDoc->GetApp();
  CXFA_FFApp::CallbackIface* pAppProvider = pApp->GetAppProvider();
  if (pAppProvider && iCount) {
    int32_t remaining = iCount > 7 ? iCount - 7 : 0;
    iCount -= remaining;
    WideString wsMsg;
    for (int32_t i = 0; i < iCount; i++)
      wsMsg += m_NullTestMsgArray[i] + L"\n";

    if (remaining > 0) {
      wsMsg += L"\n" + WideString::Format(
                           L"Message limit exceeded. Remaining %d "
                           L"validation errors not reported.",
                           remaining);
    }
    pAppProvider->MsgBox(wsMsg, pAppProvider->GetAppTitle(),
                         static_cast<uint32_t>(AlertIcon::kStatus),
                         static_cast<uint32_t>(AlertButton::kOK));
  }
  m_NullTestMsgArray.clear();
}

void CXFA_FFDocView::UpdateDocView() {
  if (IsUpdateLocked())
    return;

  LockUpdate();
  while (!m_NewAddedNodes.empty()) {
    CXFA_Node* pNode = m_NewAddedNodes.front();
    m_NewAddedNodes.pop_front();
    InitCalculate(pNode);
    InitValidate(pNode);
    ExecEventActivityByDeepFirst(pNode, XFA_EVENT_Ready, true, true);
  }

  RunSubformIndexChange();
  RunCalculateWidgets();
  RunValidate();

  ShowNullTestMsg();

  if (RunLayout() && m_bLayoutEvent)
    RunEventLayoutReady();

  m_bLayoutEvent = false;
  m_CalculateNodes.clear();
  UnlockUpdate();
}

void CXFA_FFDocView::UpdateUIDisplay(CXFA_Node* pNode, CXFA_FFWidget* pExcept) {
  CXFA_FFWidget* pWidget = GetWidgetForNode(pNode);
  CXFA_FFWidget* pNext = nullptr;
  for (; pWidget; pWidget = pNext) {
    pNext = pWidget->GetNextFFWidget();
    if (pWidget == pExcept || !pWidget->IsLoaded() ||
        (pNode->GetFFWidgetType() != XFA_FFWidgetType::kCheckButton &&
         pWidget->IsFocused())) {
      continue;
    }
    pWidget->UpdateFWLData();
    pWidget->InvalidateRect();
  }
}

int32_t CXFA_FFDocView::CountPageViews() const {
  CXFA_LayoutProcessor* pProcessor = GetLayoutProcessor();
  return pProcessor ? pProcessor->CountPages() : 0;
}

CXFA_FFPageView* CXFA_FFDocView::GetPageView(int32_t nIndex) const {
  CXFA_LayoutProcessor* pProcessor = GetLayoutProcessor();
  if (!pProcessor)
    return nullptr;

  auto* pPage = pProcessor->GetPage(nIndex);
  return pPage ? pPage->GetPageView() : nullptr;
}

CXFA_LayoutProcessor* CXFA_FFDocView::GetLayoutProcessor() const {
  return CXFA_LayoutProcessor::FromDocument(m_pDoc->GetXFADoc());
}

bool CXFA_FFDocView::ResetSingleNodeData(CXFA_Node* pNode) {
  XFA_Element eType = pNode->GetElementType();
  if (eType != XFA_Element::Field && eType != XFA_Element::ExclGroup)
    return false;

  pNode->ResetData();
  UpdateUIDisplay(pNode, nullptr);
  CXFA_Validate* validate = pNode->GetValidateIfExists();
  if (!validate)
    return true;

  AddValidateNode(pNode);
  validate->SetFlag(XFA_NodeFlag::kNeedsInitApp);
  return true;
}

void CXFA_FFDocView::ResetNode(CXFA_Node* pNode) {
  m_bLayoutEvent = true;
  bool bChanged = false;
  CXFA_Node* pFormNode = nullptr;
  if (pNode) {
    bChanged = ResetSingleNodeData(pNode);
    pFormNode = pNode;
  } else {
    pFormNode = GetRootSubform();
  }
  if (!pFormNode)
    return;

  if (pFormNode->GetElementType() != XFA_Element::Field &&
      pFormNode->GetElementType() != XFA_Element::ExclGroup) {
    CXFA_ReadyNodeIterator it(pFormNode);
    while (CXFA_Node* next_node = it.MoveToNext()) {
      bChanged |= ResetSingleNodeData(next_node);
      if (next_node->GetElementType() == XFA_Element::ExclGroup)
        it.SkipTree();
    }
  }
  if (bChanged)
    m_pDoc->SetChangeMark();
}

CXFA_FFWidget* CXFA_FFDocView::GetWidgetForNode(CXFA_Node* node) {
  return GetFFWidget(
      ToContentLayoutItem(GetLayoutProcessor()->GetLayoutItem(node)));
}

CXFA_FFWidgetHandler* CXFA_FFDocView::GetWidgetHandler() {
  if (!m_pWidgetHandler) {
    m_pWidgetHandler = cppgc::MakeGarbageCollected<CXFA_FFWidgetHandler>(
        m_pDoc->GetHeap()->GetAllocationHandle(), this);
  }
  return m_pWidgetHandler;
}

bool CXFA_FFDocView::SetFocus(CXFA_FFWidget* pNewFocus) {
  if (pNewFocus == m_pFocusWidget)
    return false;

  if (m_pFocusWidget) {
    CXFA_ContentLayoutItem* pItem = m_pFocusWidget->GetLayoutItem();
    if (pItem->TestStatusBits(XFA_WidgetStatus::kVisible) &&
        !pItem->TestStatusBits(XFA_WidgetStatus::kFocused)) {
      if (!m_pFocusWidget->IsLoaded())
        m_pFocusWidget->LoadWidget();
      if (!m_pFocusWidget->OnSetFocus(m_pFocusWidget))
        m_pFocusWidget.Clear();
    }
  }
  if (m_pFocusWidget) {
    if (!m_pFocusWidget->OnKillFocus(pNewFocus))
      return false;
  }

  if (pNewFocus) {
    if (pNewFocus->GetLayoutItem()->TestStatusBits(
            XFA_WidgetStatus::kVisible)) {
      if (!pNewFocus->IsLoaded())
        pNewFocus->LoadWidget();
      if (!pNewFocus->OnSetFocus(m_pFocusWidget))
        pNewFocus = nullptr;
    }
  }
  if (pNewFocus) {
    CXFA_Node* node = pNewFocus->GetNode();
    m_pFocusNode = node->IsWidgetReady() ? node : nullptr;
    m_pFocusWidget = pNewFocus;
  } else {
    m_pFocusNode.Clear();
    m_pFocusWidget.Clear();
  }
  return true;
}

void CXFA_FFDocView::SetFocusNode(CXFA_Node* node) {
  CXFA_FFWidget* pNewFocus = node ? GetWidgetForNode(node) : nullptr;
  if (!SetFocus(pNewFocus))
    return;

  m_pFocusNode = node;
  if (m_iStatus != LayoutStatus::kEnd)
    return;

  m_pDoc->SetFocusWidget(m_pFocusWidget);
}

void CXFA_FFDocView::DeleteLayoutItem(CXFA_FFWidget* pWidget) {
  if (m_pFocusNode != pWidget->GetNode())
    return;

  m_pFocusNode.Clear();
  m_pFocusWidget.Clear();
}

static XFA_EventError XFA_ProcessEvent(CXFA_FFDocView* pDocView,
                                       CXFA_Node* pNode,
                                       CXFA_EventParam* pParam) {
  if (!pParam || pParam->m_eType == XFA_EVENT_Unknown)
    return XFA_EventError::kNotExist;
  if (pNode && pNode->GetElementType() == XFA_Element::Draw)
    return XFA_EventError::kNotExist;

  switch (pParam->m_eType) {
    case XFA_EVENT_Calculate:
      return pNode->ProcessCalculate(pDocView);
    case XFA_EVENT_Validate:
      if (pDocView->GetDoc()->IsValidationsEnabled())
        return pNode->ProcessValidate(pDocView, 0x01);
      return XFA_EventError::kDisabled;
    case XFA_EVENT_InitCalculate: {
      CXFA_Calculate* calc = pNode->GetCalculateIfExists();
      if (!calc)
        return XFA_EventError::kNotExist;
      if (pNode->IsUserInteractive())
        return XFA_EventError::kDisabled;
      return pNode->ExecuteScript(pDocView, calc->GetScriptIfExists(), pParam);
    }
    default:
      return pNode->ProcessEvent(pDocView, kXFAEventActivity[pParam->m_eType],
                                 pParam);
  }
}

XFA_EventError CXFA_FFDocView::ExecEventActivityByDeepFirst(
    CXFA_Node* pFormNode,
    XFA_EVENTTYPE eEventType,
    bool bIsFormReady,
    bool bRecursive) {
  if (!pFormNode)
    return XFA_EventError::kNotExist;

  XFA_Element elementType = pFormNode->GetElementType();
  if (elementType == XFA_Element::Field) {
    if (eEventType == XFA_EVENT_IndexChange)
      return XFA_EventError::kNotExist;

    if (!pFormNode->IsWidgetReady())
      return XFA_EventError::kNotExist;

    CXFA_EventParam eParam(eEventType);
    eParam.m_bIsFormReady = bIsFormReady;
    return XFA_ProcessEvent(this, pFormNode, &eParam);
  }

  XFA_EventError iRet = XFA_EventError::kNotExist;
  if (bRecursive) {
    for (CXFA_Node* pNode = pFormNode->GetFirstContainerChild(); pNode;
         pNode = pNode->GetNextContainerSibling()) {
      elementType = pNode->GetElementType();
      if (elementType != XFA_Element::Variables &&
          elementType != XFA_Element::Draw) {
        XFA_EventErrorAccumulate(
            &iRet, ExecEventActivityByDeepFirst(pNode, eEventType, bIsFormReady,
                                                bRecursive));
      }
    }
  }
  if (!pFormNode->IsWidgetReady())
    return iRet;

  CXFA_EventParam eParam(eEventType);
  eParam.m_bIsFormReady = bIsFormReady;

  XFA_EventErrorAccumulate(&iRet, XFA_ProcessEvent(this, pFormNode, &eParam));
  return iRet;
}

CXFA_FFWidget* CXFA_FFDocView::GetWidgetByName(const WideString& wsName,
                                               CXFA_FFWidget* pRefWidget) {
  if (!IsValidXMLNameString(wsName)) {
    return nullptr;
  }
  CFXJSE_Engine* pScriptContext = m_pDoc->GetXFADoc()->GetScriptContext();
  CXFA_Node* pRefNode = nullptr;
  if (pRefWidget) {
    CXFA_Node* node = pRefWidget->GetNode();
    pRefNode = node->IsWidgetReady() ? node : nullptr;
  }
  WideString wsExpression = (!pRefNode ? L"$form." : L"") + wsName;
  std::optional<CFXJSE_Engine::ResolveResult> maybeResult =
      pScriptContext->ResolveObjects(
          pRefNode, wsExpression.AsStringView(),
          Mask<XFA_ResolveFlag>{
              XFA_ResolveFlag::kChildren, XFA_ResolveFlag::kProperties,
              XFA_ResolveFlag::kSiblings, XFA_ResolveFlag::kParent});
  if (!maybeResult.has_value())
    return nullptr;

  if (maybeResult.value().type == CFXJSE_Engine::ResolveResult::Type::kNodes) {
    CXFA_Node* pNode = maybeResult.value().objects.front()->AsNode();
    if (pNode && pNode->IsWidgetReady())
      return GetWidgetForNode(pNode);
  }
  return nullptr;
}

void CXFA_FFDocView::OnPageViewEvent(CXFA_ViewLayoutItem* pSender,
                                     CXFA_FFDoc::PageViewEvent eEvent) {
  CXFA_FFPageView* pFFPageView = pSender ? pSender->GetPageView() : nullptr;
  m_pDoc->OnPageViewEvent(pFFPageView, eEvent);
}

void CXFA_FFDocView::InvalidateRect(CXFA_FFPageView* pPageView,
                                    const CFX_RectF& rtInvalidate) {
  m_pDoc->InvalidateRect(pPageView, rtInvalidate);
}

bool CXFA_FFDocView::RunLayout() {
  LockUpdate();
  m_bInLayoutStatus = true;

  CXFA_LayoutProcessor* pProcessor = GetLayoutProcessor();
  if (!pProcessor->IncrementLayout() && pProcessor->StartLayout() < 100) {
    pProcessor->DoLayout();
    UnlockUpdate();
    m_bInLayoutStatus = false;
    m_pDoc->OnPageViewEvent(nullptr, CXFA_FFDoc::PageViewEvent::kStopLayout);
    return true;
  }

  m_bInLayoutStatus = false;
  m_pDoc->OnPageViewEvent(nullptr, CXFA_FFDoc::PageViewEvent::kStopLayout);
  UnlockUpdate();
  return false;
}

void CXFA_FFDocView::RunSubformIndexChange() {
  std::set<CXFA_Node*> seen;
  while (!m_IndexChangedSubforms.empty()) {
    CXFA_Node* pSubformNode = m_IndexChangedSubforms.front();
    m_IndexChangedSubforms.pop_front();
    bool bInserted = seen.insert(pSubformNode).second;
    if (!bInserted || !pSubformNode->IsWidgetReady())
      continue;

    CXFA_EventParam eParam(XFA_EVENT_IndexChange);
    pSubformNode->ProcessEvent(this, XFA_AttributeValue::IndexChange, &eParam);
  }
}

void CXFA_FFDocView::AddNewFormNode(CXFA_Node* pNode) {
  m_NewAddedNodes.push_back(pNode);
  InitLayout(pNode);
}

void CXFA_FFDocView::AddIndexChangedSubform(CXFA_Subform* pNode) {
  if (!pdfium::Contains(m_IndexChangedSubforms, pNode))
    m_IndexChangedSubforms.push_back(pNode);
}

void CXFA_FFDocView::RunDocClose() {
  CXFA_Node* pRootItem =
      ToNode(m_pDoc->GetXFADoc()->GetXFAObject(XFA_HASHCODE_Form));
  if (!pRootItem)
    return;

  ExecEventActivityByDeepFirst(pRootItem, XFA_EVENT_DocClose, false, true);
}

void CXFA_FFDocView::AddCalculateNode(CXFA_Node* node) {
  CXFA_Node* pCurrentNode =
      !m_CalculateNodes.empty() ? m_CalculateNodes.back() : nullptr;
  if (pCurrentNode != node)
    m_CalculateNodes.push_back(node);
}

void CXFA_FFDocView::AddCalculateNodeNotify(CXFA_Node* pNodeChange) {
  CJX_Object::CalcData* pGlobalData = pNodeChange->JSObject()->GetCalcData();
  if (!pGlobalData)
    return;

  for (auto& pResult : pGlobalData->m_Globals) {
    if (!pResult->HasRemovedChildren() && pResult->IsWidgetReady())
      AddCalculateNode(pResult);
  }
}

size_t CXFA_FFDocView::RunCalculateRecursive(size_t index) {
  while (index < m_CalculateNodes.size()) {
    CXFA_Node* node = m_CalculateNodes[index];

    AddCalculateNodeNotify(node);
    size_t recurse = node->JSObject()->GetCalcRecursionCount() + 1;
    node->JSObject()->SetCalcRecursionCount(recurse);
    if (recurse > 11)
      break;
    if (node->ProcessCalculate(this) == XFA_EventError::kSuccess &&
        node->IsWidgetReady()) {
      AddValidateNode(node);
    }

    index = RunCalculateRecursive(++index);
  }
  return index;
}

XFA_EventError CXFA_FFDocView::RunCalculateWidgets() {
  if (!m_pDoc->IsCalculationsEnabled())
    return XFA_EventError::kDisabled;

  if (!m_CalculateNodes.empty())
    RunCalculateRecursive(0);

  for (CXFA_Node* node : m_CalculateNodes)
    node->JSObject()->SetCalcRecursionCount(0);

  m_CalculateNodes.clear();
  return XFA_EventError::kSuccess;
}

void CXFA_FFDocView::AddValidateNode(CXFA_Node* node) {
  if (!pdfium::Contains(m_ValidateNodes, node))
    m_ValidateNodes.push_back(node);
}

void CXFA_FFDocView::InitCalculate(CXFA_Node* pNode) {
  ExecEventActivityByDeepFirst(pNode, XFA_EVENT_InitCalculate, false, true);
}

void CXFA_FFDocView::ProcessValueChanged(CXFA_Node* node) {
  AddValidateNode(node);
  AddCalculateNode(node);
  RunCalculateWidgets();
  RunValidate();
}

void CXFA_FFDocView::InitValidate(CXFA_Node* pNode) {
  if (!m_pDoc->IsValidationsEnabled())
    return;

  ExecEventActivityByDeepFirst(pNode, XFA_EVENT_Validate, false, true);
  m_ValidateNodes.clear();
}

void CXFA_FFDocView::RunValidate() {
  if (!m_pDoc->IsValidationsEnabled())
    return;

  while (!m_ValidateNodes.empty()) {
    CXFA_Node* node = m_ValidateNodes.front();
    m_ValidateNodes.pop_front();
    if (!node->HasRemovedChildren())
      node->ProcessValidate(this, 0);
  }
}

bool CXFA_FFDocView::RunEventLayoutReady() {
  CXFA_Node* pRootItem =
      ToNode(m_pDoc->GetXFADoc()->GetXFAObject(XFA_HASHCODE_Form));
  if (!pRootItem)
    return false;

  ExecEventActivityByDeepFirst(pRootItem, XFA_EVENT_Ready, false, true);
  RunLayout();
  return true;
}

void CXFA_FFDocView::RunBindItems() {
  while (!m_BindItems.empty()) {
    CXFA_BindItems* item = m_BindItems.front();
    m_BindItems.pop_front();
    if (item->HasRemovedChildren())
      continue;

    CXFA_Node* pWidgetNode = item->GetParent();
    if (!pWidgetNode || !pWidgetNode->IsWidgetReady())
      continue;

    CFXJSE_Engine* pScriptContext =
        pWidgetNode->GetDocument()->GetScriptContext();
    WideString wsRef = item->GetRef();
    std::optional<CFXJSE_Engine::ResolveResult> maybeRS =
        pScriptContext->ResolveObjects(
            pWidgetNode, wsRef.AsStringView(),
            Mask<XFA_ResolveFlag>{
                XFA_ResolveFlag::kChildren, XFA_ResolveFlag::kProperties,
                XFA_ResolveFlag::kSiblings, XFA_ResolveFlag::kParent,
                XFA_ResolveFlag::kALL});
    pWidgetNode->DeleteItem(-1, false, false);
    if (!maybeRS.has_value() ||
        maybeRS.value().type != CFXJSE_Engine::ResolveResult::Type::kNodes ||
        maybeRS.value().objects.empty()) {
      continue;
    }
    WideString wsValueRef = item->GetValueRef();
    WideString wsLabelRef = item->GetLabelRef();
    const bool bUseValue = wsLabelRef.IsEmpty() || wsLabelRef == wsValueRef;
    const bool bLabelUseContent =
        wsLabelRef.IsEmpty() || wsLabelRef.EqualsASCII("$");
    const bool bValueUseContent =
        wsValueRef.IsEmpty() || wsValueRef.EqualsASCII("$");
    WideString wsValue;
    WideString wsLabel;
    uint32_t uValueHash = FX_HashCode_GetW(wsValueRef.AsStringView());
    for (auto& refObject : maybeRS.value().objects) {
      CXFA_Node* refNode = refObject->AsNode();
      if (!refNode)
        continue;

      if (bValueUseContent) {
        wsValue = refNode->JSObject()->GetContent(false);
      } else {
        CXFA_Node* nodeValue = refNode->GetFirstChildByName(uValueHash);
        wsValue = nodeValue ? nodeValue->JSObject()->GetContent(false)
                            : refNode->JSObject()->GetContent(false);
      }

      if (!bUseValue) {
        if (bLabelUseContent) {
          wsLabel = refNode->JSObject()->GetContent(false);
        } else {
          CXFA_Node* nodeLabel =
              refNode->GetFirstChildByName(wsLabelRef.AsStringView());
          if (nodeLabel)
            wsLabel = nodeLabel->JSObject()->GetContent(false);
        }
      } else {
        wsLabel = wsValue;
      }
      pWidgetNode->InsertItem(wsLabel, wsValue, false);
    }
  }
}

void CXFA_FFDocView::SetChangeMark() {
  if (m_iStatus != LayoutStatus::kEnd)
    return;

  m_pDoc->SetChangeMark();
}

CXFA_Node* CXFA_FFDocView::GetRootSubform() {
  CXFA_Node* pFormPacketNode =
      ToNode(m_pDoc->GetXFADoc()->GetXFAObject(XFA_HASHCODE_Form));
  if (!pFormPacketNode)
    return nullptr;

  return pFormPacketNode->GetFirstChildByClass<CXFA_Subform>(
      XFA_Element::Subform);
}

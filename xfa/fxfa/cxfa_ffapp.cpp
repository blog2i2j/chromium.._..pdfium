// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "xfa/fxfa/cxfa_ffapp.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "xfa/fwl/cfwl_notedriver.h"
#include "xfa/fwl/cfwl_widgetmgr.h"
#include "xfa/fxfa/cxfa_ffdoc.h"
#include "xfa/fxfa/cxfa_ffwidgethandler.h"
#include "xfa/fxfa/cxfa_fontmgr.h"
#include "xfa/fxfa/cxfa_fwladapterwidgetmgr.h"
#include "xfa/fxfa/cxfa_fwltheme.h"

CXFA_FFApp::CXFA_FFApp(IXFA_AppProvider* pProvider)
    : m_pProvider(pProvider), m_pXFAFontMgr(std::make_unique<CXFA_FontMgr>()) {
  // Ensure fully initialized before making objects based on |this|.
  m_pFWLApp = cppgc::MakeGarbageCollected<CFWL_App>(
      GetHeap()->GetAllocationHandle(), this);
}

CXFA_FFApp::~CXFA_FFApp() = default;

void CXFA_FFApp::Trace(cppgc::Visitor* visitor) const {
  visitor->Trace(m_pAdapterWidgetMgr);
  visitor->Trace(m_pFWLTheme);
  visitor->Trace(m_pFWLApp);
}

bool CXFA_FFApp::LoadFWLTheme(CXFA_FFDoc* doc) {
  auto* fwl_theme = cppgc::MakeGarbageCollected<CXFA_FWLTheme>(
      GetHeap()->GetAllocationHandle(), this);
  if (!fwl_theme->LoadCalendarFont(doc))
    return false;

  m_pFWLTheme = fwl_theme;
  return true;
}

CFWL_WidgetMgr::AdapterIface* CXFA_FFApp::GetWidgetMgrAdapter() {
  if (!m_pAdapterWidgetMgr) {
    m_pAdapterWidgetMgr = cppgc::MakeGarbageCollected<CXFA_FWLAdapterWidgetMgr>(
        GetHeap()->GetAllocationHandle());
  }
  return m_pAdapterWidgetMgr;
}

TimerHandlerIface* CXFA_FFApp::GetTimerHandler() {
  return m_pProvider->GetTimerHandler();
}

IFWL_ThemeProvider* CXFA_FFApp::GetThemeProvider() {
  return m_pFWLTheme;
}

cppgc::Heap* CXFA_FFApp::GetHeap() {
  return m_pProvider->GetGCHeap();
}

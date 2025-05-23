// Copyright 2016 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fxge/cfx_gemodule.h"

#include "core/fxcrt/check.h"
#include "core/fxge/cfx_folderfontinfo.h"
#include "core/fxge/cfx_fontcache.h"
#include "core/fxge/cfx_fontmgr.h"

namespace {

CFX_GEModule* g_pGEModule = nullptr;

}  // namespace

CFX_GEModule::CFX_GEModule(const char** pUserFontPaths)
    : platform_(PlatformIface::Create()),
      font_mgr_(std::make_unique<CFX_FontMgr>()),
      font_cache_(std::make_unique<CFX_FontCache>()),
      user_font_paths_(pUserFontPaths) {}

CFX_GEModule::~CFX_GEModule() = default;

// static
void CFX_GEModule::Create(const char** pUserFontPaths) {
  DCHECK(!g_pGEModule);
  g_pGEModule = new CFX_GEModule(pUserFontPaths);
  g_pGEModule->platform_->Init();
  g_pGEModule->GetFontMgr()->GetBuiltinMapper()->SetSystemFontInfo(
      g_pGEModule->platform_->CreateDefaultSystemFontInfo());
}

// static
void CFX_GEModule::Destroy() {
  DCHECK(g_pGEModule);
  delete g_pGEModule;
  g_pGEModule = nullptr;
}

// static
CFX_GEModule* CFX_GEModule::Get() {
  DCHECK(g_pGEModule);
  return g_pGEModule;
}

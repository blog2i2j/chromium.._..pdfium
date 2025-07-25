// Copyright 2016 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fpdfapi/render/cpdf_renderstatus.h"

#include <stdint.h>

#include <algorithm>
#include <memory>
#include <numeric>
#include <set>
#include <utility>
#include <vector>

#include "build/build_config.h"
#include "constants/transparency.h"
#include "core/fpdfapi/font/cpdf_font.h"
#include "core/fpdfapi/font/cpdf_type3char.h"
#include "core/fpdfapi/font/cpdf_type3font.h"
#include "core/fpdfapi/page/cpdf_docpagedata.h"
#include "core/fpdfapi/page/cpdf_form.h"
#include "core/fpdfapi/page/cpdf_formobject.h"
#include "core/fpdfapi/page/cpdf_function.h"
#include "core/fpdfapi/page/cpdf_graphicstates.h"
#include "core/fpdfapi/page/cpdf_image.h"
#include "core/fpdfapi/page/cpdf_imageobject.h"
#include "core/fpdfapi/page/cpdf_occontext.h"
#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/page/cpdf_pageimagecache.h"
#include "core/fpdfapi/page/cpdf_pageobject.h"
#include "core/fpdfapi/page/cpdf_pathobject.h"
#include "core/fpdfapi/page/cpdf_shadingobject.h"
#include "core/fpdfapi/page/cpdf_shadingpattern.h"
#include "core/fpdfapi/page/cpdf_textobject.h"
#include "core/fpdfapi/page/cpdf_tilingpattern.h"
#include "core/fpdfapi/page/cpdf_transferfunc.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/parser/fpdf_parser_utility.h"
#include "core/fpdfapi/render/charposlist.h"
#include "core/fpdfapi/render/cpdf_docrenderdata.h"
#include "core/fpdfapi/render/cpdf_imagerenderer.h"
#include "core/fpdfapi/render/cpdf_rendercontext.h"
#include "core/fpdfapi/render/cpdf_renderoptions.h"
#include "core/fpdfapi/render/cpdf_rendershading.h"
#include "core/fpdfapi/render/cpdf_rendertiling.h"
#include "core/fpdfapi/render/cpdf_textrenderer.h"
#include "core/fpdfapi/render/cpdf_type3cache.h"
#include "core/fxcrt/autorestorer.h"
#include "core/fxcrt/check.h"
#include "core/fxcrt/compiler_specific.h"
#include "core/fxcrt/containers/contains.h"
#include "core/fxcrt/data_vector.h"
#include "core/fxcrt/fx_2d_size.h"
#include "core/fxcrt/fx_safe_types.h"
#include "core/fxcrt/fx_system.h"
#include "core/fxcrt/notreached.h"
#include "core/fxcrt/span.h"
#include "core/fxcrt/stl_util.h"
#include "core/fxcrt/unowned_ptr.h"
#include "core/fxge/agg/cfx_agg_imagerenderer.h"
#include "core/fxge/cfx_defaultrenderdevice.h"
#include "core/fxge/cfx_fillrenderoptions.h"
#include "core/fxge/cfx_glyphbitmap.h"
#include "core/fxge/cfx_path.h"
#include "core/fxge/dib/cfx_dibitmap.h"
#include "core/fxge/fx_font.h"
#include "core/fxge/renderdevicedriver_iface.h"
#include "core/fxge/text_char_pos.h"
#include "core/fxge/text_glyph_pos.h"

#if BUILDFLAG(IS_WIN)
#include "core/fpdfapi/render/cpdf_scaledrenderbuffer.h"
#endif

namespace {

constexpr int kRenderMaxRecursionDepth = 64;
int g_CurrentRecursionDepth = 0;

CFX_FillRenderOptions GetFillOptionsForDrawPathWithBlend(
    const CPDF_RenderOptions::Options& options,
    const CPDF_PathObject* path_obj,
    CFX_FillRenderOptions::FillType fill_type,
    bool is_stroke,
    bool is_type3_char) {
  CFX_FillRenderOptions fill_options(fill_type);
  if (fill_type != CFX_FillRenderOptions::FillType::kNoFill &&
      options.bRectAA) {
    fill_options.rect_aa = true;
  }
  if (options.bNoPathSmooth) {
    fill_options.aliased_path = true;
  }
  if (path_obj->general_state().GetStrokeAdjust()) {
    fill_options.adjust_stroke = true;
  }
  if (is_stroke) {
    fill_options.stroke = true;
  }
  if (is_type3_char) {
    fill_options.text_mode = true;
  }

  return fill_options;
}

CFX_FillRenderOptions GetFillOptionsForDrawTextPath(
    const CPDF_RenderOptions::Options& options,
    const CPDF_TextObject* text_obj,
    bool is_stroke,
    bool is_fill) {
  CFX_FillRenderOptions fill_options;
  if (is_stroke && is_fill) {
    fill_options.stroke = true;
    fill_options.stroke_text_mode = true;
  }
  if (text_obj->general_state().GetStrokeAdjust()) {
    fill_options.adjust_stroke = true;
  }
  if (options.bNoTextSmooth) {
    fill_options.aliased_path = true;
  }

  return fill_options;
}

FXDIB_Format GetFormatForLuminosity(bool is_luminosity) {
  if (!is_luminosity) {
    return FXDIB_Format::k8bppMask;
  }
#if BUILDFLAG(IS_APPLE)
  return FXDIB_Format::kBgrx;
#else
  if (CFX_DefaultRenderDevice::UseSkiaRenderer()) {
    return FXDIB_Format::kBgrx;
  }
  return FXDIB_Format::kBgr;
#endif
}

bool IsAvailableMatrix(const CFX_Matrix& matrix) {
  if (matrix.a == 0 || matrix.d == 0) {
    return matrix.b != 0 && matrix.c != 0;
  }

  if (matrix.b == 0 || matrix.c == 0) {
    return matrix.a != 0 && matrix.d != 0;
  }

  return true;
}

bool MissingFillColor(const CPDF_ColorState* pColorState) {
  return !pColorState->HasRef() || pColorState->GetFillColor()->IsNull();
}

bool MissingStrokeColor(const CPDF_ColorState* pColorState) {
  return !pColorState->HasRef() || pColorState->GetStrokeColor()->IsNull();
}

bool Type3CharMissingFillColor(const CPDF_Type3Char* pChar,
                               const CPDF_ColorState* pColorState) {
  return pChar && (!pChar->colored() || MissingFillColor(pColorState));
}

bool Type3CharMissingStrokeColor(const CPDF_Type3Char* pChar,
                                 const CPDF_ColorState* pColorState) {
  return pChar && (!pChar->colored() || MissingStrokeColor(pColorState));
}

}  // namespace

CPDF_RenderStatus::CPDF_RenderStatus(CPDF_RenderContext* pContext,
                                     CFX_RenderDevice* pDevice)
    : context_(pContext), device_(pDevice) {}

CPDF_RenderStatus::~CPDF_RenderStatus() = default;

void CPDF_RenderStatus::Initialize(const CPDF_RenderStatus* pParentStatus,
                                   const CPDF_GraphicStates* pInitialStates) {
#if BUILDFLAG(IS_WIN)
  print_ = device_->GetDeviceType() == DeviceType::kPrinter;
#endif
  page_resource_.Reset(context_->GetPageResources());
  if (pInitialStates && !type3_char_) {
    initial_states_ = *pInitialStates;
    if (pParentStatus) {
      if (!initial_states_.color_state().HasFillColor()) {
        initial_states_.mutable_color_state().SetFillColorRef(
            pParentStatus->initial_states_.color_state().GetFillColorRef());
        *initial_states_.mutable_color_state().GetMutableFillColor() =
            *pParentStatus->initial_states_.color_state().GetFillColor();
      }
      if (!initial_states_.color_state().HasStrokeColor()) {
        initial_states_.mutable_color_state().SetStrokeColorRef(
            pParentStatus->initial_states_.color_state().GetFillColorRef());
        *initial_states_.mutable_color_state().GetMutableStrokeColor() =
            *pParentStatus->initial_states_.color_state().GetStrokeColor();
      }
    }
  } else {
    initial_states_.SetDefaultStates();
  }
}

void CPDF_RenderStatus::RenderObjectList(
    const CPDF_PageObjectHolder* pObjectHolder,
    const CFX_Matrix& mtObj2Device) {
  CFX_FloatRect clip_rect = mtObj2Device.GetInverse().TransformRect(
      CFX_FloatRect(device_->GetClipBox()));
  for (const auto& pCurObj : *pObjectHolder) {
    if (pCurObj.get() == stop_obj_) {
      stopped_ = true;
      return;
    }
    if (!pCurObj || !pCurObj->IsActive()) {
      continue;
    }

    if (pCurObj->GetRect().left > clip_rect.right ||
        pCurObj->GetRect().right < clip_rect.left ||
        pCurObj->GetRect().bottom > clip_rect.top ||
        pCurObj->GetRect().top < clip_rect.bottom) {
      continue;
    }
    RenderSingleObject(pCurObj.get(), mtObj2Device);
    if (stopped_) {
      return;
    }
  }
}

void CPDF_RenderStatus::RenderSingleObject(CPDF_PageObject* pObj,
                                           const CFX_Matrix& mtObj2Device) {
  AutoRestorer<int> restorer(&g_CurrentRecursionDepth);
  if (++g_CurrentRecursionDepth > kRenderMaxRecursionDepth) {
    return;
  }
  cur_obj_ = pObj;
  if (!options_.CheckPageObjectVisible(pObj)) {
    return;
  }
  ProcessClipPath(pObj->clip_path(), mtObj2Device);
  if (ProcessTransparency(pObj, mtObj2Device)) {
    return;
  }
  ProcessObjectNoClip(pObj, mtObj2Device);
}

bool CPDF_RenderStatus::ContinueSingleObject(CPDF_PageObject* pObj,
                                             const CFX_Matrix& mtObj2Device,
                                             PauseIndicatorIface* pPause) {
  if (image_renderer_) {
    if (image_renderer_->Continue(pPause)) {
      return true;
    }

    if (!image_renderer_->GetResult()) {
      DrawObjWithBackground(pObj, mtObj2Device);
    }
    image_renderer_.reset();
    return false;
  }

  cur_obj_ = pObj;
  if (!options_.CheckPageObjectVisible(pObj)) {
    return false;
  }

  ProcessClipPath(pObj->clip_path(), mtObj2Device);
  if (ProcessTransparency(pObj, mtObj2Device)) {
    return false;
  }

  if (!pObj->IsImage()) {
    ProcessObjectNoClip(pObj, mtObj2Device);
    return false;
  }

  image_renderer_ = std::make_unique<CPDF_ImageRenderer>(this);
  if (!image_renderer_->Start(pObj->AsImage(), mtObj2Device,
                              /*bStdCS=*/false)) {
    if (!image_renderer_->GetResult()) {
      DrawObjWithBackground(pObj, mtObj2Device);
    }
    image_renderer_.reset();
    return false;
  }
  return ContinueSingleObject(pObj, mtObj2Device, pPause);
}

FX_RECT CPDF_RenderStatus::GetObjectClippedRect(
    const CPDF_PageObject* pObj,
    const CFX_Matrix& mtObj2Device) const {
  FX_RECT rect = pObj->GetTransformedBBox(mtObj2Device);
  rect.Intersect(device_->GetClipBox());
  return rect;
}

void CPDF_RenderStatus::ProcessObjectNoClip(CPDF_PageObject* pObj,
                                            const CFX_Matrix& mtObj2Device) {
  bool bRet = false;
  switch (pObj->GetType()) {
    case CPDF_PageObject::Type::kText:
      bRet = ProcessText(pObj->AsText(), mtObj2Device, nullptr);
      break;
    case CPDF_PageObject::Type::kPath:
      bRet = ProcessPath(pObj->AsPath(), mtObj2Device);
      break;
    case CPDF_PageObject::Type::kImage:
      bRet = ProcessImage(pObj->AsImage(), mtObj2Device);
      break;
    case CPDF_PageObject::Type::kShading:
      ProcessShading(pObj->AsShading(), mtObj2Device);
      return;
    case CPDF_PageObject::Type::kForm:
      bRet = ProcessForm(pObj->AsForm(), mtObj2Device);
      break;
  }
  if (!bRet) {
    DrawObjWithBackground(pObj, mtObj2Device);
  }
}

bool CPDF_RenderStatus::DrawObjWithBlend(CPDF_PageObject* pObj,
                                         const CFX_Matrix& mtObj2Device) {
  switch (pObj->GetType()) {
    case CPDF_PageObject::Type::kPath:
      return ProcessPath(pObj->AsPath(), mtObj2Device);
    case CPDF_PageObject::Type::kImage:
      return ProcessImage(pObj->AsImage(), mtObj2Device);
    case CPDF_PageObject::Type::kForm:
      return ProcessForm(pObj->AsForm(), mtObj2Device);
    case CPDF_PageObject::Type::kText:
    case CPDF_PageObject::Type::kShading:
      return false;
  }
}

void CPDF_RenderStatus::DrawObjWithBackground(CPDF_PageObject* pObj,
                                              const CFX_Matrix& mtObj2Device) {
  FX_RECT rect = GetObjectClippedRect(pObj, mtObj2Device);
  if (rect.IsEmpty()) {
    return;
  }

  const bool needs_buffer =
      !(device_->GetDeviceCaps(FXDC_RENDER_CAPS) & FXRC_GET_BITS);
  if (!needs_buffer) {
    DrawObjWithBackgroundToDevice(pObj, mtObj2Device, device_, CFX_Matrix());
    return;
  }

#if BUILDFLAG(IS_WIN)
  CPDF_ScaledRenderBuffer buffer(device_, rect);
  int res = (pObj->IsImage() && IsPrint()) ? 0 : 300;
  if (!buffer.Initialize(context_, pObj, options_, res)) {
    return;
  }

  DrawObjWithBackgroundToDevice(pObj, mtObj2Device, buffer.GetDevice(),
                                buffer.GetMatrix());
  buffer.OutputToDevice();
#else
  NOTREACHED();
#endif
}

void CPDF_RenderStatus::DrawObjWithBackgroundToDevice(
    CPDF_PageObject* obj,
    const CFX_Matrix& object_to_device,
    CFX_RenderDevice* device,
    const CFX_Matrix& device_matrix) {
  RetainPtr<const CPDF_Dictionary> pFormResource;
  const CPDF_FormObject* pFormObj = obj->AsForm();
  if (pFormObj) {
    pFormResource = pFormObj->form()->GetDict()->GetDictFor("Resources");
  }

  CPDF_RenderStatus status(context_, device);
  status.SetOptions(options_);
  status.SetDeviceMatrix(device_matrix);
  status.SetTransparency(transparency_);
  status.SetDropObjects(drop_objects_);
  status.SetFormResource(std::move(pFormResource));
  status.SetInGroup(in_group_);
  status.Initialize(nullptr, nullptr);
  status.RenderSingleObject(obj, object_to_device * device_matrix);
}

bool CPDF_RenderStatus::ProcessForm(const CPDF_FormObject* pFormObj,
                                    const CFX_Matrix& mtObj2Device) {
  RetainPtr<const CPDF_Dictionary> pOC =
      pFormObj->form()->GetDict()->GetDictFor("OC");
  if (pOC && !options_.CheckOCGDictVisible(pOC.Get())) {
    return true;
  }

  CFX_Matrix matrix = pFormObj->form_matrix() * mtObj2Device;
  RetainPtr<const CPDF_Dictionary> pResources =
      pFormObj->form()->GetDict()->GetDictFor("Resources");
  CPDF_RenderStatus status(context_, device_);
  status.SetOptions(options_);
  status.SetStopObject(stop_obj_);
  status.SetTransparency(transparency_);
  status.SetDropObjects(drop_objects_);
  status.SetFormResource(std::move(pResources));
  status.SetInGroup(in_group_);
  status.Initialize(this, &pFormObj->graphic_states());
  {
    CFX_RenderDevice::StateRestorer restorer(device_);
    status.RenderObjectList(pFormObj->form(), matrix);
    stopped_ = status.stopped_;
  }
  return true;
}

bool CPDF_RenderStatus::ProcessPath(CPDF_PathObject* path_obj,
                                    const CFX_Matrix& mtObj2Device) {
  CFX_FillRenderOptions::FillType fill_type = path_obj->filltype();
  bool stroke = path_obj->stroke();
  ProcessPathPattern(path_obj, mtObj2Device, &fill_type, &stroke);
  if (fill_type == CFX_FillRenderOptions::FillType::kNoFill && !stroke) {
    return true;
  }

  // If the option to convert fill paths to stroke is enabled for forced color,
  // set |fill_type| to FillType::kNoFill and |stroke| to true.
  CPDF_RenderOptions::Options& options = options_.GetOptions();
  if (options_.ColorModeIs(CPDF_RenderOptions::Type::kForcedColor) &&
      options.bConvertFillToStroke &&
      fill_type != CFX_FillRenderOptions::FillType::kNoFill) {
    stroke = true;
    fill_type = CFX_FillRenderOptions::FillType::kNoFill;
  }

  uint32_t fill_argb = fill_type != CFX_FillRenderOptions::FillType::kNoFill
                           ? GetFillArgb(path_obj)
                           : 0;
  uint32_t stroke_argb = stroke ? GetStrokeArgb(path_obj) : 0;
  CFX_Matrix path_matrix = path_obj->matrix() * mtObj2Device;
  if (!IsAvailableMatrix(path_matrix)) {
    return true;
  }

  return device_->DrawPath(
      *path_obj->path().GetObject(), &path_matrix,
      path_obj->graph_state().GetObject(), fill_argb, stroke_argb,
      GetFillOptionsForDrawPathWithBlend(options, path_obj, fill_type, stroke,
                                         type3_char_));
}

RetainPtr<CPDF_TransferFunc> CPDF_RenderStatus::GetTransferFunc(
    RetainPtr<const CPDF_Object> pObj) const {
  DCHECK(pObj);
  auto* pDocCache = CPDF_DocRenderData::FromDocument(context_->GetDocument());
  return pDocCache ? pDocCache->GetTransferFunc(std::move(pObj)) : nullptr;
}

FX_ARGB CPDF_RenderStatus::GetFillArgb(CPDF_PageObject* pObj) const {
  if (Type3CharMissingFillColor(type3_char_, &pObj->color_state())) {
    return t3_fill_color_;
  }

  return GetFillArgbForType3(pObj);
}

FX_ARGB CPDF_RenderStatus::GetFillArgbForType3(CPDF_PageObject* pObj) const {
  const CPDF_ColorState* pColorState = &pObj->color_state();
  if (MissingFillColor(pColorState)) {
    pColorState = &initial_states_.color_state();
  }

  FX_COLORREF colorref = pColorState->GetFillColorRef();
  if (colorref == 0xFFFFFFFF) {
    return 0;
  }

  int32_t alpha =
      static_cast<int32_t>((pObj->general_state().GetFillAlpha() * 255));
  RetainPtr<const CPDF_Object> pTR = pObj->general_state().GetTR();
  if (pTR) {
    if (!pObj->general_state().GetTransferFunc()) {
      pObj->mutable_general_state().SetTransferFunc(
          GetTransferFunc(std::move(pTR)));
    }
    if (pObj->general_state().GetTransferFunc()) {
      colorref =
          pObj->general_state().GetTransferFunc()->TranslateColor(colorref);
    }
  }
  return options_.TranslateObjectFillColor(
      AlphaAndColorRefToArgb(alpha, colorref), pObj->GetType());
}

FX_ARGB CPDF_RenderStatus::GetStrokeArgb(CPDF_PageObject* pObj) const {
  const CPDF_ColorState* pColorState = &pObj->color_state();
  if (Type3CharMissingStrokeColor(type3_char_, pColorState)) {
    return t3_fill_color_;
  }

  if (MissingStrokeColor(pColorState)) {
    pColorState = &initial_states_.color_state();
  }

  FX_COLORREF colorref = pColorState->GetStrokeColorRef();
  if (colorref == 0xFFFFFFFF) {
    return 0;
  }

  int32_t alpha = static_cast<int32_t>(pObj->general_state().GetStrokeAlpha() *
                                       255);  // not rounded.
  RetainPtr<const CPDF_Object> pTR = pObj->general_state().GetTR();
  if (pTR) {
    if (!pObj->general_state().GetTransferFunc()) {
      pObj->mutable_general_state().SetTransferFunc(
          GetTransferFunc(std::move(pTR)));
    }
    if (pObj->general_state().GetTransferFunc()) {
      colorref =
          pObj->general_state().GetTransferFunc()->TranslateColor(colorref);
    }
  }
  return options_.TranslateObjectStrokeColor(
      AlphaAndColorRefToArgb(alpha, colorref), pObj->GetType());
}

void CPDF_RenderStatus::ProcessClipPath(const CPDF_ClipPath& ClipPath,
                                        const CFX_Matrix& mtObj2Device) {
  if (!ClipPath.HasRef()) {
    if (last_clip_path_.HasRef()) {
      device_->RestoreState(true);
      last_clip_path_.SetNull();
    }
    return;
  }
  if (last_clip_path_ == ClipPath) {
    return;
  }

  last_clip_path_ = ClipPath;
  device_->RestoreState(true);
  for (size_t i = 0; i < ClipPath.GetPathCount(); ++i) {
    const CFX_Path* pPath = ClipPath.GetPath(i).GetObject();
    if (!pPath) {
      continue;
    }

    if (pPath->GetPoints().empty()) {
      CFX_Path empty_path;
      empty_path.AppendRect(-1, -1, 0, 0);
      device_->SetClip_PathFill(empty_path, nullptr,
                                CFX_FillRenderOptions::WindingOptions());
    } else {
      device_->SetClip_PathFill(*pPath, &mtObj2Device,
                                CFX_FillRenderOptions(ClipPath.GetClipType(i)));
    }
  }

  if (ClipPath.GetTextCount() == 0) {
    return;
  }

  if (!IsPrint() &&
      !(device_->GetDeviceCaps(FXDC_RENDER_CAPS) & FXRC_SOFT_CLIP)) {
    return;
  }

  std::unique_ptr<CFX_Path> pTextClippingPath;
  for (size_t i = 0; i < ClipPath.GetTextCount(); ++i) {
    CPDF_TextObject* pText = ClipPath.GetText(i);
    if (pText) {
      if (!pTextClippingPath) {
        pTextClippingPath = std::make_unique<CFX_Path>();
      }
      ProcessText(pText, mtObj2Device, pTextClippingPath.get());
      continue;
    }

    if (!pTextClippingPath) {
      continue;
    }

    CFX_FillRenderOptions fill_options(CFX_FillRenderOptions::WindingOptions());
    if (options_.GetOptions().bNoTextSmooth) {
      fill_options.aliased_path = true;
    }
    device_->SetClip_PathFill(*pTextClippingPath, nullptr, fill_options);
    pTextClippingPath.reset();
  }
}

bool CPDF_RenderStatus::ClipPattern(const CPDF_PageObject* page_obj,
                                    const CFX_Matrix& mtObj2Device,
                                    bool stroke) {
  if (page_obj->IsPath()) {
    return SelectClipPath(page_obj->AsPath(), mtObj2Device, stroke);
  }
  if (page_obj->IsImage()) {
    device_->SetClip_Rect(page_obj->GetTransformedBBox(mtObj2Device));
    return true;
  }
  return false;
}

bool CPDF_RenderStatus::SelectClipPath(const CPDF_PathObject* path_obj,
                                       const CFX_Matrix& mtObj2Device,
                                       bool stroke) {
  CFX_Matrix path_matrix = path_obj->matrix() * mtObj2Device;
  if (stroke) {
    return device_->SetClip_PathStroke(*path_obj->path().GetObject(),
                                       &path_matrix,
                                       path_obj->graph_state().GetObject());
  }
  CFX_FillRenderOptions fill_options(path_obj->filltype());
  if (options_.GetOptions().bNoPathSmooth) {
    fill_options.aliased_path = true;
  }
  return device_->SetClip_PathFill(*path_obj->path().GetObject(), &path_matrix,
                                   fill_options);
}

bool CPDF_RenderStatus::ProcessTransparency(CPDF_PageObject* pPageObj,
                                            const CFX_Matrix& mtObj2Device) {
  const BlendMode blend_type = pPageObj->general_state().GetBlendType();
  RetainPtr<CPDF_Dictionary> pSMaskDict =
      pPageObj->mutable_general_state().GetMutableSoftMask();
  if (pSMaskDict) {
    if (pPageObj->IsImage() &&
        pPageObj->AsImage()->GetImage()->GetDict()->KeyExist("SMask")) {
      pSMaskDict = nullptr;
    }
  }
  RetainPtr<const CPDF_Dictionary> pFormResource;
  float group_alpha = 1.0f;
  float initial_alpha = 1.0f;
  CPDF_Transparency transparency = transparency_;
  bool bGroupTransparent = false;
  const CPDF_FormObject* pFormObj = pPageObj->AsForm();
  if (pFormObj) {
    group_alpha = pFormObj->general_state().GetFillAlpha();
    transparency = pFormObj->form()->GetTransparency();
    bGroupTransparent = transparency.IsIsolated();
    pFormResource = pFormObj->form()->GetDict()->GetDictFor("Resources");
    initial_alpha = initial_states_.general_state().GetFillAlpha();
  }
  bool bTextClip = !IsPrint() && pPageObj->clip_path().HasRef() &&
                   pPageObj->clip_path().GetTextCount() > 0 &&
                   !(device_->GetDeviceCaps(FXDC_RENDER_CAPS) & FXRC_SOFT_CLIP);
  if (!pSMaskDict && group_alpha == 1.0f && blend_type == BlendMode::kNormal &&
      !bTextClip && !bGroupTransparent && initial_alpha == 1.0f) {
    return false;
  }
#if BUILDFLAG(IS_WIN)
  if (IsPrint()) {
    DrawObjWithBackground(pPageObj, mtObj2Device);
    return true;
  }
#endif
  FX_RECT rect = pPageObj->GetTransformedBBox(mtObj2Device);
  rect.Intersect(device_->GetClipBox());
  if (rect.IsEmpty()) {
    return true;
  }

  const int width = rect.Width();
  const int height = rect.Height();
  CFX_DefaultRenderDevice bitmap_device;
  RetainPtr<CFX_DIBitmap> backdrop;
  if (!transparency.IsIsolated() &&
      (device_->GetRenderCaps() & FXRC_GET_BITS)) {
    backdrop = pdfium::MakeRetain<CFX_DIBitmap>();
    if (!device_->CreateCompatibleBitmap(backdrop, width, height)) {
      return true;
    }
    device_->GetDIBits(backdrop, rect.left, rect.top);
  }
  if (!bitmap_device.CreateWithBackdrop(
          width, height, GetCompatibleArgbFormat(), std::move(backdrop))) {
    return true;
  }

  CFX_Matrix new_matrix = mtObj2Device;
  new_matrix.Translate(-rect.left, -rect.top);

  RetainPtr<CFX_DIBitmap> text_mask_bitmap;
  if (bTextClip) {
    text_mask_bitmap = pdfium::MakeRetain<CFX_DIBitmap>();
    if (!text_mask_bitmap->Create(width, height, FXDIB_Format::k8bppMask)) {
      return true;
    }

    CFX_DefaultRenderDevice text_device;
    text_device.Attach(text_mask_bitmap);
    for (size_t i = 0; i < pPageObj->clip_path().GetTextCount(); ++i) {
      CPDF_TextObject* textobj = pPageObj->clip_path().GetText(i);
      if (!textobj) {
        break;
      }

      // TODO(thestig): Should we check the return value here?
      CPDF_TextRenderer::DrawTextPath(
          &text_device, textobj->GetCharCodes(), textobj->GetCharPositions(),
          textobj->text_state().GetFont().Get(),
          textobj->text_state().GetFontSize(), textobj->GetTextMatrix(),
          &new_matrix, textobj->graph_state().GetObject(), 0xffffffff, 0,
          nullptr, CFX_FillRenderOptions());
    }
  }
  CPDF_RenderStatus bitmap_render(context_, &bitmap_device);
  bitmap_render.SetOptions(options_);
  bitmap_render.SetStopObject(stop_obj_);
  bitmap_render.SetStdCS(true);
  bitmap_render.SetDropObjects(drop_objects_);
  bitmap_render.SetFormResource(std::move(pFormResource));
  bitmap_render.SetInGroup(transparency.IsGroup());
  bitmap_render.Initialize(nullptr, nullptr);
  bitmap_render.ProcessObjectNoClip(pPageObj, new_matrix);
  stopped_ = bitmap_render.stopped_;
  if (pSMaskDict) {
    CFX_Matrix smask_matrix =
        *pPageObj->general_state().GetSMaskMatrix() * mtObj2Device;
    RetainPtr<CFX_DIBitmap> smask_bitmap =
        LoadSMask(pSMaskDict.Get(), rect, smask_matrix);
    if (smask_bitmap) {
      bitmap_device.MultiplyAlphaMask(std::move(smask_bitmap));
    }
  }
  if (text_mask_bitmap) {
    bitmap_device.MultiplyAlphaMask(std::move(text_mask_bitmap));
  }
  if (transparency.IsGroup()) {
    bitmap_device.MultiplyAlpha(group_alpha);
  }
  if (initial_alpha != 1.0f && !in_group_) {
    bitmap_device.MultiplyAlpha(initial_alpha);
  }
  transparency = transparency_;
  if (pPageObj->IsForm()) {
    transparency.SetGroup();
  }
  CompositeDIBitmap(bitmap_device.GetBitmap(), rect.left, rect.top,
                    /*mask_argb=*/0, /*alpha=*/1.0f, blend_type, transparency);
  return true;
}

FX_RECT CPDF_RenderStatus::GetClippedBBox(const FX_RECT& rect) const {
  FX_RECT bbox = rect;
  bbox.Intersect(device_->GetClipBox());
  return bbox;
}

RetainPtr<CFX_DIBitmap> CPDF_RenderStatus::GetBackdrop(
    const CPDF_PageObject* pObj,
    const FX_RECT& bbox,
    bool bBackAlphaRequired) {
  int width = bbox.Width();
  int height = bbox.Height();
  auto backdrop = pdfium::MakeRetain<CFX_DIBitmap>();
  if (bBackAlphaRequired && !drop_objects_) {
    // TODO(crbug.com/42271020): Consider adding support for
    // `FXDIB_Format::kBgraPremul`
    if (!backdrop->Create(width, height, FXDIB_Format::kBgra)) {
      return nullptr;
    }
  } else {
    if (!device_->CreateCompatibleBitmap(backdrop, width, height)) {
      return nullptr;
    }
  }

  const int cap_to_check =
      backdrop->IsAlphaFormat() ? FXRC_ALPHA_OUTPUT : FXRC_GET_BITS;
  if (device_->GetRenderCaps() & cap_to_check) {
    device_->GetDIBits(backdrop, bbox.left, bbox.top);
    return backdrop;
  }
  CFX_Matrix FinalMatrix = device_matrix_;
  FinalMatrix.Translate(-bbox.left, -bbox.top);
  if (!backdrop->IsAlphaFormat()) {
    backdrop->Clear(0xffffffff);
  }

  CFX_DefaultRenderDevice device;
  device.Attach(backdrop);
  context_->Render(&device, pObj, &options_, &FinalMatrix);
  return backdrop;
}

std::unique_ptr<CPDF_GraphicStates> CPDF_RenderStatus::CloneObjStates(
    const CPDF_GraphicStates* pSrcStates,
    bool stroke) {
  if (!pSrcStates) {
    return nullptr;
  }

  auto pStates = std::make_unique<CPDF_GraphicStates>(*pSrcStates);
  const CPDF_Color* pObjColor = stroke
                                    ? pSrcStates->color_state().GetStrokeColor()
                                    : pSrcStates->color_state().GetFillColor();
  if (!pObjColor->IsNull()) {
    pStates->mutable_color_state().SetFillColorRef(
        stroke ? pSrcStates->color_state().GetStrokeColorRef()
               : pSrcStates->color_state().GetFillColorRef());
    pStates->mutable_color_state().SetStrokeColorRef(
        pStates->color_state().GetFillColorRef());
  }
  return pStates;
}

bool CPDF_RenderStatus::ProcessText(CPDF_TextObject* textobj,
                                    const CFX_Matrix& mtObj2Device,
                                    CFX_Path* clipping_path) {
  if (textobj->GetCharCodes().empty()) {
    return true;
  }

  const TextRenderingMode text_render_mode =
      textobj->text_state().GetTextMode();
  if (text_render_mode == TextRenderingMode::MODE_INVISIBLE) {
    return true;
  }

  RetainPtr<CPDF_Font> pFont = textobj->text_state().GetFont();
  if (pFont->IsType3Font()) {
    return ProcessType3Text(textobj, mtObj2Device);
  }

  bool is_fill = false;
  bool is_stroke = false;
  bool is_clip = false;
  if (clipping_path) {
    is_clip = true;
  } else {
    switch (text_render_mode) {
      case TextRenderingMode::MODE_FILL:
      case TextRenderingMode::MODE_FILL_CLIP:
        is_fill = true;
        break;
      case TextRenderingMode::MODE_STROKE:
      case TextRenderingMode::MODE_STROKE_CLIP:
        if (pFont->HasFace()) {
          is_stroke = true;
        } else {
          is_fill = true;
        }
        break;
      case TextRenderingMode::MODE_FILL_STROKE:
      case TextRenderingMode::MODE_FILL_STROKE_CLIP:
        is_fill = true;
        if (pFont->HasFace()) {
          is_stroke = true;
        }
        break;
      case TextRenderingMode::MODE_INVISIBLE:
        // Already handled above, but the compiler is not smart enough to
        // realize it.
        NOTREACHED();
      case TextRenderingMode::MODE_CLIP:
        return true;
      case TextRenderingMode::MODE_UNKNOWN:
        NOTREACHED();
    }
  }
  FX_ARGB stroke_argb = 0;
  FX_ARGB fill_argb = 0;
  bool bPattern = false;
  if (is_stroke) {
    if (textobj->color_state().GetStrokeColor()->IsPattern()) {
      bPattern = true;
    } else {
      stroke_argb = GetStrokeArgb(textobj);
    }
  }
  if (is_fill) {
    if (textobj->color_state().GetFillColor()->IsPattern()) {
      bPattern = true;
    } else {
      fill_argb = GetFillArgb(textobj);
    }
  }
  CFX_Matrix text_matrix = textobj->GetTextMatrix();
  if (!IsAvailableMatrix(text_matrix)) {
    return true;
  }

  float font_size = textobj->text_state().GetFontSize();
  if (bPattern) {
    DrawTextPathWithPattern(textobj, mtObj2Device, pFont.Get(), font_size,
                            text_matrix, is_fill, is_stroke);
    return true;
  }
  if (is_clip || is_stroke) {
    const CFX_Matrix* pDeviceMatrix = &mtObj2Device;
    CFX_Matrix device_matrix;
    if (is_stroke) {
      pdfium::span<const float> pCTM = textobj->text_state().GetCTM();
      if (pCTM[0] != 1.0f || pCTM[3] != 1.0f) {
        CFX_Matrix ctm(pCTM[0], pCTM[1], pCTM[2], pCTM[3], 0, 0);
        text_matrix *= ctm.GetInverse();
        device_matrix = ctm * mtObj2Device;
        pDeviceMatrix = &device_matrix;
      }
    }
    return CPDF_TextRenderer::DrawTextPath(
        device_, textobj->GetCharCodes(), textobj->GetCharPositions(),
        pFont.Get(), font_size, text_matrix, pDeviceMatrix,
        textobj->graph_state().GetObject(), fill_argb, stroke_argb,
        clipping_path,
        GetFillOptionsForDrawTextPath(options_.GetOptions(), textobj, is_stroke,
                                      is_fill));
  }
  text_matrix.Concat(mtObj2Device);
  return CPDF_TextRenderer::DrawNormalText(
      device_, textobj->GetCharCodes(), textobj->GetCharPositions(),
      pFont.Get(), font_size, text_matrix, fill_argb, options_);
}

// TODO(npm): Font fallback for type 3 fonts? (Completely separate code!!)
bool CPDF_RenderStatus::ProcessType3Text(CPDF_TextObject* textobj,
                                         const CFX_Matrix& mtObj2Device) {
  CPDF_Type3Font* pType3Font = textobj->text_state().GetFont()->AsType3Font();
  if (pdfium::Contains(type3_font_cache_, pType3Font)) {
    return true;
  }

  FX_ARGB fill_argb = GetFillArgbForType3(textobj);
  int fill_alpha = FXARGB_A(fill_argb);
#if BUILDFLAG(IS_WIN)
  if (IsPrint() && fill_alpha < 255) {
    return false;
  }
#endif

  CFX_Matrix text_matrix = textobj->GetTextMatrix();
  CFX_Matrix char_matrix = pType3Font->GetFontMatrix();
  float font_size = textobj->text_state().GetFontSize();
  char_matrix.Scale(font_size, font_size);

  // Must come before |glyphs|, because |glyphs| points into |refTypeCache|.
  std::set<RetainPtr<CPDF_Type3Cache>> refTypeCache;
  std::vector<TextGlyphPos> glyphs;
  if (!IsPrint()) {
    glyphs.resize(textobj->GetCharCodes().size());
  }

  for (size_t iChar = 0; iChar < textobj->GetCharCodes().size(); ++iChar) {
    uint32_t charcode = textobj->GetCharCodes()[iChar];
    if (charcode == static_cast<uint32_t>(-1)) {
      continue;
    }

    CPDF_Type3Char* pType3Char = pType3Font->LoadChar(charcode);
    if (!pType3Char) {
      continue;
    }

    CFX_Matrix matrix = char_matrix;
    matrix.e += iChar > 0 ? textobj->GetCharPositions()[iChar - 1] : 0;
    matrix.Concat(text_matrix);
    matrix.Concat(mtObj2Device);
    if (!pType3Char->LoadBitmapFromSoleImageOfForm()) {
      if (!glyphs.empty()) {
        for (size_t i = 0; i < iChar; ++i) {
          const TextGlyphPos& glyph = glyphs[i];
          if (!glyph.glyph_) {
            continue;
          }

          std::optional<CFX_Point> point = glyph.GetOrigin({0, 0});
          if (!point.has_value()) {
            continue;
          }

          device_->SetBitMask(glyph.glyph_->GetBitmap(), point->x, point->y,
                              fill_argb);
        }
        glyphs.clear();
      }

      std::unique_ptr<CPDF_GraphicStates> pStates =
          CloneObjStates(&textobj->graphic_states(), false);
      CPDF_RenderOptions options = options_;
      options.GetOptions().bForceHalftone = true;
      options.GetOptions().bRectAA = true;

      const auto* pForm = static_cast<const CPDF_Form*>(pType3Char->form());
      RetainPtr<const CPDF_Dictionary> pFormResource =
          pForm->GetDict()->GetDictFor("Resources");

      if (fill_alpha == 255) {
        CPDF_RenderStatus status(context_, device_);
        status.SetOptions(options);
        status.SetTransparency(pForm->GetTransparency());
        status.SetType3Char(pType3Char);
        status.SetFillColor(fill_argb);
        status.SetDropObjects(drop_objects_);
        status.SetFormResource(std::move(pFormResource));
        status.Initialize(this, pStates.get());
        status.type3_font_cache_ = type3_font_cache_;
        status.type3_font_cache_.emplace_back(pType3Font);

        CFX_RenderDevice::StateRestorer restorer(device_);
        status.RenderObjectList(pForm, matrix);
      } else {
        FX_RECT rect =
            matrix.TransformRect(pForm->CalcBoundingBox()).GetOuterRect();
        if (!rect.Valid()) {
          continue;
        }

        CFX_DefaultRenderDevice bitmap_device;
        // TODO(crbug.com/42271020): Consider adding support for
        // `FXDIB_Format::kBgraPremul`
        if (!bitmap_device.Create(rect.Width(), rect.Height(),
                                  FXDIB_Format::kBgra)) {
          return true;
        }
        CPDF_RenderStatus status(context_, &bitmap_device);
        status.SetOptions(options);
        status.SetTransparency(pForm->GetTransparency());
        status.SetType3Char(pType3Char);
        status.SetFillColor(fill_argb);
        status.SetDropObjects(drop_objects_);
        status.SetFormResource(std::move(pFormResource));
        status.Initialize(this, pStates.get());
        status.type3_font_cache_ = type3_font_cache_;
        status.type3_font_cache_.emplace_back(pType3Font);
        matrix.Translate(-rect.left, -rect.top);
        status.RenderObjectList(pForm, matrix);
        device_->SetDIBits(bitmap_device.GetBitmap(), rect.left, rect.top);
      }
    } else if (pType3Char->GetBitmap()) {
#if BUILDFLAG(IS_WIN)
      if (IsPrint()) {
        CFX_Matrix image_matrix = pType3Char->matrix() * matrix;
        CPDF_ImageRenderer renderer(this);
        if (renderer.Start(pType3Char->GetBitmap(), fill_argb, image_matrix,
                           FXDIB_ResampleOptions(), false)) {
          renderer.Continue(nullptr);
        }
        if (!renderer.GetResult()) {
          return false;
        }
        continue;
      }
#endif

      CPDF_Document* doc = pType3Font->GetDocument();
      RetainPtr<CPDF_Type3Cache> pCache =
          CPDF_DocRenderData::FromDocument(doc)->GetCachedType3(pType3Font);

      const CFX_GlyphBitmap* pBitmap = pCache->LoadGlyph(charcode, matrix);
      if (!pBitmap) {
        continue;
      }

      refTypeCache.insert(std::move(pCache));

      CFX_Point origin(FXSYS_roundf(matrix.e), FXSYS_roundf(matrix.f));
      if (glyphs.empty()) {
        FX_SAFE_INT32 left = origin.x;
        left += pBitmap->left();
        if (!left.IsValid()) {
          continue;
        }

        FX_SAFE_INT32 top = origin.y;
        top -= pBitmap->top();
        if (!top.IsValid()) {
          continue;
        }

        device_->SetBitMask(pBitmap->GetBitmap(), left.ValueOrDie(),
                            top.ValueOrDie(), fill_argb);
      } else {
        glyphs[iChar].glyph_ = pBitmap;
        glyphs[iChar].origin_ = origin;
      }
    }
  }

  if (glyphs.empty()) {
    return true;
  }

  FX_RECT rect = GetGlyphsBBox(glyphs, 0);
  auto bitmap = pdfium::MakeRetain<CFX_DIBitmap>();
  if (!bitmap->Create(rect.Width(), rect.Height(), FXDIB_Format::k8bppMask)) {
    return true;
  }

  for (const TextGlyphPos& glyph : glyphs) {
    if (!glyph.glyph_ || !glyph.glyph_->GetBitmap()->IsMaskFormat()) {
      continue;
    }

    std::optional<CFX_Point> point = glyph.GetOrigin({rect.left, rect.top});
    if (!point.has_value()) {
      continue;
    }

    bitmap->CompositeMask(
        point->x, point->y, glyph.glyph_->GetBitmap()->GetWidth(),
        glyph.glyph_->GetBitmap()->GetHeight(), glyph.glyph_->GetBitmap(),
        fill_argb, 0, 0, BlendMode::kNormal, nullptr, false);
  }
  device_->SetBitMask(std::move(bitmap), rect.left, rect.top, fill_argb);
  return true;
}

void CPDF_RenderStatus::DrawTextPathWithPattern(const CPDF_TextObject* textobj,
                                                const CFX_Matrix& mtObj2Device,
                                                CPDF_Font* pFont,
                                                float font_size,
                                                const CFX_Matrix& mtTextMatrix,
                                                bool fill,
                                                bool stroke) {
  if (!stroke) {
    std::vector<std::unique_ptr<CPDF_TextObject>> pCopy;
    pCopy.push_back(textobj->Clone());

    CPDF_PathObject path;
    path.set_filltype(CFX_FillRenderOptions::FillType::kWinding);
    path.mutable_clip_path().CopyClipPath(last_clip_path_);
    path.mutable_clip_path().AppendTexts(&pCopy);
    path.mutable_color_state() = textobj->color_state();
    path.mutable_general_state() = textobj->general_state();
    path.path().AppendFloatRect(textobj->GetRect());
    path.SetRect(textobj->GetRect());

    AutoRestorer<UnownedPtr<const CPDF_PageObject>> restorer2(&cur_obj_);
    RenderSingleObject(&path, mtObj2Device);
    return;
  }

  std::vector<TextCharPos> char_pos_list = GetCharPosList(
      textobj->GetCharCodes(), textobj->GetCharPositions(), pFont, font_size);
  for (const TextCharPos& charpos : char_pos_list) {
    auto* font = charpos.fallback_font_position_ == -1
                     ? pFont->GetFont()
                     : pFont->GetFontFallback(charpos.fallback_font_position_);
    const CFX_Path* pPath =
        font->LoadGlyphPath(charpos.glyph_index_, charpos.font_char_width_);
    if (!pPath) {
      continue;
    }

    CPDF_PathObject path;
    path.mutable_graph_state() = textobj->graph_state();
    path.mutable_color_state() = textobj->color_state();

    CFX_Matrix matrix = charpos.GetEffectiveMatrix(CFX_Matrix(
        font_size, 0, 0, font_size, charpos.origin_.x, charpos.origin_.y));
    matrix.Concat(mtTextMatrix);
    path.set_stroke(stroke);
    path.set_filltype(fill ? CFX_FillRenderOptions::FillType::kWinding
                           : CFX_FillRenderOptions::FillType::kNoFill);
    path.path().Append(*pPath, &matrix);
    path.SetPathMatrix(CFX_Matrix());
    ProcessPath(&path, mtObj2Device);
  }
}

void CPDF_RenderStatus::DrawShadingPattern(CPDF_ShadingPattern* pattern,
                                           const CPDF_PageObject* pPageObj,
                                           const CFX_Matrix& mtObj2Device,
                                           bool stroke) {
  if (!pattern->Load()) {
    return;
  }

  CFX_RenderDevice::StateRestorer restorer(device_);
  if (!ClipPattern(pPageObj, mtObj2Device, stroke)) {
    return;
  }

  FX_RECT rect = GetObjectClippedRect(pPageObj, mtObj2Device);
  if (rect.IsEmpty()) {
    return;
  }

  CFX_Matrix matrix = pattern->pattern_to_form() * mtObj2Device;
  int alpha =
      FXSYS_roundf(255 * (stroke ? pPageObj->general_state().GetStrokeAlpha()
                                 : pPageObj->general_state().GetFillAlpha()));
  CPDF_RenderShading::Draw(device_, context_, cur_obj_, pattern, matrix, rect,
                           alpha, options_);
}

void CPDF_RenderStatus::ProcessShading(const CPDF_ShadingObject* pShadingObj,
                                       const CFX_Matrix& mtObj2Device) {
  FX_RECT rect = GetObjectClippedRect(pShadingObj, mtObj2Device);
  if (rect.IsEmpty()) {
    return;
  }

  CFX_Matrix matrix = pShadingObj->matrix() * mtObj2Device;
  CPDF_RenderShading::Draw(
      device_, context_, cur_obj_, pShadingObj->pattern(), matrix, rect,
      FXSYS_roundf(255 * pShadingObj->general_state().GetFillAlpha()),
      options_);
}

void CPDF_RenderStatus::DrawTilingPattern(CPDF_TilingPattern* pattern,
                                          CPDF_PageObject* pPageObj,
                                          const CFX_Matrix& mtObj2Device,
                                          bool stroke) {
  const std::unique_ptr<CPDF_Form> pPatternForm = pattern->Load(pPageObj);
  if (!pPatternForm) {
    return;
  }

  CFX_RenderDevice::StateRestorer restorer(device_);
  if (!ClipPattern(pPageObj, mtObj2Device, stroke)) {
    return;
  }

  FX_RECT clip_box = device_->GetClipBox();
  if (clip_box.IsEmpty()) {
    return;
  }

  RetainPtr<CFX_DIBitmap> screen =
      CPDF_RenderTiling::Draw(this, pPageObj, pattern, pPatternForm.get(),
                              mtObj2Device, clip_box, stroke);
  if (!screen) {
    return;
  }

  static constexpr FX_ARGB kMask = 0;
  CompositeDIBitmap(std::move(screen), clip_box.left, clip_box.top, kMask,
                    /*alpha=*/1.0f, BlendMode::kNormal, CPDF_Transparency());
}

void CPDF_RenderStatus::DrawPathWithPattern(CPDF_PathObject* path_obj,
                                            const CFX_Matrix& mtObj2Device,
                                            const CPDF_Color* pColor,
                                            bool stroke) {
  RetainPtr<CPDF_Pattern> pattern = pColor->GetPattern();
  if (!pattern) {
    return;
  }

  if (CPDF_TilingPattern* pTilingPattern = pattern->AsTilingPattern()) {
    DrawTilingPattern(pTilingPattern, path_obj, mtObj2Device, stroke);
  } else if (CPDF_ShadingPattern* pShadingPattern =
                 pattern->AsShadingPattern()) {
    DrawShadingPattern(pShadingPattern, path_obj, mtObj2Device, stroke);
  }
}

void CPDF_RenderStatus::ProcessPathPattern(
    CPDF_PathObject* path_obj,
    const CFX_Matrix& mtObj2Device,
    CFX_FillRenderOptions::FillType* fill_type,
    bool* stroke) {
  DCHECK(fill_type);
  DCHECK(stroke);

  if (*fill_type != CFX_FillRenderOptions::FillType::kNoFill) {
    const CPDF_Color& FillColor = *path_obj->color_state().GetFillColor();
    if (FillColor.IsPattern()) {
      DrawPathWithPattern(path_obj, mtObj2Device, &FillColor, false);
      *fill_type = CFX_FillRenderOptions::FillType::kNoFill;
    }
  }
  if (*stroke) {
    const CPDF_Color& StrokeColor = *path_obj->color_state().GetStrokeColor();
    if (StrokeColor.IsPattern()) {
      DrawPathWithPattern(path_obj, mtObj2Device, &StrokeColor, true);
      *stroke = false;
    }
  }
}

bool CPDF_RenderStatus::ProcessImage(CPDF_ImageObject* pImageObj,
                                     const CFX_Matrix& mtObj2Device) {
  CPDF_ImageRenderer render(this);
  if (render.Start(pImageObj, mtObj2Device, std_cs_)) {
    render.Continue(nullptr);
  }
  return render.GetResult();
}

void CPDF_RenderStatus::CompositeDIBitmap(
    RetainPtr<CFX_DIBitmap> bitmap,
    int left,
    int top,
    FX_ARGB mask_argb,
    float alpha,
    BlendMode blend_mode,
    const CPDF_Transparency& transparency) {
  CHECK(bitmap);

  if (blend_mode == BlendMode::kNormal) {
    if (bitmap->IsMaskFormat()) {
#if BUILDFLAG(IS_WIN)
      FX_ARGB fill_argb = options_.TranslateColor(mask_argb);
      if (alpha != 1.0f) {
        auto& bgra = reinterpret_cast<FX_BGRA_STRUCT<uint8_t>&>(fill_argb);
        bgra.alpha *= FXSYS_roundf(alpha * 255) / 255;
      }
      if (device_->SetBitMask(bitmap, left, top, fill_argb)) {
        return;
      }
#else
      NOTREACHED();
#endif
    } else {
      if (alpha != 1.0f) {
        if (CFX_DefaultRenderDevice::UseSkiaRenderer()) {
          CFX_Matrix matrix = CFX_RenderDevice::GetFlipMatrix(
              bitmap->GetWidth(), bitmap->GetHeight(), left, top);
          device_->StartDIBits(std::move(bitmap), alpha, /*argb=*/0, matrix,
                               FXDIB_ResampleOptions());
          return;
        }
        bitmap->MultiplyAlpha(alpha);
      }
      if (device_->SetDIBits(bitmap, left, top)) {
        return;
      }
    }
  }
  bool bIsolated = transparency.IsIsolated();
  bool bBackAlphaRequired =
      blend_mode != BlendMode::kNormal && bIsolated && !drop_objects_;
  bool bGetBackGround =
      ((device_->GetRenderCaps() & FXRC_ALPHA_OUTPUT)) ||
      (!(device_->GetRenderCaps() & FXRC_ALPHA_OUTPUT) &&
       (device_->GetRenderCaps() & FXRC_GET_BITS) && !bBackAlphaRequired);
  if (bGetBackGround) {
    if (bIsolated || !transparency.IsGroup()) {
      if (!bitmap->IsMaskFormat()) {
        device_->SetDIBitsWithBlend(std::move(bitmap), left, top, blend_mode);
      }
      return;
    }

    FX_RECT rect(left, top, left + bitmap->GetWidth(),
                 top + bitmap->GetHeight());
    rect.Intersect(device_->GetClipBox());
    RetainPtr<CFX_DIBitmap> pClone;
    if (device_->GetBackDrop() && device_->GetBitmap()) {
      pClone = device_->GetBackDrop()->ClipTo(rect);
      if (!pClone) {
        return;
      }

      pClone->CompositeBitmap(0, 0, pClone->GetWidth(), pClone->GetHeight(),
                              device_->GetBitmap(), rect.left, rect.top,
                              BlendMode::kNormal, nullptr, false);
      left = std::min(left, 0);
      top = std::min(top, 0);
      if (bitmap->IsMaskFormat()) {
#if BUILDFLAG(IS_WIN)
        pClone->CompositeMask(0, 0, pClone->GetWidth(), pClone->GetHeight(),
                              bitmap, mask_argb, left, top, blend_mode, nullptr,
                              false);
#else
        NOTREACHED();
#endif
      } else {
        pClone->CompositeBitmap(0, 0, pClone->GetWidth(), pClone->GetHeight(),
                                bitmap, left, top, blend_mode, nullptr, false);
      }
    } else {
      pClone = bitmap;
    }
    if (device_->GetBackDrop()) {
      device_->SetDIBits(pClone, rect.left, rect.top);
    } else {
      if (!bitmap->IsMaskFormat()) {
        device_->SetDIBitsWithBlend(std::move(bitmap), rect.left, rect.top,
                                    blend_mode);
      }
    }
    return;
  }

  FX_RECT bbox = GetClippedBBox(
      FX_RECT(left, top, left + bitmap->GetWidth(), top + bitmap->GetHeight()));
  RetainPtr<CFX_DIBitmap> backdrop = GetBackdrop(
      cur_obj_, bbox, blend_mode != BlendMode::kNormal && bIsolated);
  if (!backdrop) {
    return;
  }

  const int width = bitmap->GetWidth();
  const int height = bitmap->GetHeight();
  if (bitmap->IsMaskFormat()) {
#if BUILDFLAG(IS_WIN)
    backdrop->CompositeMask(left - bbox.left, top - bbox.top, width, height,
                            std::move(bitmap), mask_argb, 0, 0, blend_mode,
                            nullptr, false);
#else
    NOTREACHED();
#endif
  } else {
    backdrop->CompositeBitmap(left - bbox.left, top - bbox.top, width, height,
                              std::move(bitmap), 0, 0, blend_mode, nullptr,
                              false);
  }

  auto new_backdrop = pdfium::MakeRetain<CFX_DIBitmap>();
  CHECK(new_backdrop->Create(backdrop->GetWidth(), backdrop->GetHeight(),
                             FXDIB_Format::kBgrx));
  new_backdrop->Clear(0xffffffff);
  new_backdrop->CompositeBitmap(0, 0, new_backdrop->GetWidth(),
                                new_backdrop->GetHeight(), std::move(backdrop),
                                0, 0, BlendMode::kNormal, nullptr, false);
  device_->SetDIBits(std::move(new_backdrop), bbox.left, bbox.top);
}

RetainPtr<CFX_DIBitmap> CPDF_RenderStatus::LoadSMask(
    CPDF_Dictionary* smask_dict,
    const FX_RECT& clip_rect,
    const CFX_Matrix& smask_matrix) {
  RetainPtr<CPDF_Stream> pGroup =
      smask_dict->GetMutableStreamFor(pdfium::transparency::kG);
  if (!pGroup) {
    return nullptr;
  }

  std::unique_ptr<CPDF_Function> pFunc;
  RetainPtr<const CPDF_Object> pFuncObj =
      smask_dict->GetDirectObjectFor(pdfium::transparency::kTR);
  if (pFuncObj && (pFuncObj->IsDictionary() || pFuncObj->IsStream())) {
    pFunc = CPDF_Function::Load(std::move(pFuncObj));
  }

  CFX_Matrix matrix = smask_matrix;
  matrix.Translate(-clip_rect.left, -clip_rect.top);

  CPDF_Form form(context_->GetDocument(), context_->GetMutablePageResources(),
                 pGroup);
  form.ParseContent();

  CFX_DefaultRenderDevice bitmap_device;
  bool bLuminosity =
      smask_dict->GetByteStringFor(pdfium::transparency::kSoftMaskSubType) !=
      pdfium::transparency::kAlpha;
  const int width = clip_rect.Width();
  const int height = clip_rect.Height();
  const FXDIB_Format format = GetFormatForLuminosity(bLuminosity);
  if (!bitmap_device.Create(width, height, format)) {
    return nullptr;
  }

  CPDF_ColorSpace::Family nCSFamily = CPDF_ColorSpace::Family::kUnknown;
  const FX_ARGB background_color =
      bLuminosity
          ? GetBackgroundColor(smask_dict, pGroup->GetDict().Get(), &nCSFamily)
          : 0;
  bitmap_device.Clear(background_color);

  RetainPtr<const CPDF_Dictionary> pFormResource =
      form.GetDict()->GetDictFor("Resources");
  CPDF_RenderOptions options;
  options.SetColorMode(bLuminosity ? CPDF_RenderOptions::kNormal
                                   : CPDF_RenderOptions::kAlpha);
  CPDF_RenderStatus status(context_, &bitmap_device);
  status.SetOptions(options);
  status.SetGroupFamily(nCSFamily);
  status.SetLoadMask(bLuminosity);
  status.SetStdCS(true);
  status.SetFormResource(std::move(pFormResource));
  status.SetDropObjects(drop_objects_);
  status.Initialize(nullptr, nullptr);
  status.RenderObjectList(&form, matrix);

  auto result_mask = pdfium::MakeRetain<CFX_DIBitmap>();
  if (!result_mask->Create(width, height, FXDIB_Format::k8bppMask)) {
    return nullptr;
  }

  pdfium::span<uint8_t> dest_buf = result_mask->GetWritableBuffer();
  RetainPtr<const CFX_DIBitmap> bitmap = bitmap_device.GetBitmap();
  pdfium::span<const uint8_t> src_buf = bitmap->GetBuffer();
  const int dest_pitch = result_mask->GetPitch();
  const int src_pitch = bitmap->GetPitch();
  DataVector<uint8_t> transfers(256);
  if (pFunc) {
    std::vector<float> results(pFunc->OutputCount());
    for (size_t i = 0; i < transfers.size(); ++i) {
      float input = i / 255.0f;
      pFunc->Call(pdfium::span_from_ref(input), results);
      transfers[i] = FXSYS_roundf(results[0] * 255);
    }
  } else {
    // Fill |transfers| with 0, 1, ... N.
    std::iota(transfers.begin(), transfers.end(), 0);
  }
  if (bLuminosity) {
    const int bytes_per_pixel = bitmap->GetBPP() / 8;
    for (int row = 0; row < height; row++) {
      const size_t dest_offset = Fx2DSizeOrDie(row, dest_pitch);
      const size_t src_offset = Fx2DSizeOrDie(row, src_pitch);
      uint8_t* dest_pos = dest_buf.subspan(dest_offset).data();
      const uint8_t* src_pos = src_buf.subspan(src_offset).data();
      for (int col = 0; col < width; col++) {
        UNSAFE_TODO({
          *dest_pos++ = transfers[FXRGB2GRAY(src_pos[2], src_pos[1], *src_pos)];
          src_pos += bytes_per_pixel;
        });
      }
    }
  } else if (pFunc) {
    int size = dest_pitch * height;
    for (int i = 0; i < size; i++) {
      dest_buf[i] = transfers[src_buf[i]];
    }
  } else {
    fxcrt::Copy(src_buf.first(static_cast<size_t>(dest_pitch * height)),
                dest_buf);
  }
  return result_mask;
}

FX_ARGB CPDF_RenderStatus::GetBackgroundColor(
    const CPDF_Dictionary* pSMaskDict,
    const CPDF_Dictionary* group_dict,
    CPDF_ColorSpace::Family* pCSFamily) {
  static constexpr FX_ARGB kDefaultColor = ArgbEncode(255, 0, 0, 0);
  RetainPtr<const CPDF_Array> pBC =
      pSMaskDict->GetArrayFor(pdfium::transparency::kBC);
  if (!pBC) {
    return kDefaultColor;
  }

  RetainPtr<const CPDF_Object> pCSObj;
  RetainPtr<const CPDF_Dictionary> pGroup =
      group_dict ? group_dict->GetDictFor("Group") : nullptr;
  if (pGroup) {
    pCSObj = pGroup->GetDirectObjectFor(pdfium::transparency::kCS);
  }
  RetainPtr<CPDF_ColorSpace> pCS =
      CPDF_DocPageData::FromDocument(context_->GetDocument())
          ->GetColorSpace(pCSObj.Get(), nullptr);
  if (!pCS) {
    return kDefaultColor;
  }

  CPDF_ColorSpace::Family family = pCS->GetFamily();
  if (family == CPDF_ColorSpace::Family::kLab || pCS->IsSpecial() ||
      (family == CPDF_ColorSpace::Family::kICCBased && !pCS->IsNormal())) {
    return kDefaultColor;
  }

  // Store Color Space Family to use in CPDF_RenderStatus::Initialize().
  *pCSFamily = family;

  uint32_t comps = std::max(8u, pCS->ComponentCount());
  size_t count = std::min<size_t>(8, pBC->size());
  std::vector<float> floats = ReadArrayElementsToVector(pBC.Get(), count);
  floats.resize(comps);

  auto rgb = pCS->GetRGBOrZerosOnError(floats);
  return ArgbEncode(255, static_cast<int>(rgb.red * 255),
                    static_cast<int>(rgb.green * 255),
                    static_cast<int>(rgb.blue * 255));
}

FXDIB_Format CPDF_RenderStatus::GetCompatibleArgbFormat() const {
#if defined(PDF_USE_SKIA)
  if (device_->GetDeviceCaps(FXDC_RENDER_CAPS) & FXRC_PREMULTIPLIED_ALPHA) {
    return FXDIB_Format::kBgraPremul;
  }
#endif
  return FXDIB_Format::kBgra;
}

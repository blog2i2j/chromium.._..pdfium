// Copyright 2014 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "fxjs/cjs_global.h"

#include <memory>
#include <utility>
#include <vector>

#include "core/fxcrt/check.h"
#include "core/fxcrt/containers/contains.h"
#include "core/fxcrt/fx_extension.h"
#include "core/fxcrt/span.h"
#include "fxjs/cfx_globaldata.h"
#include "fxjs/cfx_keyvalue.h"
#include "fxjs/cjs_event_context.h"
#include "fxjs/cjs_object.h"
#include "fxjs/fxv8.h"
#include "fxjs/js_define.h"
#include "fxjs/js_resources.h"
#include "v8/include/v8-isolate.h"

namespace {

ByteString ByteStringFromV8Name(v8::Isolate* pIsolate,
                                v8::Local<v8::Name> name) {
  CHECK(name->IsString());
  return fxv8::ToByteString(pIsolate, name.As<v8::String>());
}

}  // namespace

CJS_Global::JSGlobalData::JSGlobalData() = default;

CJS_Global::JSGlobalData::~JSGlobalData() = default;

const JSMethodSpec CJS_Global::MethodSpecs[] = {
    {"setPersistent", setPersistent_static}};

uint32_t CJS_Global::ObjDefnID = 0;

// static
void CJS_Global::setPersistent_static(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  JSMethod<CJS_Global, &CJS_Global::setPersistent>("setPersistent", "global",
                                                   info);
}

// static
v8::Intercepted CJS_Global::queryprop_static(
    v8::Local<v8::Name> property,
    const v8::PropertyCallbackInfo<v8::Integer>& info) {
  auto pObj = JSGetObject<CJS_Global>(info.GetIsolate(), info.HolderV2());
  if (!pObj) {
    return v8::Intercepted::kNo;
  }

  ByteString bsProp = ByteStringFromV8Name(info.GetIsolate(), property);
  if (!pObj->HasProperty(bsProp)) {
    return v8::Intercepted::kNo;
  }

  info.GetReturnValue().Set(static_cast<int>(v8::PropertyAttribute::None));
  return v8::Intercepted::kYes;
}

// static
v8::Intercepted CJS_Global::getprop_static(
    v8::Local<v8::Name> property,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
  auto pObj = JSGetObject<CJS_Global>(info.GetIsolate(), info.HolderV2());
  if (!pObj) {
    return v8::Intercepted::kNo;
  }

  CJS_Runtime* pRuntime = pObj->GetRuntime();
  if (!pRuntime) {
    return v8::Intercepted::kNo;
  }

  ByteString bsProp = ByteStringFromV8Name(info.GetIsolate(), property);
  CJS_Result result = pObj->GetProperty(pRuntime, bsProp);
  if (result.HasError()) {
    pRuntime->Error(
        JSFormatErrorString("global", "GetProperty", result.Error()));
    return v8::Intercepted::kYes;
  }
  if (!result.HasReturn()) {
    return v8::Intercepted::kNo;
  }

  info.GetReturnValue().Set(result.Return());
  return v8::Intercepted::kYes;
}

// static
v8::Intercepted CJS_Global::putprop_static(
    v8::Local<v8::Name> property,
    v8::Local<v8::Value> value,
    const v8::PropertyCallbackInfo<void>& info) {
  auto pObj = JSGetObject<CJS_Global>(info.GetIsolate(), info.HolderV2());
  if (!pObj) {
    return v8::Intercepted::kNo;
  }

  CJS_Runtime* pRuntime = pObj->GetRuntime();
  if (!pRuntime) {
    return v8::Intercepted::kNo;
  }

  ByteString bsProp = ByteStringFromV8Name(info.GetIsolate(), property);
  CJS_Result result = pObj->SetProperty(pRuntime, bsProp, value);
  if (result.HasError()) {
    pRuntime->Error(
        JSFormatErrorString("global", "PutProperty", result.Error()));
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kYes;
}

// static
v8::Intercepted CJS_Global::delprop_static(
    v8::Local<v8::Name> property,
    const v8::PropertyCallbackInfo<v8::Boolean>& info) {
  auto pObj = JSGetObject<CJS_Global>(info.GetIsolate(), info.HolderV2());
  if (!pObj) {
    return v8::Intercepted::kNo;
  }

  ByteString bsProp = ByteStringFromV8Name(info.GetIsolate(), property);
  if (!pObj->DelProperty(bsProp)) {
    return v8::Intercepted::kNo;
  }

  info.GetReturnValue().Set(true);
  return v8::Intercepted::kYes;
}

void CJS_Global::enumprop_static(
    const v8::PropertyCallbackInfo<v8::Array>& info) {
  auto pObj = JSGetObject<CJS_Global>(info.GetIsolate(), info.HolderV2());
  if (!pObj) {
    return;
  }

  CJS_Runtime* pRuntime = pObj->GetRuntime();
  if (!pRuntime) {
    return;
  }

  pObj->EnumProperties(pRuntime, info);
}

// static
void CJS_Global::DefineAllProperties(CFXJS_Engine* pEngine) {
  pEngine->DefineObjAllProperties(
      ObjDefnID, CJS_Global::queryprop_static, CJS_Global::getprop_static,
      CJS_Global::putprop_static, CJS_Global::delprop_static,
      CJS_Global::enumprop_static);
}

// static
uint32_t CJS_Global::GetObjDefnID() {
  return ObjDefnID;
}

// static
void CJS_Global::DefineJSObjects(CFXJS_Engine* pEngine) {
  ObjDefnID = pEngine->DefineObj("global", FXJSOBJTYPE_STATIC,
                                 JSConstructor<CJS_Global>, JSDestructor);
  DefineMethods(pEngine, ObjDefnID, MethodSpecs);
  DefineAllProperties(pEngine);
}

CJS_Global::CJS_Global(v8::Local<v8::Object> pObject, CJS_Runtime* pRuntime)
    : CJS_Object(pObject, pRuntime),
      global_data_(CFX_GlobalData::GetRetainedInstance(nullptr)) {
  UpdateGlobalPersistentVariables();
}

CJS_Global::~CJS_Global() {
  DestroyGlobalPersisitentVariables();
  global_data_.ExtractAsDangling()->Release();
}

bool CJS_Global::HasProperty(const ByteString& propname) {
  return pdfium::Contains(map_global_, propname);
}

bool CJS_Global::DelProperty(const ByteString& propname) {
  auto it = map_global_.find(propname);
  if (it == map_global_.end()) {
    return false;
  }

  it->second->bDeleted = true;
  return true;
}

CJS_Result CJS_Global::GetProperty(CJS_Runtime* pRuntime,
                                   const ByteString& propname) {
  auto it = map_global_.find(propname);
  if (it == map_global_.end()) {
    return CJS_Result::Success();
  }

  JSGlobalData* pData = it->second.get();
  if (pData->bDeleted) {
    return CJS_Result::Success();
  }

  switch (pData->nType) {
    case CFX_Value::DataType::kNumber:
      return CJS_Result::Success(pRuntime->NewNumber(pData->dData));
    case CFX_Value::DataType::kBoolean:
      return CJS_Result::Success(pRuntime->NewBoolean(pData->bData));
    case CFX_Value::DataType::kString:
      return CJS_Result::Success(
          pRuntime->NewString(pData->sData.AsStringView()));
    case CFX_Value::DataType::kObject:
      return CJS_Result::Success(
          v8::Local<v8::Object>::New(pRuntime->GetIsolate(), pData->pData));
    case CFX_Value::DataType::kNull:
      return CJS_Result::Success(pRuntime->NewNull());
  }
}

CJS_Result CJS_Global::SetProperty(CJS_Runtime* pRuntime,
                                   const ByteString& propname,
                                   v8::Local<v8::Value> vp) {
  if (vp->IsNumber()) {
    return SetGlobalVariables(propname, CFX_Value::DataType::kNumber,
                              pRuntime->ToDouble(vp), false, ByteString(),
                              v8::Local<v8::Object>(), false);
  }
  if (vp->IsBoolean()) {
    return SetGlobalVariables(propname, CFX_Value::DataType::kBoolean, 0,
                              pRuntime->ToBoolean(vp), ByteString(),
                              v8::Local<v8::Object>(), false);
  }
  if (vp->IsString()) {
    return SetGlobalVariables(propname, CFX_Value::DataType::kString, 0, false,
                              pRuntime->ToByteString(vp),
                              v8::Local<v8::Object>(), false);
  }
  if (vp->IsObject()) {
    return SetGlobalVariables(propname, CFX_Value::DataType::kObject, 0, false,
                              ByteString(), pRuntime->ToObject(vp), false);
  }
  if (vp->IsNull()) {
    return SetGlobalVariables(propname, CFX_Value::DataType::kNull, 0, false,
                              ByteString(), v8::Local<v8::Object>(), false);
  }
  if (vp->IsUndefined()) {
    DelProperty(propname);
    return CJS_Result::Success();
  }
  return CJS_Result::Failure(JSMessage::kObjectTypeError);
}

void CJS_Global::EnumProperties(
    CJS_Runtime* pRuntime,
    const v8::PropertyCallbackInfo<v8::Array>& info) {
  v8::Local<v8::Array> result = pRuntime->NewArray();
  int idx = 0;
  for (const auto& it : map_global_) {
    if (it.second->bDeleted) {
      continue;
    }
    v8::Local<v8::Name> name = pRuntime->NewString(it.first.AsStringView());
    pRuntime->PutArrayElement(result, idx, name);
    ++idx;
  }
  info.GetReturnValue().Set(result);
}

CJS_Result CJS_Global::setPersistent(
    CJS_Runtime* pRuntime,
    pdfium::span<v8::Local<v8::Value>> params) {
  if (params.size() != 2) {
    return CJS_Result::Failure(JSMessage::kParamError);
  }

  auto it = map_global_.find(pRuntime->ToByteString(params[0]));
  if (it == map_global_.end() || it->second->bDeleted) {
    return CJS_Result::Failure(JSMessage::kGlobalNotFoundError);
  }

  it->second->bPersistent = pRuntime->ToBoolean(params[1]);
  return CJS_Result::Success();
}

void CJS_Global::UpdateGlobalPersistentVariables() {
  CJS_Runtime* pRuntime = GetRuntime();
  if (!pRuntime) {
    return;
  }

  for (int i = 0, sz = global_data_->GetSize(); i < sz; i++) {
    CFX_GlobalData::Element* pData = global_data_->GetAt(i);
    switch (pData->data.nType) {
      case CFX_Value::DataType::kNumber:
        SetGlobalVariables(pData->data.sKey, CFX_Value::DataType::kNumber,
                           pData->data.dData, false, ByteString(),
                           v8::Local<v8::Object>(), pData->bPersistent);
        pRuntime->PutObjectProperty(ToV8Object(),
                                    pData->data.sKey.AsStringView(),
                                    pRuntime->NewNumber(pData->data.dData));
        break;
      case CFX_Value::DataType::kBoolean:
        SetGlobalVariables(pData->data.sKey, CFX_Value::DataType::kBoolean, 0,
                           pData->data.bData == 1, ByteString(),
                           v8::Local<v8::Object>(), pData->bPersistent);
        pRuntime->PutObjectProperty(
            ToV8Object(), pData->data.sKey.AsStringView(),
            pRuntime->NewBoolean(pData->data.bData == 1));
        break;
      case CFX_Value::DataType::kString:
        SetGlobalVariables(pData->data.sKey, CFX_Value::DataType::kString, 0,
                           false, pData->data.sData, v8::Local<v8::Object>(),
                           pData->bPersistent);
        pRuntime->PutObjectProperty(
            ToV8Object(), pData->data.sKey.AsStringView(),
            pRuntime->NewString(pData->data.sData.AsStringView()));
        break;
      case CFX_Value::DataType::kObject: {
        v8::Local<v8::Object> pObj = pRuntime->NewObject();
        if (!pObj.IsEmpty()) {
          PutObjectProperty(pObj, &pData->data);
          SetGlobalVariables(pData->data.sKey, CFX_Value::DataType::kObject, 0,
                             false, ByteString(), pObj, pData->bPersistent);
          pRuntime->PutObjectProperty(ToV8Object(),
                                      pData->data.sKey.AsStringView(), pObj);
        }
      } break;
      case CFX_Value::DataType::kNull:
        SetGlobalVariables(pData->data.sKey, CFX_Value::DataType::kNull, 0,
                           false, ByteString(), v8::Local<v8::Object>(),
                           pData->bPersistent);
        pRuntime->PutObjectProperty(
            ToV8Object(), pData->data.sKey.AsStringView(), pRuntime->NewNull());
        break;
    }
  }
}

void CJS_Global::CommitGlobalPersisitentVariables() {
  CJS_Runtime* pRuntime = GetRuntime();
  if (!pRuntime) {
    return;
  }

  for (const auto& iter : map_global_) {
    ByteString name = iter.first;
    JSGlobalData* pData = iter.second.get();
    if (pData->bDeleted) {
      global_data_->DeleteGlobalVariable(name);
      continue;
    }
    switch (pData->nType) {
      case CFX_Value::DataType::kNumber:
        global_data_->SetGlobalVariableNumber(name, pData->dData);
        global_data_->SetGlobalVariablePersistent(name, pData->bPersistent);
        break;
      case CFX_Value::DataType::kBoolean:
        global_data_->SetGlobalVariableBoolean(name, pData->bData);
        global_data_->SetGlobalVariablePersistent(name, pData->bPersistent);
        break;
      case CFX_Value::DataType::kString:
        global_data_->SetGlobalVariableString(name, pData->sData);
        global_data_->SetGlobalVariablePersistent(name, pData->bPersistent);
        break;
      case CFX_Value::DataType::kObject: {
        v8::Local<v8::Object> obj =
            v8::Local<v8::Object>::New(pRuntime->GetIsolate(), pData->pData);
        global_data_->SetGlobalVariableObject(name,
                                              ObjectToArray(pRuntime, obj));
        global_data_->SetGlobalVariablePersistent(name, pData->bPersistent);
      } break;
      case CFX_Value::DataType::kNull:
        global_data_->SetGlobalVariableNull(name);
        global_data_->SetGlobalVariablePersistent(name, pData->bPersistent);
        break;
    }
  }
}

std::vector<std::unique_ptr<CFX_KeyValue>> CJS_Global::ObjectToArray(
    CJS_Runtime* pRuntime,
    v8::Local<v8::Object> pObj) {
  std::vector<std::unique_ptr<CFX_KeyValue>> array;
  std::vector<WideString> pKeyList = pRuntime->GetObjectPropertyNames(pObj);
  for (const auto& ws : pKeyList) {
    ByteString sKey = ws.ToUTF8();
    v8::Local<v8::Value> v =
        pRuntime->GetObjectProperty(pObj, sKey.AsStringView());
    if (v->IsNumber()) {
      auto pObjElement = std::make_unique<CFX_KeyValue>();
      pObjElement->nType = CFX_Value::DataType::kNumber;
      pObjElement->sKey = sKey;
      pObjElement->dData = pRuntime->ToDouble(v);
      array.push_back(std::move(pObjElement));
      continue;
    }
    if (v->IsBoolean()) {
      auto pObjElement = std::make_unique<CFX_KeyValue>();
      pObjElement->nType = CFX_Value::DataType::kBoolean;
      pObjElement->sKey = sKey;
      pObjElement->dData = pRuntime->ToBoolean(v);
      array.push_back(std::move(pObjElement));
      continue;
    }
    if (v->IsString()) {
      ByteString sValue = pRuntime->ToByteString(v);
      auto pObjElement = std::make_unique<CFX_KeyValue>();
      pObjElement->nType = CFX_Value::DataType::kString;
      pObjElement->sKey = sKey;
      pObjElement->sData = sValue;
      array.push_back(std::move(pObjElement));
      continue;
    }
    if (v->IsObject()) {
      auto pObjElement = std::make_unique<CFX_KeyValue>();
      pObjElement->nType = CFX_Value::DataType::kObject;
      pObjElement->sKey = sKey;
      pObjElement->objData = ObjectToArray(pRuntime, pRuntime->ToObject(v));
      array.push_back(std::move(pObjElement));
      continue;
    }
    if (v->IsNull()) {
      auto pObjElement = std::make_unique<CFX_KeyValue>();
      pObjElement->nType = CFX_Value::DataType::kNull;
      pObjElement->sKey = sKey;
      array.push_back(std::move(pObjElement));
    }
  }
  return array;
}

void CJS_Global::PutObjectProperty(v8::Local<v8::Object> pObj,
                                   CFX_KeyValue* pData) {
  CJS_Runtime* pRuntime = GetRuntime();
  if (pRuntime) {
    return;
  }

  for (size_t i = 0; i < pData->objData.size(); ++i) {
    CFX_KeyValue* pObjData = pData->objData.at(i).get();
    switch (pObjData->nType) {
      case CFX_Value::DataType::kNumber:
        pRuntime->PutObjectProperty(pObj, pObjData->sKey.AsStringView(),
                                    pRuntime->NewNumber(pObjData->dData));
        break;
      case CFX_Value::DataType::kBoolean:
        pRuntime->PutObjectProperty(pObj, pObjData->sKey.AsStringView(),
                                    pRuntime->NewBoolean(pObjData->bData == 1));
        break;
      case CFX_Value::DataType::kString:
        pRuntime->PutObjectProperty(
            pObj, pObjData->sKey.AsStringView(),
            pRuntime->NewString(pObjData->sData.AsStringView()));
        break;
      case CFX_Value::DataType::kObject: {
        v8::Local<v8::Object> pNewObj = pRuntime->NewObject();
        if (!pNewObj.IsEmpty()) {
          PutObjectProperty(pNewObj, pObjData);
          pRuntime->PutObjectProperty(pObj, pObjData->sKey.AsStringView(),
                                      pNewObj);
        }
      } break;
      case CFX_Value::DataType::kNull:
        pRuntime->PutObjectProperty(pObj, pObjData->sKey.AsStringView(),
                                    pRuntime->NewNull());
        break;
    }
  }
}

void CJS_Global::DestroyGlobalPersisitentVariables() {
  map_global_.clear();
}

CJS_Result CJS_Global::SetGlobalVariables(const ByteString& propname,
                                          CFX_Value::DataType nType,
                                          double dData,
                                          bool bData,
                                          const ByteString& sData,
                                          v8::Local<v8::Object> pData,
                                          bool bDefaultPersistent) {
  if (propname.IsEmpty()) {
    return CJS_Result::Failure(JSMessage::kUnknownProperty);
  }

  auto it = map_global_.find(propname);
  if (it != map_global_.end()) {
    JSGlobalData* pTemp = it->second.get();
    if (pTemp->bDeleted || pTemp->nType != nType) {
      pTemp->dData = 0;
      pTemp->bData = false;
      pTemp->sData.clear();
      pTemp->nType = nType;
    }
    pTemp->bDeleted = false;
    switch (nType) {
      case CFX_Value::DataType::kNumber:
        pTemp->dData = dData;
        break;
      case CFX_Value::DataType::kBoolean:
        pTemp->bData = bData;
        break;
      case CFX_Value::DataType::kString:
        pTemp->sData = sData;
        break;
      case CFX_Value::DataType::kObject:
        pTemp->pData.Reset(GetRuntime()->GetIsolate(), pData);
        break;
      case CFX_Value::DataType::kNull:
        break;
    }
    return CJS_Result::Success();
  }

  auto pNewData = std::make_unique<JSGlobalData>();
  switch (nType) {
    case CFX_Value::DataType::kNumber:
      pNewData->nType = CFX_Value::DataType::kNumber;
      pNewData->dData = dData;
      pNewData->bPersistent = bDefaultPersistent;
      break;
    case CFX_Value::DataType::kBoolean:
      pNewData->nType = CFX_Value::DataType::kBoolean;
      pNewData->bData = bData;
      pNewData->bPersistent = bDefaultPersistent;
      break;
    case CFX_Value::DataType::kString:
      pNewData->nType = CFX_Value::DataType::kString;
      pNewData->sData = sData;
      pNewData->bPersistent = bDefaultPersistent;
      break;
    case CFX_Value::DataType::kObject:
      pNewData->nType = CFX_Value::DataType::kObject;
      pNewData->pData.Reset(GetRuntime()->GetIsolate(), pData);
      pNewData->bPersistent = bDefaultPersistent;
      break;
    case CFX_Value::DataType::kNull:
      pNewData->nType = CFX_Value::DataType::kNull;
      pNewData->bPersistent = bDefaultPersistent;
      break;
  }
  map_global_[propname] = std::move(pNewData);
  return CJS_Result::Success();
}

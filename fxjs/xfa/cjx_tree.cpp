// Copyright 2017 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "fxjs/xfa/cjx_tree.h"

#include "core/fxcrt/numerics/safe_conversions.h"
#include "core/fxcrt/span.h"
#include "fxjs/fxv8.h"
#include "fxjs/js_resources.h"
#include "fxjs/xfa/cfxjse_class.h"
#include "fxjs/xfa/cfxjse_engine.h"
#include "v8/include/cppgc/allocation.h"
#include "v8/include/v8-object.h"
#include "v8/include/v8-primitive.h"
#include "xfa/fxfa/parser/cxfa_arraynodelist.h"
#include "xfa/fxfa/parser/cxfa_attachnodelist.h"
#include "xfa/fxfa/parser/cxfa_document.h"
#include "xfa/fxfa/parser/cxfa_node.h"
#include "xfa/fxfa/parser/cxfa_object.h"

const CJX_MethodSpec CJX_Tree::MethodSpecs[] = {
    {"resolveNode", resolveNode_static},
    {"resolveNodes", resolveNodes_static}};

CJX_Tree::CJX_Tree(CXFA_Object* obj) : CJX_Object(obj) {
  DefineMethods(MethodSpecs);
}

CJX_Tree::~CJX_Tree() = default;

bool CJX_Tree::DynamicTypeIs(TypeTag eType) const {
  return eType == static_type__ || ParentType__::DynamicTypeIs(eType);
}

CJS_Result CJX_Tree::resolveNode(CFXJSE_Engine* runtime,
                                 pdfium::span<v8::Local<v8::Value>> params) {
  if (params.size() != 1) {
    return CJS_Result::Failure(JSMessage::kParamError);
  }

  WideString wsExpression = runtime->ToWideString(params[0]);
  CXFA_Object* pRefNode = GetXFAObject();
  if (pRefNode->GetElementType() == XFA_Element::Xfa) {
    pRefNode = runtime->GetThisObject();
  }

  std::optional<CFXJSE_Engine::ResolveResult> maybeResult =
      runtime->ResolveObjects(
          ToNode(pRefNode), wsExpression.AsStringView(),
          Mask<XFA_ResolveFlag>{
              XFA_ResolveFlag::kChildren, XFA_ResolveFlag::kAttributes,
              XFA_ResolveFlag::kProperties, XFA_ResolveFlag::kParent,
              XFA_ResolveFlag::kSiblings});
  if (!maybeResult.has_value()) {
    return CJS_Result::Success(runtime->NewNull());
  }

  if (maybeResult.value().type == CFXJSE_Engine::ResolveResult::Type::kNodes) {
    return CJS_Result::Success(runtime->GetOrCreateJSBindingFromMap(
        maybeResult.value().objects.front().Get()));
  }

  if (!maybeResult.value().script_attribute.callback ||
      maybeResult.value().script_attribute.eValueType !=
          XFA_ScriptType::Object) {
    return CJS_Result::Success(runtime->NewNull());
  }

  v8::Local<v8::Value> pValue;
  CJX_Object* jsObject = maybeResult.value().objects.front()->JSObject();
  (*maybeResult.value().script_attribute.callback)(
      runtime->GetIsolate(), jsObject, &pValue, false,
      maybeResult.value().script_attribute.attribute);
  return CJS_Result::Success(pValue);
}

CJS_Result CJX_Tree::resolveNodes(CFXJSE_Engine* runtime,
                                  pdfium::span<v8::Local<v8::Value>> params) {
  if (params.size() != 1) {
    return CJS_Result::Failure(JSMessage::kParamError);
  }

  CXFA_Object* refNode = GetXFAObject();
  if (refNode->GetElementType() == XFA_Element::Xfa) {
    refNode = runtime->GetThisObject();
  }

  const Mask<XFA_ResolveFlag> kFlags = {
      XFA_ResolveFlag::kChildren, XFA_ResolveFlag::kAttributes,
      XFA_ResolveFlag::kProperties, XFA_ResolveFlag::kParent,
      XFA_ResolveFlag::kSiblings};
  return CJS_Result::Success(ResolveNodeList(runtime->GetIsolate(),
                                             runtime->ToWideString(params[0]),
                                             kFlags, ToNode(refNode)));
}

void CJX_Tree::all(v8::Isolate* pIsolate,
                   v8::Local<v8::Value>* pValue,
                   bool bSetting,
                   XFA_Attribute eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException(pIsolate);
    return;
  }
  const Mask<XFA_ResolveFlag> kFlags = {XFA_ResolveFlag::kSiblings,
                                        XFA_ResolveFlag::kALL};
  WideString wsExpression = GetAttributeByEnum(XFA_Attribute::Name) + L"[*]";
  *pValue = ResolveNodeList(pIsolate, wsExpression, kFlags, nullptr);
}

void CJX_Tree::classAll(v8::Isolate* pIsolate,
                        v8::Local<v8::Value>* pValue,
                        bool bSetting,
                        XFA_Attribute eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException(pIsolate);
    return;
  }
  const Mask<XFA_ResolveFlag> kFlags = {XFA_ResolveFlag::kSiblings,
                                        XFA_ResolveFlag::kALL};
  WideString wsExpression =
      L"#" + WideString::FromASCII(GetXFAObject()->GetClassName()) + L"[*]";
  *pValue = ResolveNodeList(pIsolate, wsExpression, kFlags, nullptr);
}

void CJX_Tree::nodes(v8::Isolate* pIsolate,
                     v8::Local<v8::Value>* pValue,
                     bool bSetting,
                     XFA_Attribute eAttribute) {
  if (bSetting) {
    FXJSE_ThrowMessage(pIsolate, "Unable to set ");
    return;
  }

  CXFA_Document* doc = GetDocument();
  auto* pNodeList = cppgc::MakeGarbageCollected<CXFA_AttachNodeList>(
      doc->GetHeap()->GetAllocationHandle(), doc, GetXFANode());
  doc->GetNodeOwner()->PersistList(pNodeList);

  CFXJSE_Engine* pEngine = doc->GetScriptContext();
  *pValue = pNodeList->JSObject()->NewBoundV8Object(
      pIsolate, pEngine->GetJseNormalClass()->GetTemplate(pIsolate));
}

void CJX_Tree::parent(v8::Isolate* pIsolate,
                      v8::Local<v8::Value>* pValue,
                      bool bSetting,
                      XFA_Attribute eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException(pIsolate);
    return;
  }

  CXFA_Node* pParent = GetXFANode()->GetParent();
  *pValue = pParent ? GetDocument()
                          ->GetScriptContext()
                          ->GetOrCreateJSBindingFromMap(pParent)
                          .As<v8::Value>()
                    : fxv8::NewNullHelper(pIsolate).As<v8::Value>();
}

void CJX_Tree::index(v8::Isolate* pIsolate,
                     v8::Local<v8::Value>* pValue,
                     bool bSetting,
                     XFA_Attribute eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException(pIsolate);
    return;
  }

  CXFA_Node* pNode = GetXFANode();
  size_t iIndex = pNode ? pNode->GetIndexByName() : 0;
  *pValue =
      fxv8::NewNumberHelper(pIsolate, pdfium::checked_cast<int32_t>(iIndex));
}

void CJX_Tree::classIndex(v8::Isolate* pIsolate,
                          v8::Local<v8::Value>* pValue,
                          bool bSetting,
                          XFA_Attribute eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException(pIsolate);
    return;
  }

  CXFA_Node* pNode = GetXFANode();
  size_t iIndex = pNode ? pNode->GetIndexByClassName() : 0;
  *pValue =
      fxv8::NewNumberHelper(pIsolate, pdfium::checked_cast<int32_t>(iIndex));
}

void CJX_Tree::somExpression(v8::Isolate* pIsolate,
                             v8::Local<v8::Value>* pValue,
                             bool bSetting,
                             XFA_Attribute eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException(pIsolate);
    return;
  }

  ByteString bsSOMExpression = GetXFAObject()->GetSOMExpression().ToUTF8();
  *pValue = fxv8::NewStringHelper(pIsolate, bsSOMExpression.AsStringView());
}

v8::Local<v8::Value> CJX_Tree::ResolveNodeList(v8::Isolate* pIsolate,
                                               WideString wsExpression,
                                               Mask<XFA_ResolveFlag> dwFlag,
                                               CXFA_Node* refNode) {
  if (!refNode) {
    refNode = GetXFANode();
  }

  CXFA_Document* doc = GetDocument();
  auto* pNodeList = cppgc::MakeGarbageCollected<CXFA_ArrayNodeList>(
      doc->GetHeap()->GetAllocationHandle(), doc);
  doc->GetNodeOwner()->PersistList(pNodeList);

  CFXJSE_Engine* pScriptContext = doc->GetScriptContext();
  std::optional<CFXJSE_Engine::ResolveResult> maybeResult =
      pScriptContext->ResolveObjects(refNode, wsExpression.AsStringView(),
                                     dwFlag);

  if (maybeResult.has_value()) {
    if (maybeResult.value().type ==
        CFXJSE_Engine::ResolveResult::Type::kNodes) {
      for (auto& pObject : maybeResult.value().objects) {
        if (pObject->IsNode()) {
          pNodeList->Append(pObject->AsNode());
        }
      }
    } else {
      if (maybeResult.value().script_attribute.callback &&
          maybeResult.value().script_attribute.eValueType ==
              XFA_ScriptType::Object) {
        for (auto& pObject : maybeResult.value().objects) {
          v8::Local<v8::Value> innerValue;
          CJX_Object* jsObject = pObject->JSObject();
          (*maybeResult.value().script_attribute.callback)(
              pIsolate, jsObject, &innerValue, false,
              maybeResult.value().script_attribute.attribute);
          CXFA_Object* obj =
              CFXJSE_Engine::ToObject(pScriptContext->GetIsolate(), innerValue);
          if (obj->IsNode()) {
            pNodeList->Append(obj->AsNode());
          }
        }
      }
    }
  }
  return pNodeList->JSObject()->NewBoundV8Object(
      pIsolate, pScriptContext->GetJseNormalClass()->GetTemplate(pIsolate));
}

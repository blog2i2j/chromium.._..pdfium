// Copyright 2016 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef CORE_FPDFAPI_FONT_CPDF_FONT_H_
#define CORE_FPDFAPI_FONT_CPDF_FONT_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "build/build_config.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_stream_acc.h"
#include "core/fxcrt/fx_coordinates.h"
#include "core/fxcrt/fx_string.h"
#include "core/fxcrt/observed_ptr.h"
#include "core/fxcrt/retain_ptr.h"
#include "core/fxcrt/unowned_ptr.h"
#include "core/fxge/cfx_font.h"

class CFX_DIBitmap;
class CPDF_CIDFont;
class CPDF_Document;
class CPDF_TrueTypeFont;
class CPDF_Type1Font;
class CPDF_Type3Char;
class CPDF_Type3Font;
class CPDF_ToUnicodeMap;
enum class FontEncoding;

class CPDF_Font : public Retainable, public Observable {
 public:
  // Callback mechanism for Type3 fonts to get pixels from forms.
  class FormIface {
   public:
    virtual ~FormIface() = default;

    virtual void ParseContentForType3Char(CPDF_Type3Char* pChar) = 0;
    virtual bool HasPageObjects() const = 0;
    virtual CFX_FloatRect CalcBoundingBox() const = 0;
    virtual std::optional<std::pair<RetainPtr<CFX_DIBitmap>, CFX_Matrix>>
    GetBitmapAndMatrixFromSoleImageOfForm() const = 0;
  };

  // Callback mechanism for Type3 fonts to get new forms from upper layers.
  class FormFactoryIface {
   public:
    virtual ~FormFactoryIface() = default;

    virtual std::unique_ptr<FormIface> CreateForm(
        CPDF_Document* document,
        RetainPtr<CPDF_Dictionary> pPageResources,
        RetainPtr<CPDF_Stream> pFormStream) = 0;
  };

  static constexpr uint32_t kInvalidCharCode = static_cast<uint32_t>(-1);

  // |pFactory| only required for Type3 fonts.
  static RetainPtr<CPDF_Font> Create(CPDF_Document* doc,
                                     RetainPtr<CPDF_Dictionary> font_dict,
                                     FormFactoryIface* pFactory);
  static RetainPtr<CPDF_Font> GetStockFont(CPDF_Document* doc,
                                           ByteStringView fontname);

  virtual bool IsType1Font() const;
  virtual bool IsTrueTypeFont() const;
  virtual bool IsType3Font() const;
  virtual bool IsCIDFont() const;
  virtual const CPDF_Type1Font* AsType1Font() const;
  virtual CPDF_Type1Font* AsType1Font();
  virtual const CPDF_TrueTypeFont* AsTrueTypeFont() const;
  virtual CPDF_TrueTypeFont* AsTrueTypeFont();
  virtual const CPDF_Type3Font* AsType3Font() const;
  virtual CPDF_Type3Font* AsType3Font();
  virtual const CPDF_CIDFont* AsCIDFont() const;
  virtual CPDF_CIDFont* AsCIDFont();

  virtual void WillBeDestroyed();
  virtual bool IsVertWriting() const;
  virtual bool IsUnicodeCompatible() const = 0;
  virtual uint32_t GetNextChar(ByteStringView pString, size_t* pOffset) const;
  virtual size_t CountChar(ByteStringView pString) const;
  virtual void AppendChar(ByteString* buf, uint32_t charcode) const;
  virtual int GlyphFromCharCode(uint32_t charcode, bool* pVertGlyph) = 0;
#if BUILDFLAG(IS_APPLE)
  virtual int GlyphFromCharCodeExt(uint32_t charcode);
#endif
  virtual WideString UnicodeFromCharCode(uint32_t charcode) const;
  virtual uint32_t CharCodeFromUnicode(wchar_t Unicode) const;
  virtual bool HasFontWidths() const;

  ByteString GetBaseFontName() const { return base_font_name_; }
  std::optional<FX_Charset> GetSubstFontCharset() const;
  bool IsEmbedded() const { return IsType3Font() || font_file_ != nullptr; }
  RetainPtr<CPDF_Dictionary> GetMutableFontDict() { return font_dict_; }
  RetainPtr<const CPDF_Dictionary> GetFontDict() const { return font_dict_; }
  uint32_t GetFontDictObjNum() const { return font_dict_->GetObjNum(); }
  bool FontDictIs(const CPDF_Dictionary* pThat) const {
    return font_dict_ == pThat;
  }
  void ClearFontDict() { font_dict_ = nullptr; }
  bool IsStandardFont() const;
  bool HasFace() const { return !!font_.GetFace(); }

  const FX_RECT& GetFontBBox() const { return font_bbox_; }
  int GetTypeAscent() const { return ascent_; }
  int GetTypeDescent() const { return descent_; }
  int GetStringWidth(ByteStringView pString);
  uint32_t FallbackFontFromCharcode(uint32_t charcode);
  int FallbackGlyphFromCharcode(int fallbackFont, uint32_t charcode);
  int GetFontFlags() const { return flags_; }
  int GetItalicAngle() const { return italic_angle_; }

  // Note that even when non-nullopt, the value may be outside the normal range
  // of [100, 900].
  std::optional<int> GetFontWeight() const;

  virtual int GetCharWidthF(uint32_t charcode) = 0;
  virtual FX_RECT GetCharBBox(uint32_t charcode) = 0;

  // Can return nullptr for stock Type1 fonts. Always returns non-null for other
  // font types.
  CPDF_Document* GetDocument() const { return document_; }

  CFX_Font* GetFont() { return &font_; }
  const CFX_Font* GetFont() const { return &font_; }

  CFX_Font* GetFontFallback(int position);

  const ByteString& GetResourceName() const { return resource_name_; }
  void SetResourceName(const ByteString& name) { resource_name_ = name; }

 protected:
  CPDF_Font(CPDF_Document* document, RetainPtr<CPDF_Dictionary> font_dict);
  ~CPDF_Font() override;

  // Tries to select any Unicode character map.
  static bool UseTTCharmapUnicode(const RetainPtr<CFX_Face>& face);

  // Commonly used wrappers for UseTTCharmap().
  static bool UseTTCharmapMSSymbol(const RetainPtr<CFX_Face>& face) {
    return UseTTCharmap(face, 3, 0);
  }
  static bool UseTTCharmapMacRoman(const RetainPtr<CFX_Face>& face) {
    return UseTTCharmap(face, 1, 0);
  }
  static bool UseTTCharmap(const RetainPtr<CFX_Face>& face,
                           int platform_id,
                           int encoding_id);

  static const char* GetAdobeCharName(FontEncoding base_encoding,
                                      const std::vector<ByteString>& charnames,
                                      uint32_t charcode);

  virtual bool Load() = 0;

  void LoadUnicodeMap() const;  // logically const only.
  void LoadFontDescriptor(const CPDF_Dictionary* font_desc);
  void CheckFontMetrics();

  UnownedPtr<CPDF_Document> const document_;
  ByteString resource_name_;  // The resource name for this font.
  CFX_Font font_;
  std::vector<std::unique_ptr<CFX_Font>> font_fallbacks_;
  RetainPtr<CPDF_StreamAcc> font_file_;
  RetainPtr<CPDF_Dictionary> font_dict_;
  ByteString base_font_name_;
  mutable std::unique_ptr<CPDF_ToUnicodeMap> to_unicode_map_;
  mutable bool to_unicode_loaded_ = false;
  bool will_be_destroyed_ = false;
  int flags_ = 0;
  int stem_v_ = 0;
  int ascent_ = 0;
  int descent_ = 0;
  int italic_angle_ = 0;
  FX_RECT font_bbox_;
};

#endif  // CORE_FPDFAPI_FONT_CPDF_FONT_H_

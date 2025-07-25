// Copyright 2015 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "build/build_config.h"
#include "core/fxcrt/notreached.h"
#include "core/fxge/fx_font.h"
#include "public/cpp/fpdf_scopers.h"
#include "public/fpdf_doc.h"
#include "public/fpdf_text.h"
#include "public/fpdf_transformpage.h"
#include "public/fpdfview.h"
#include "testing/embedder_test.h"
#include "testing/fx_string_testhelpers.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/utils/compare_coordinates.h"

using ::testing::ElementsAreArray;

namespace {

constexpr char kHelloGoodbyeText[] = "Hello, world!\r\nGoodbye, world!";
constexpr int kHelloGoodbyeTextSize = std::size(kHelloGoodbyeText);

// For use with rotated_text.pdf.
int GetRotatedTextFirstCharIndexForQuadrant(int quadrant) {
  // Unlike hello_world.pdf, rotated_text.pdf has an extra space before
  // "Goodbye".
  static constexpr size_t kSubstringsSize[] = {
      std::char_traits<char>::length("Hello, "),
      std::char_traits<char>::length("world!\r\n "),
      std::char_traits<char>::length("Goodbye, ")};
  switch (quadrant) {
    case 0:
      return 0;
    case 1:
      return kSubstringsSize[0];
    case 2:
      return kSubstringsSize[0] + kSubstringsSize[1];
    case 3:
      return kSubstringsSize[0] + kSubstringsSize[1] + kSubstringsSize[2];
    default:
      NOTREACHED();
  }
}

// For use with rotated_text_90.pdf.
int GetRotatedText90FirstCharIndexForQuadrant(int quadrant) {
  // Unlike hello_world.pdf, rotated_text_90.pdf has an extra CRLF after
  // "Hello," and an extra space before "Goodbye".
  static constexpr size_t kSubstringsSize[] = {
      std::char_traits<char>::length("Hello,\r\n "),
      std::char_traits<char>::length("world!\r\n "),
      std::char_traits<char>::length("Goodbye, ")};
  switch (quadrant) {
    case 0:
      return 0;
    case 1:
      return kSubstringsSize[0];
    case 2:
      return kSubstringsSize[0] + kSubstringsSize[1];
    case 3:
      return kSubstringsSize[0] + kSubstringsSize[1] + kSubstringsSize[2];
    default:
      NOTREACHED();
  }
}

}  // namespace

class FPDFTextEmbedderTest : public EmbedderTest {};

TEST_F(FPDFTextEmbedderTest, Text) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  unsigned short buffer[128];
  std::ranges::fill(buffer, 0xbdbd);

  // Check that edge cases are handled gracefully
  EXPECT_EQ(0, FPDFText_GetText(textpage.get(), 0, 128, nullptr));
  EXPECT_EQ(0, FPDFText_GetText(textpage.get(), -1, 128, buffer));
  EXPECT_EQ(0, FPDFText_GetText(textpage.get(), 0, -1, buffer));
  EXPECT_EQ(1, FPDFText_GetText(textpage.get(), 0, 0, buffer));
  EXPECT_EQ(0, buffer[0]);

  // Keep going and check the next case.
  std::ranges::fill(buffer, 0xbdbd);
  EXPECT_EQ(2, FPDFText_GetText(textpage.get(), 0, 1, buffer));
  EXPECT_EQ(kHelloGoodbyeText[0], buffer[0]);
  EXPECT_EQ(0, buffer[1]);

  // Check includes the terminating NUL that is provided.
  int num_chars = FPDFText_GetText(textpage.get(), 0, 128, buffer);
  ASSERT_EQ(kHelloGoodbyeTextSize, num_chars);
  EXPECT_THAT(pdfium::span(buffer).first<kHelloGoodbyeTextSize>(),
              ElementsAreArray(kHelloGoodbyeText));

  // Count does not include the terminating NUL in the string literal.
  // Neither does ByteStringView::GetLength().
  ByteStringView expected_text(kHelloGoodbyeText);
  EXPECT_EQ(static_cast<int>(expected_text.GetLength()),
            FPDFText_CountChars(textpage.get()));
  for (size_t i = 0; i < expected_text.GetLength(); ++i) {
    EXPECT_EQ(static_cast<unsigned int>(expected_text[i]),
              FPDFText_GetUnicode(textpage.get(), i))
        << " at " << i;
  }

  // Extracting using a buffer that will be completely filled. Small buffer is
  // 12 elements long, since it will need 2 locations per displayed character in
  // the expected string, plus 2 more for the terminating character.
  static const char kSmallExpected[] = "Hello";
  unsigned short small_buffer[12];
  std::ranges::fill(buffer, 0xbdbd);
  EXPECT_EQ(6, FPDFText_GetText(textpage.get(), 0, 5, small_buffer));
  EXPECT_THAT(pdfium::span(small_buffer).first(sizeof(kSmallExpected)),
              ElementsAreArray(kSmallExpected));

  EXPECT_EQ(12.0, FPDFText_GetFontSize(textpage.get(), 0));
  EXPECT_EQ(16.0, FPDFText_GetFontSize(textpage.get(), 15));

  double left = 1.0;
  double right = 2.0;
  double bottom = 3.0;
  double top = 4.0;
  EXPECT_FALSE(FPDFText_GetCharBox(nullptr, 4, &left, &right, &bottom, &top));
  EXPECT_DOUBLE_EQ(1.0, left);
  EXPECT_DOUBLE_EQ(2.0, right);
  EXPECT_DOUBLE_EQ(3.0, bottom);
  EXPECT_DOUBLE_EQ(4.0, top);
  EXPECT_FALSE(
      FPDFText_GetCharBox(textpage.get(), -1, &left, &right, &bottom, &top));
  EXPECT_DOUBLE_EQ(1.0, left);
  EXPECT_DOUBLE_EQ(2.0, right);
  EXPECT_DOUBLE_EQ(3.0, bottom);
  EXPECT_DOUBLE_EQ(4.0, top);
  EXPECT_FALSE(
      FPDFText_GetCharBox(textpage.get(), 55, &left, &right, &bottom, &top));
  EXPECT_DOUBLE_EQ(1.0, left);
  EXPECT_DOUBLE_EQ(2.0, right);
  EXPECT_DOUBLE_EQ(3.0, bottom);
  EXPECT_DOUBLE_EQ(4.0, top);
  EXPECT_FALSE(
      FPDFText_GetCharBox(textpage.get(), 4, nullptr, &right, &bottom, &top));
  EXPECT_FALSE(
      FPDFText_GetCharBox(textpage.get(), 4, &left, nullptr, &bottom, &top));
  EXPECT_FALSE(
      FPDFText_GetCharBox(textpage.get(), 4, &left, &right, nullptr, &top));
  EXPECT_FALSE(
      FPDFText_GetCharBox(textpage.get(), 4, &left, &right, &bottom, nullptr));
  EXPECT_FALSE(FPDFText_GetCharBox(textpage.get(), 4, nullptr, nullptr, nullptr,
                                   nullptr));

  EXPECT_TRUE(
      FPDFText_GetCharBox(textpage.get(), 4, &left, &right, &bottom, &top));
  EXPECT_NEAR(41.120, left, 0.001);
  EXPECT_NEAR(46.208, right, 0.001);
  EXPECT_NEAR(49.892, bottom, 0.001);
  EXPECT_NEAR(55.652, top, 0.001);

  FS_RECTF rect = {4.0f, 1.0f, 3.0f, 2.0f};
  EXPECT_FALSE(FPDFText_GetLooseCharBox(nullptr, 4, &rect));
  EXPECT_FLOAT_EQ(4.0f, rect.left);
  EXPECT_FLOAT_EQ(3.0f, rect.right);
  EXPECT_FLOAT_EQ(2.0f, rect.bottom);
  EXPECT_FLOAT_EQ(1.0f, rect.top);
  EXPECT_FALSE(FPDFText_GetLooseCharBox(textpage.get(), -1, &rect));
  EXPECT_FLOAT_EQ(4.0f, rect.left);
  EXPECT_FLOAT_EQ(3.0f, rect.right);
  EXPECT_FLOAT_EQ(2.0f, rect.bottom);
  EXPECT_FLOAT_EQ(1.0f, rect.top);
  EXPECT_FALSE(FPDFText_GetLooseCharBox(textpage.get(), 55, &rect));
  EXPECT_FLOAT_EQ(4.0f, rect.left);
  EXPECT_FLOAT_EQ(3.0f, rect.right);
  EXPECT_FLOAT_EQ(2.0f, rect.bottom);
  EXPECT_FLOAT_EQ(1.0f, rect.top);
  EXPECT_FALSE(FPDFText_GetLooseCharBox(textpage.get(), 4, nullptr));

  EXPECT_TRUE(FPDFText_GetLooseCharBox(textpage.get(), 4, &rect));
  EXPECT_FLOAT_EQ(40.664001f, rect.left);
  EXPECT_FLOAT_EQ(46.664001f, rect.right);
  EXPECT_FLOAT_EQ(46.375999f, rect.bottom);
  EXPECT_FLOAT_EQ(61.771999f, rect.top);

  double x = 0.0;
  double y = 0.0;
  EXPECT_TRUE(FPDFText_GetCharOrigin(textpage.get(), 4, &x, &y));
  EXPECT_NEAR(40.664, x, 0.001);
  EXPECT_NEAR(50.000, y, 0.001);

  EXPECT_EQ(4,
            FPDFText_GetCharIndexAtPos(textpage.get(), 42.0, 50.0, 1.0, 1.0));
  EXPECT_EQ(-1, FPDFText_GetCharIndexAtPos(textpage.get(), 0.0, 0.0, 1.0, 1.0));
  EXPECT_EQ(-1,
            FPDFText_GetCharIndexAtPos(textpage.get(), 199.0, 199.0, 1.0, 1.0));

  // Test out of range indices.
  EXPECT_EQ(-1, FPDFText_GetCharIndexAtPos(textpage.get(), 42.0, 10000000.0,
                                           1.0, 1.0));
  EXPECT_EQ(-1,
            FPDFText_GetCharIndexAtPos(textpage.get(), -1.0, 50.0, 1.0, 1.0));

  // Count does not include the terminating NUL in the string literal.
  EXPECT_EQ(2,
            FPDFText_CountRects(textpage.get(), 0, kHelloGoodbyeTextSize - 1));

  left = 0.0;
  right = 0.0;
  bottom = 0.0;
  top = 0.0;
  EXPECT_TRUE(
      FPDFText_GetRect(textpage.get(), 1, &left, &top, &right, &bottom));
  EXPECT_NEAR(20.800, left, 0.001);
  EXPECT_NEAR(135.040, right, 0.001);
  EXPECT_NEAR(96.688, bottom, 0.001);
  EXPECT_NEAR(111.600, top, 0.001);

  // Test out of range indicies set outputs to (0.0, 0.0, 0.0, 0.0).
  left = -1.0;
  right = -1.0;
  bottom = -1.0;
  top = -1.0;
  EXPECT_FALSE(
      FPDFText_GetRect(textpage.get(), -1, &left, &top, &right, &bottom));
  EXPECT_EQ(0.0, left);
  EXPECT_EQ(0.0, right);
  EXPECT_EQ(0.0, bottom);
  EXPECT_EQ(0.0, top);

  left = -2.0;
  right = -2.0;
  bottom = -2.0;
  top = -2.0;
  EXPECT_FALSE(
      FPDFText_GetRect(textpage.get(), 2, &left, &top, &right, &bottom));
  EXPECT_EQ(0.0, left);
  EXPECT_EQ(0.0, right);
  EXPECT_EQ(0.0, bottom);
  EXPECT_EQ(0.0, top);

  EXPECT_EQ(9, FPDFText_GetBoundedText(textpage.get(), 41.0, 56.0, 82.0, 48.0,
                                       nullptr, 0));

  // Extract starting at character 4 as above.
  std::ranges::fill(buffer, 0xbdbd);
  EXPECT_EQ(1, FPDFText_GetBoundedText(textpage.get(), 41.0, 56.0, 82.0, 48.0,
                                       buffer, 1));
  EXPECT_EQ('o', buffer[0]);  // 5th character in "hello".
  EXPECT_EQ(0xbdbd, buffer[1]);

  std::ranges::fill(buffer, 0xbdbd);
  EXPECT_EQ(9, FPDFText_GetBoundedText(textpage.get(), 41.0, 56.0, 82.0, 48.0,
                                       buffer, 9));
  EXPECT_THAT(
      pdfium::span(buffer).first(9u),
      ElementsAreArray(pdfium::span(kHelloGoodbyeText).subspan(4u, 9u)));
  EXPECT_EQ(0xbdbd, buffer[9]);

  std::ranges::fill(buffer, 0xbdbd);
  EXPECT_EQ(10, FPDFText_GetBoundedText(textpage.get(), 41.0, 56.0, 82.0, 48.0,
                                        buffer, 128));
  EXPECT_THAT(
      pdfium::span(buffer).first(9u),
      ElementsAreArray(pdfium::span(kHelloGoodbyeText).subspan(4u, 9u)));
  EXPECT_EQ(0u, buffer[9]);
  EXPECT_EQ(0xbdbd, buffer[10]);
}

TEST_F(FPDFTextEmbedderTest, TextVertical) {
  ASSERT_TRUE(OpenDocument("vertical_text.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  EXPECT_EQ(12.0, FPDFText_GetFontSize(textpage.get(), 0));
  EXPECT_EQ(static_cast<uint32_t>('e'), FPDFText_GetUnicode(textpage.get(), 1));
  EXPECT_EQ(static_cast<uint32_t>('l'), FPDFText_GetUnicode(textpage.get(), 2));

  double x = 0.0;
  double y = 0.0;
  EXPECT_TRUE(FPDFText_GetCharOrigin(textpage.get(), 1, &x, &y));
  EXPECT_NEAR(6.664, x, 0.001);
  EXPECT_NEAR(171.508, y, 0.001);

  EXPECT_TRUE(FPDFText_GetCharOrigin(textpage.get(), 2, &x, &y));
  EXPECT_NEAR(8.668, x, 0.001);
  EXPECT_NEAR(160.492, y, 0.001);

  double left;
  double right;
  double bottom;
  double top;
  EXPECT_TRUE(
      FPDFText_GetCharBox(textpage.get(), 1, &left, &right, &bottom, &top));
#if BUILDFLAG(IS_MAC)
  EXPECT_NEAR(7.168, left, 0.001);
#else
  EXPECT_NEAR(7.276, left, 0.001);
#endif
  EXPECT_NEAR(12.808, right, 0.001);
#if BUILDFLAG(IS_MAC)
  EXPECT_NEAR(171.4, bottom, 0.001);
  EXPECT_NEAR(178.06, top, 0.001);
#else
  EXPECT_NEAR(171.364, bottom, 0.001);
  EXPECT_NEAR(178.288, top, 0.001);
#endif
  EXPECT_TRUE(
      FPDFText_GetCharBox(textpage.get(), 2, &left, &right, &bottom, &top));
#if BUILDFLAG(IS_MAC)
  EXPECT_NEAR(9.472, left, 0.001);
  EXPECT_NEAR(10.528, right, 0.001);
  EXPECT_NEAR(160.492, bottom, 0.001);
  EXPECT_NEAR(169.324, top, 0.001);
#else
  EXPECT_NEAR(9.772, left, 0.001);
  EXPECT_NEAR(11.56, right, 0.001);
  EXPECT_NEAR(160.348, bottom, 0.001);
  EXPECT_NEAR(170.188, top, 0.001);
#endif

  FS_RECTF rect;
  EXPECT_TRUE(FPDFText_GetLooseCharBox(textpage.get(), 1, &rect));
  EXPECT_NEAR(4, rect.left, 0.001);
  EXPECT_NEAR(16, rect.right, 0.001);
  EXPECT_NEAR(170.308, rect.bottom, 0.001);
  EXPECT_NEAR(178.984, rect.top, 0.001);

  EXPECT_TRUE(FPDFText_GetLooseCharBox(textpage.get(), 2, &rect));
  EXPECT_NEAR(4, rect.left, 0.001);
  EXPECT_NEAR(16, rect.right, 0.001);
  EXPECT_NEAR(159.292, rect.bottom, 0.001);
  EXPECT_NEAR(170.308, rect.top, 0.001);
}

TEST_F(FPDFTextEmbedderTest, TextHebrewMirrored) {
  ASSERT_TRUE(OpenDocument("hebrew_mirrored.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  static constexpr int kCharCount = 10;
  ASSERT_EQ(kCharCount, FPDFText_CountChars(textpage.get()));

  unsigned short buffer[kCharCount + 1];
  std::ranges::fill(buffer, 0x4242);
  EXPECT_EQ(kCharCount + 1,
            FPDFText_GetText(textpage.get(), 0, kCharCount, buffer));
  EXPECT_EQ(0x05d1, buffer[0]);
  EXPECT_EQ(0x05e0, buffer[1]);
  EXPECT_EQ(0x05d9, buffer[2]);
  EXPECT_EQ(0x05de, buffer[3]);
  EXPECT_EQ(0x05d9, buffer[4]);
  EXPECT_EQ(0x05df, buffer[5]);
  EXPECT_EQ(0x000d, buffer[6]);
  EXPECT_EQ(0x000a, buffer[7]);
  EXPECT_EQ(0x05df, buffer[8]);
  EXPECT_EQ(0x05d1, buffer[9]);
}

TEST_F(FPDFTextEmbedderTest, TextSearch) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  ScopedFPDFWideString nope = GetFPDFWideString(L"nope");
  ScopedFPDFWideString world = GetFPDFWideString(L"world");
  ScopedFPDFWideString world_caps = GetFPDFWideString(L"WORLD");
  ScopedFPDFWideString world_substr = GetFPDFWideString(L"orld");

  {
    // No occurrences of "nope" in test page.
    ScopedFPDFTextFind search(
        FPDFText_FindStart(textpage.get(), nope.get(), 0, 0));
    EXPECT_TRUE(search);
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

    // Advancing finds nothing.
    EXPECT_FALSE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

    // Retreating finds nothing.
    EXPECT_FALSE(FPDFText_FindPrev(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));
  }

  {
    // Two occurrences of "world" in test page.
    ScopedFPDFTextFind search(
        FPDFText_FindStart(textpage.get(), world.get(), 0, 2));
    EXPECT_TRUE(search);

    // Remains not found until advanced.
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

    // First occurrence of "world" in this test page.
    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(7, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(5, FPDFText_GetSchCount(search.get()));

    // Last occurrence of "world" in this test page.
    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(24, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(5, FPDFText_GetSchCount(search.get()));

    // Found position unchanged when fails to advance.
    EXPECT_FALSE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(24, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(5, FPDFText_GetSchCount(search.get()));

    // Back to first occurrence.
    EXPECT_TRUE(FPDFText_FindPrev(search.get()));
    EXPECT_EQ(7, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(5, FPDFText_GetSchCount(search.get()));

    // Found position unchanged when fails to retreat.
    EXPECT_FALSE(FPDFText_FindPrev(search.get()));
    EXPECT_EQ(7, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(5, FPDFText_GetSchCount(search.get()));
  }

  {
    // Exact search unaffected by case sensitiity and whole word flags.
    ScopedFPDFTextFind search(FPDFText_FindStart(
        textpage.get(), world.get(), FPDF_MATCHCASE | FPDF_MATCHWHOLEWORD, 0));
    EXPECT_TRUE(search);
    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(7, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(5, FPDFText_GetSchCount(search.get()));
  }

  {
    // Default is case-insensitive, so matching agaist caps works.
    ScopedFPDFTextFind search(
        FPDFText_FindStart(textpage.get(), world_caps.get(), 0, 0));
    EXPECT_TRUE(search);
    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(7, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(5, FPDFText_GetSchCount(search.get()));
  }

  {
    // But can be made case sensitive, in which case this fails.
    ScopedFPDFTextFind search(FPDFText_FindStart(
        textpage.get(), world_caps.get(), FPDF_MATCHCASE, 0));
    EXPECT_FALSE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));
  }

  {
    // Default is match anywhere within word, so matching substring works.
    ScopedFPDFTextFind search(
        FPDFText_FindStart(textpage.get(), world_substr.get(), 0, 0));
    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(8, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));
  }

  {
    // But can be made to mach word boundaries, in which case this fails.
    ScopedFPDFTextFind search(FPDFText_FindStart(
        textpage.get(), world_substr.get(), FPDF_MATCHWHOLEWORD, 0));
    EXPECT_FALSE(FPDFText_FindNext(search.get()));
    // TODO(tsepez): investigate strange index/count values in this state.
  }
}

TEST_F(FPDFTextEmbedderTest, TextSearchConsecutive) {
  ASSERT_TRUE(OpenDocument("find_text_consecutive.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  ScopedFPDFWideString aaaa = GetFPDFWideString(L"aaaa");

  {
    // Search for "aaaa" yields 2 results in "aaaaaaaaaa".
    ScopedFPDFTextFind search(
        FPDFText_FindStart(textpage.get(), aaaa.get(), 0, 0));
    EXPECT_TRUE(search);

    // Remains not found until advanced.
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

    // First occurrence of "aaaa" in this test page.
    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));

    // Last occurrence of "aaaa" in this test page.
    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(4, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));

    // Found position unchanged when fails to advance.
    EXPECT_FALSE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(4, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));

    // Back to first occurrence.
    EXPECT_TRUE(FPDFText_FindPrev(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));

    // Found position unchanged when fails to retreat.
    EXPECT_FALSE(FPDFText_FindPrev(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));
  }

  {
    // Search for "aaaa" yields 7 results in "aaaaaaaaaa", when searching with
    // FPDF_CONSECUTIVE.
    ScopedFPDFTextFind search(
        FPDFText_FindStart(textpage.get(), aaaa.get(), FPDF_CONSECUTIVE, 0));
    EXPECT_TRUE(search);

    // Remains not found until advanced.
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

    // Find consecutive occurrences of "aaaa" in this test page:
    for (int i = 0; i < 7; ++i) {
      EXPECT_TRUE(FPDFText_FindNext(search.get()));
      EXPECT_EQ(i, FPDFText_GetSchResultIndex(search.get()));
      EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));
    }

    // Found position unchanged when fails to advance.
    EXPECT_FALSE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(6, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));

    for (int i = 5; i >= 0; --i) {
      EXPECT_TRUE(FPDFText_FindPrev(search.get()));
      EXPECT_EQ(i, FPDFText_GetSchResultIndex(search.get()));
      EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));
    }

    // Found position unchanged when fails to retreat.
    EXPECT_FALSE(FPDFText_FindPrev(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));
  }
}

TEST_F(FPDFTextEmbedderTest, TextSearchTermAtEnd) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  ScopedFPDFWideString search_term = GetFPDFWideString(L"world!");
  ScopedFPDFTextFind search(
      FPDFText_FindStart(textpage.get(), search_term.get(), 0, 0));
  ASSERT_TRUE(search);
  EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
  EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

  EXPECT_TRUE(FPDFText_FindNext(search.get()));
  EXPECT_EQ(7, FPDFText_GetSchResultIndex(search.get()));
  EXPECT_EQ(6, FPDFText_GetSchCount(search.get()));

  EXPECT_TRUE(FPDFText_FindNext(search.get()));
  EXPECT_EQ(24, FPDFText_GetSchResultIndex(search.get()));
  EXPECT_EQ(6, FPDFText_GetSchCount(search.get()));
}

TEST_F(FPDFTextEmbedderTest, TextSearchLeadingSpace) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  ScopedFPDFWideString search_term = GetFPDFWideString(L" Good");
  ScopedFPDFTextFind search(
      FPDFText_FindStart(textpage.get(), search_term.get(), 0, 0));
  ASSERT_TRUE(search);
  EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
  EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

  EXPECT_TRUE(FPDFText_FindNext(search.get()));
  EXPECT_EQ(14, FPDFText_GetSchResultIndex(search.get()));
  EXPECT_EQ(5, FPDFText_GetSchCount(search.get()));

  EXPECT_FALSE(FPDFText_FindNext(search.get()));
}

TEST_F(FPDFTextEmbedderTest, TextSearchTrailingSpace) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  ScopedFPDFWideString search_term = GetFPDFWideString(L"ld! ");
  ScopedFPDFTextFind search(
      FPDFText_FindStart(textpage.get(), search_term.get(), 0, 0));
  ASSERT_TRUE(search);
  EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
  EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

  EXPECT_TRUE(FPDFText_FindNext(search.get()));
  EXPECT_EQ(10, FPDFText_GetSchResultIndex(search.get()));
  EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));

  EXPECT_FALSE(FPDFText_FindNext(search.get()));
}

TEST_F(FPDFTextEmbedderTest, TextSearchSpaceInSearchTerm) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  ScopedFPDFWideString search_term = GetFPDFWideString(L"ld! G");
  ScopedFPDFTextFind search(
      FPDFText_FindStart(textpage.get(), search_term.get(), 0, 0));
  ASSERT_TRUE(search);
  EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
  EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

  EXPECT_TRUE(FPDFText_FindNext(search.get()));
  EXPECT_EQ(10, FPDFText_GetSchResultIndex(search.get()));
  // Note: Even though `search_term` contains 5 characters,
  // FPDFText_FindNext() matched "\r\n" in `textpage` against the space in
  // `search_term`.
  EXPECT_EQ(6, FPDFText_GetSchCount(search.get()));

  EXPECT_FALSE(FPDFText_FindNext(search.get()));
}

// Fails on Windows. https://crbug.com/pdfium/1370
#if BUILDFLAG(IS_WIN)
#define MAYBE_TextSearchLatinExtended DISABLED_TextSearchLatinExtended
#else
#define MAYBE_TextSearchLatinExtended TextSearchLatinExtended
#endif
TEST_F(FPDFTextEmbedderTest, MAYBE_TextSearchLatinExtended) {
  ASSERT_TRUE(OpenDocument("latin_extended.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  // Upper/lowercase 'a' with breve.
  static constexpr FPDF_WCHAR kNeedleUpper[] = {0x0102, 0x0000};
  static constexpr FPDF_WCHAR kNeedleLower[] = {0x0103, 0x0000};

  for (const auto* needle : {kNeedleUpper, kNeedleLower}) {
    ScopedFPDFTextFind search(FPDFText_FindStart(textpage.get(), needle, 0, 0));
    EXPECT_TRUE(search);
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

    // Should find 2 results at position 21/22, both with length 1.
    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(2, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(1, FPDFText_GetSchCount(search.get()));
    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(3, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(1, FPDFText_GetSchCount(search.get()));
    // And no more than 2 results.
    EXPECT_FALSE(FPDFText_FindNext(search.get()));
  }
}

// Test that the page has characters despite a bad stream length.
TEST_F(FPDFTextEmbedderTest, StreamLengthPastEndOfFile) {
  ASSERT_TRUE(OpenDocument("bug_57.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);
  EXPECT_EQ(13, FPDFText_CountChars(textpage.get()));
}

TEST_F(FPDFTextEmbedderTest, WebLinks) {
  ASSERT_TRUE(OpenDocument("weblinks.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  {
    ScopedFPDFPageLink pagelink(FPDFLink_LoadWebLinks(textpage.get()));
    EXPECT_TRUE(pagelink);

    // Page contains two HTTP-style URLs.
    EXPECT_EQ(2, FPDFLink_CountWebLinks(pagelink.get()));

    // Only a terminating NUL required for bogus links.
    EXPECT_EQ(1, FPDFLink_GetURL(pagelink.get(), 2, nullptr, 0));
    EXPECT_EQ(1, FPDFLink_GetURL(pagelink.get(), 1400, nullptr, 0));
    EXPECT_EQ(1, FPDFLink_GetURL(pagelink.get(), -1, nullptr, 0));
  }

  FPDF_PAGELINK pagelink = FPDFLink_LoadWebLinks(textpage.get());
  EXPECT_TRUE(pagelink);

  // Query the number of characters required for each link (incl NUL).
  EXPECT_EQ(25, FPDFLink_GetURL(pagelink, 0, nullptr, 0));
  EXPECT_EQ(26, FPDFLink_GetURL(pagelink, 1, nullptr, 0));

  static constexpr char kExpectedUrl[] = "http://example.com?q=foo";
  static constexpr size_t kExpectedLen = sizeof(kExpectedUrl);
  std::array<unsigned short, 128> buffer;

  // Retrieve a link with too small a buffer.  Buffer will not be
  // NUL-terminated, but must not be modified past indicated length,
  // so pre-fill with a pattern to check write bounds.
  std::ranges::fill(buffer, 0xbdbd);
  EXPECT_EQ(1, FPDFLink_GetURL(pagelink, 0, buffer.data(), 1));
  EXPECT_EQ('h', buffer[0]);
  EXPECT_EQ(0xbdbd, buffer[1]);

  // Check buffer that doesn't have space for a terminating NUL.
  std::ranges::fill(buffer, 0xbdbd);
  auto kExpectedUrlTruncated =
      pdfium::span(kExpectedUrl).first(kExpectedLen - 1);
  EXPECT_EQ(static_cast<int>(kExpectedUrlTruncated.size()),
            FPDFLink_GetURL(pagelink, 0, buffer.data(), kExpectedLen - 1));
  EXPECT_THAT(pdfium::span(buffer).first(kExpectedLen - 1),
              ElementsAreArray(kExpectedUrlTruncated));
  EXPECT_EQ(0xbdbd, buffer[kExpectedLen - 1]);

  // Retreive link with exactly-sized buffer.
  std::ranges::fill(buffer, 0xbdbd);
  EXPECT_EQ(static_cast<int>(kExpectedLen),
            FPDFLink_GetURL(pagelink, 0, buffer.data(), kExpectedLen));
  EXPECT_THAT(pdfium::span(buffer).first(kExpectedLen),
              ElementsAreArray(kExpectedUrl));
  EXPECT_EQ(0u, buffer[kExpectedLen - 1]);
  EXPECT_EQ(0xbdbd, buffer[kExpectedLen]);

  // Retreive link with ample-sized-buffer.
  std::ranges::fill(buffer, 0xbdbd);
  EXPECT_EQ(static_cast<int>(kExpectedLen),
            FPDFLink_GetURL(pagelink, 0, buffer.data(), buffer.size()));
  EXPECT_THAT(pdfium::span(buffer).first(kExpectedLen),
              ElementsAreArray(kExpectedUrl));
  EXPECT_EQ(0u, buffer[kExpectedLen - 1]);
  EXPECT_EQ(0xbdbd, buffer[kExpectedLen]);

  // Each link rendered in a single rect in this test page.
  EXPECT_EQ(1, FPDFLink_CountRects(pagelink, 0));
  EXPECT_EQ(1, FPDFLink_CountRects(pagelink, 1));

  // Each link rendered in a single rect in this test page.
  EXPECT_EQ(0, FPDFLink_CountRects(pagelink, -1));
  EXPECT_EQ(0, FPDFLink_CountRects(pagelink, 2));
  EXPECT_EQ(0, FPDFLink_CountRects(pagelink, 10000));

  // Check boundary of valid link index with valid rect index.
  double left = 0.0;
  double right = 0.0;
  double top = 0.0;
  double bottom = 0.0;
  EXPECT_TRUE(FPDFLink_GetRect(pagelink, 0, 0, &left, &top, &right, &bottom));
  EXPECT_NEAR(50.828, left, 0.001);
  EXPECT_NEAR(187.904, right, 0.001);
  EXPECT_NEAR(97.516, bottom, 0.001);
  EXPECT_NEAR(108.700, top, 0.001);

  // Check that valid link with invalid rect index leaves parameters unchanged.
  left = -1.0;
  right = -1.0;
  top = -1.0;
  bottom = -1.0;
  EXPECT_FALSE(FPDFLink_GetRect(pagelink, 0, 1, &left, &top, &right, &bottom));
  EXPECT_EQ(-1.0, left);
  EXPECT_EQ(-1.0, right);
  EXPECT_EQ(-1.0, bottom);
  EXPECT_EQ(-1.0, top);

  // Check that invalid link index leaves parameters unchanged.
  left = -2.0;
  right = -2.0;
  top = -2.0;
  bottom = -2.0;
  EXPECT_FALSE(FPDFLink_GetRect(pagelink, -1, 0, &left, &top, &right, &bottom));
  EXPECT_EQ(-2.0, left);
  EXPECT_EQ(-2.0, right);
  EXPECT_EQ(-2.0, bottom);
  EXPECT_EQ(-2.0, top);

  FPDFLink_CloseWebLinks(pagelink);
}

TEST_F(FPDFTextEmbedderTest, WebLinksAcrossLines) {
  ASSERT_TRUE(OpenDocument("weblinks_across_lines.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  FPDF_PAGELINK pagelink = FPDFLink_LoadWebLinks(textpage.get());
  EXPECT_TRUE(pagelink);

  static constexpr auto kExpectedUrls = std::to_array<const char*>({
      "http://example.com",           // from "http://www.example.com?\r\nfoo"
      "http://example.com/",          // from "http://www.example.com/\r\nfoo"
      "http://example.com/test-foo",  // from "http://example.com/test-\r\nfoo"
      "http://abc.com/test-foo",      // from "http://abc.com/test-\r\n\r\nfoo"
      // Next two links from "http://www.example.com/\r\nhttp://www.abc.com/"
      "http://example.com/",
      "http://www.abc.com",
  });
  static const int kNumLinks = static_cast<int>(std::size(kExpectedUrls));

  EXPECT_EQ(kNumLinks, FPDFLink_CountWebLinks(pagelink));

  for (int i = 0; i < kNumLinks; i++) {
    std::array<unsigned short, 128> buffer = {};
    ByteStringView expected_url(kExpectedUrls[i]);
    EXPECT_EQ(static_cast<int>(expected_url.GetLength() + 1),
              FPDFLink_GetURL(pagelink, i, nullptr, 0));
    EXPECT_EQ(static_cast<int>(expected_url.GetLength() + 1),
              FPDFLink_GetURL(pagelink, i, buffer.data(), buffer.size()));
    EXPECT_THAT(pdfium::span(buffer).first(expected_url.GetLength()),
                ElementsAreArray(expected_url));
    EXPECT_EQ(0u, buffer[expected_url.GetLength()]);
  }

  FPDFLink_CloseWebLinks(pagelink);
}

TEST_F(FPDFTextEmbedderTest, WebLinksAcrossLinesBug) {
  ASSERT_TRUE(OpenDocument("bug_650.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  FPDF_PAGELINK pagelink = FPDFLink_LoadWebLinks(textpage.get());
  EXPECT_TRUE(pagelink);

  EXPECT_EQ(2, FPDFLink_CountWebLinks(pagelink));
  unsigned short buffer[128] = {0};
  static constexpr char kExpectedUrl[] =
      "http://tutorial45.com/learn-autocad-basics-day-166/";
  static constexpr size_t kUrlSize = sizeof(kExpectedUrl);

  EXPECT_EQ(static_cast<int>(kUrlSize),
            FPDFLink_GetURL(pagelink, 1, nullptr, 0));
  EXPECT_EQ(static_cast<int>(kUrlSize),
            FPDFLink_GetURL(pagelink, 1, buffer, std::size(buffer)));
  EXPECT_THAT(pdfium::span(buffer).first(kUrlSize),
              ElementsAreArray(kExpectedUrl));

  FPDFLink_CloseWebLinks(pagelink);
}

TEST_F(FPDFTextEmbedderTest, WebLinksCharRanges) {
  ASSERT_TRUE(OpenDocument("weblinks.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage text_page(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(text_page);

  FPDF_PAGELINK page_link = FPDFLink_LoadWebLinks(text_page.get());
  EXPECT_TRUE(page_link);

  // Test for char indices of a valid link
  int start_char_index;
  int char_count;
  ASSERT_TRUE(
      FPDFLink_GetTextRange(page_link, 0, &start_char_index, &char_count));
  EXPECT_EQ(35, start_char_index);
  EXPECT_EQ(24, char_count);

  // Test for char indices of an invalid link
  start_char_index = -10;
  char_count = -8;
  ASSERT_FALSE(
      FPDFLink_GetTextRange(page_link, 6, &start_char_index, &char_count));
  EXPECT_EQ(start_char_index, -10);
  EXPECT_EQ(char_count, -8);

  // Test for pagelink = nullptr
  start_char_index = -10;
  char_count = -8;
  ASSERT_FALSE(
      FPDFLink_GetTextRange(nullptr, 0, &start_char_index, &char_count));
  EXPECT_EQ(start_char_index, -10);
  EXPECT_EQ(char_count, -8);

  // Test for link_index < 0
  start_char_index = -10;
  char_count = -8;
  ASSERT_FALSE(
      FPDFLink_GetTextRange(page_link, -4, &start_char_index, &char_count));
  EXPECT_EQ(start_char_index, -10);
  EXPECT_EQ(char_count, -8);

  FPDFLink_CloseWebLinks(page_link);
}

TEST_F(FPDFTextEmbedderTest, AnnotLinks) {
  ASSERT_TRUE(OpenDocument("annots.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  // Get link count via checking annotation subtype
  int annot_count = FPDFPage_GetAnnotCount(page.get());
  ASSERT_EQ(9, annot_count);
  int annot_subtype_link_count = 0;
  for (int i = 0; i < annot_count; ++i) {
    ScopedFPDFAnnotation annot(FPDFPage_GetAnnot(page.get(), i));
    if (FPDFAnnot_GetSubtype(annot.get()) == FPDF_ANNOT_LINK) {
      ++annot_subtype_link_count;
    }
  }
  EXPECT_EQ(4, annot_subtype_link_count);

  // Validate that FPDFLink_Enumerate() returns same number of links
  int start_pos = 0;
  FPDF_LINK link_annot;
  int link_count = 0;
  while (FPDFLink_Enumerate(page.get(), &start_pos, &link_annot)) {
    ASSERT_TRUE(link_annot);
    if (start_pos == 1 || start_pos == 2) {
      // First two links point to first and second page within the document
      // respectively
      FPDF_DEST link_dest = FPDFLink_GetDest(document(), link_annot);
      EXPECT_TRUE(link_dest);
      EXPECT_EQ(start_pos - 1,
                FPDFDest_GetDestPageIndex(document(), link_dest));
    } else if (start_pos == 3) {  // points to PDF Spec URL
      FS_RECTF link_rect;
      EXPECT_TRUE(FPDFLink_GetAnnotRect(link_annot, &link_rect));
      EXPECT_NEAR(66.0, link_rect.left, 0.001);
      EXPECT_NEAR(544.0, link_rect.top, 0.001);
      EXPECT_NEAR(196.0, link_rect.right, 0.001);
      EXPECT_NEAR(529.0, link_rect.bottom, 0.001);
    } else if (start_pos == 4) {  // this link has quad points
      int quad_point_count = FPDFLink_CountQuadPoints(link_annot);
      EXPECT_EQ(1, quad_point_count);
      FS_QUADPOINTSF quad_points;
      EXPECT_TRUE(FPDFLink_GetQuadPoints(link_annot, 0, &quad_points));
      EXPECT_NEAR(83.0, quad_points.x1, 0.001);
      EXPECT_NEAR(453.0, quad_points.y1, 0.001);
      EXPECT_NEAR(178.0, quad_points.x2, 0.001);
      EXPECT_NEAR(453.0, quad_points.y2, 0.001);
      EXPECT_NEAR(83.0, quad_points.x3, 0.001);
      EXPECT_NEAR(440.0, quad_points.y3, 0.001);
      EXPECT_NEAR(178.0, quad_points.x4, 0.001);
      EXPECT_NEAR(440.0, quad_points.y4, 0.001);
      // AnnotRect is same as quad points for this link
      FS_RECTF link_rect;
      EXPECT_TRUE(FPDFLink_GetAnnotRect(link_annot, &link_rect));
      EXPECT_NEAR(link_rect.left, quad_points.x1, 0.001);
      EXPECT_NEAR(link_rect.top, quad_points.y1, 0.001);
      EXPECT_NEAR(link_rect.right, quad_points.x4, 0.001);
      EXPECT_NEAR(link_rect.bottom, quad_points.y4, 0.001);
    }
    ++link_count;
  }
  EXPECT_EQ(annot_subtype_link_count, link_count);
}

TEST_F(FPDFTextEmbedderTest, GetFontSize) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  static constexpr auto kExpectedFontsSizes = std::to_array<const double>(
      {12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 1,  1,
       16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16});

  int count = FPDFText_CountChars(textpage.get());
  ASSERT_EQ(std::size(kExpectedFontsSizes), static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    EXPECT_EQ(kExpectedFontsSizes[i], FPDFText_GetFontSize(textpage.get(), i))
        << i;
  }
}

TEST_F(FPDFTextEmbedderTest, GetFontInfo) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);
  std::vector<char> font_name;
  size_t num_chars1 = strlen("Hello, world!");
  const char kExpectedFontName1[] = "Times-Roman";

  for (size_t i = 0; i < num_chars1; i++) {
    int flags = -1;
    unsigned long length =
        FPDFText_GetFontInfo(textpage.get(), i, nullptr, 0, &flags);
    static constexpr unsigned long expected_length = sizeof(kExpectedFontName1);
    ASSERT_EQ(expected_length, length);
    EXPECT_EQ(pdfium::kFontStyleNonSymbolic, flags);
    font_name.resize(length);
    std::fill(font_name.begin(), font_name.end(), 'a');
    flags = -1;
    EXPECT_EQ(expected_length,
              FPDFText_GetFontInfo(textpage.get(), i, font_name.data(),
                                   font_name.size(), &flags));
    EXPECT_STREQ(kExpectedFontName1, font_name.data());
    EXPECT_EQ(pdfium::kFontStyleNonSymbolic, flags);
  }
  // If the size of the buffer is not large enough, the buffer should remain
  // unchanged.
  font_name.pop_back();
  std::fill(font_name.begin(), font_name.end(), 'a');
  EXPECT_EQ(sizeof(kExpectedFontName1),
            FPDFText_GetFontInfo(textpage.get(), 0, font_name.data(),
                                 font_name.size(), nullptr));
  for (char a : font_name) {
    EXPECT_EQ('a', a);
  }

  // The text is "Hello, world!\r\nGoodbye, world!", so the next two characters
  // do not have any font information.
  EXPECT_EQ(0u,
            FPDFText_GetFontInfo(textpage.get(), num_chars1, font_name.data(),
                                 font_name.size(), nullptr));
  EXPECT_EQ(
      0u, FPDFText_GetFontInfo(textpage.get(), num_chars1 + 1, font_name.data(),
                               font_name.size(), nullptr));

  size_t num_chars2 = strlen("Goodbye, world!");
  const char kExpectedFontName2[] = "Helvetica";
  for (size_t i = num_chars1 + 2; i < num_chars1 + num_chars2 + 2; i++) {
    int flags = -1;
    unsigned long length =
        FPDFText_GetFontInfo(textpage.get(), i, nullptr, 0, &flags);
    static constexpr unsigned long expected_length = sizeof(kExpectedFontName2);
    ASSERT_EQ(expected_length, length);
    EXPECT_EQ(pdfium::kFontStyleNonSymbolic, flags);
    font_name.resize(length);
    std::fill(font_name.begin(), font_name.end(), 'a');
    flags = -1;
    EXPECT_EQ(expected_length,
              FPDFText_GetFontInfo(textpage.get(), i, font_name.data(),
                                   font_name.size(), &flags));
    EXPECT_STREQ(kExpectedFontName2, font_name.data());
    EXPECT_EQ(pdfium::kFontStyleNonSymbolic, flags);
  }

  // Now try some out of bounds indices and null pointers to make sure we do not
  // crash.
  // No textpage.
  EXPECT_EQ(0u, FPDFText_GetFontInfo(nullptr, 0, font_name.data(),
                                     font_name.size(), nullptr));
  // No buffer.
  EXPECT_EQ(sizeof(kExpectedFontName1),
            FPDFText_GetFontInfo(textpage.get(), 0, nullptr, 0, nullptr));
  // Negative index.
  EXPECT_EQ(0u, FPDFText_GetFontInfo(textpage.get(), -1, font_name.data(),
                                     font_name.size(), nullptr));
  // Out of bounds index.
  EXPECT_EQ(0u, FPDFText_GetFontInfo(textpage.get(), 1000, font_name.data(),
                                     font_name.size(), nullptr));
}

TEST_F(FPDFTextEmbedderTest, ToUnicode) {
  ASSERT_TRUE(OpenDocument("bug_583.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  ASSERT_EQ(1, FPDFText_CountChars(textpage.get()));
  EXPECT_EQ(0U, FPDFText_GetUnicode(textpage.get(), 0));
}

TEST_F(FPDFTextEmbedderTest, IsGenerated) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  EXPECT_EQ(static_cast<unsigned int>('H'),
            FPDFText_GetUnicode(textpage.get(), 0));
  EXPECT_EQ(0, FPDFText_IsGenerated(textpage.get(), 0));
  EXPECT_EQ(static_cast<unsigned int>(' '),
            FPDFText_GetUnicode(textpage.get(), 6));
  EXPECT_EQ(0, FPDFText_IsGenerated(textpage.get(), 6));

  EXPECT_EQ(static_cast<unsigned int>('\r'),
            FPDFText_GetUnicode(textpage.get(), 13));
  EXPECT_EQ(1, FPDFText_IsGenerated(textpage.get(), 13));
  EXPECT_EQ(static_cast<unsigned int>('\n'),
            FPDFText_GetUnicode(textpage.get(), 14));
  EXPECT_EQ(1, FPDFText_IsGenerated(textpage.get(), 14));

  EXPECT_EQ(-1, FPDFText_IsGenerated(textpage.get(), -1));
  EXPECT_EQ(-1, FPDFText_IsGenerated(textpage.get(), kHelloGoodbyeTextSize));
  EXPECT_EQ(-1, FPDFText_IsGenerated(nullptr, 6));
}

TEST_F(FPDFTextEmbedderTest, IsHyphen) {
  ASSERT_TRUE(OpenDocument("bug_781804.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  EXPECT_EQ(static_cast<unsigned int>('V'),
            FPDFText_GetUnicode(textpage.get(), 0));
  EXPECT_EQ(0, FPDFText_IsHyphen(textpage.get(), 0));
  EXPECT_EQ(static_cast<unsigned int>('\2'),
            FPDFText_GetUnicode(textpage.get(), 6));
  EXPECT_EQ(1, FPDFText_IsHyphen(textpage.get(), 6));

  EXPECT_EQ(static_cast<unsigned int>('U'),
            FPDFText_GetUnicode(textpage.get(), 14));
  EXPECT_EQ(0, FPDFText_IsHyphen(textpage.get(), 14));
  EXPECT_EQ(static_cast<unsigned int>(L'\u2010'),
            FPDFText_GetUnicode(textpage.get(), 18));
  EXPECT_EQ(0, FPDFText_IsHyphen(textpage.get(), 18));

  EXPECT_EQ(-1, FPDFText_IsHyphen(textpage.get(), -1));
  EXPECT_EQ(-1, FPDFText_IsHyphen(textpage.get(), 1000));
  EXPECT_EQ(-1, FPDFText_IsHyphen(nullptr, 6));
}

TEST_F(FPDFTextEmbedderTest, IsInvalidUnicode) {
  ASSERT_TRUE(OpenDocument("bug_1388_2.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  static constexpr int kExpectedCharCount = 5;
  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);
  EXPECT_EQ(kExpectedCharCount, FPDFText_CountChars(textpage.get()));

  EXPECT_EQ(static_cast<unsigned int>('X'),
            FPDFText_GetUnicode(textpage.get(), 0));
  EXPECT_EQ(0, FPDFText_HasUnicodeMapError(textpage.get(), 0));
  EXPECT_EQ(static_cast<unsigned int>(' '),
            FPDFText_GetUnicode(textpage.get(), 1));
  EXPECT_EQ(0, FPDFText_HasUnicodeMapError(textpage.get(), 1));

  EXPECT_EQ(31u, FPDFText_GetUnicode(textpage.get(), 2));
  EXPECT_EQ(1, FPDFText_HasUnicodeMapError(textpage.get(), 2));

  EXPECT_EQ(-1, FPDFText_HasUnicodeMapError(textpage.get(), -1));
  EXPECT_EQ(-1,
            FPDFText_HasUnicodeMapError(textpage.get(), kExpectedCharCount));
  EXPECT_EQ(-1, FPDFText_HasUnicodeMapError(nullptr, 0));
}

TEST_F(FPDFTextEmbedderTest, Bug921) {
  ASSERT_TRUE(OpenDocument("bug_921.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  static constexpr auto kData = std::to_array<const unsigned int>(
      {1095, 1077, 1083, 1086, 1074, 1077, 1095, 1077, 1089, 1082, 1086, 1077,
       32,   1089, 1090, 1088, 1072, 1076, 1072, 1085, 1080, 1077, 46,   32});
  static constexpr int kStartIndex = 238;

  ASSERT_EQ(268, FPDFText_CountChars(textpage.get()));
  for (size_t i = 0; i < std::size(kData); ++i) {
    EXPECT_EQ(kData[i], FPDFText_GetUnicode(textpage.get(), kStartIndex + i));
  }
  std::array<unsigned short, std::size(kData) + 1> buffer;
  std::ranges::fill(buffer, 0xbdbd);
  int count = FPDFText_GetText(textpage.get(), kStartIndex, kData.size(),
                               buffer.data());
  ASSERT_GT(count, 0);
  ASSERT_EQ(std::size(kData) + 1, static_cast<size_t>(count));
  for (size_t i = 0; i < std::size(kData); ++i) {
    EXPECT_EQ(kData[i], buffer[i]);
  }
  EXPECT_EQ(0, buffer[kData.size()]);
}

TEST_F(FPDFTextEmbedderTest, GetTextWithHyphen) {
  ASSERT_TRUE(OpenDocument("bug_781804.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  // Check that soft hyphens are not included
  // Expecting 'Veritaserum', except there is a \uFFFE where the hyphen was in
  // the original text. This is a weird thing that Adobe does, which we
  // replicate.
  static constexpr auto soft_expected = std::to_array<unsigned short>(
      {0x0056, 0x0065, 0x0072, 0x0069, 0x0074, 0x0061, 0xfffe, 0x0073, 0x0065,
       0x0072, 0x0075, 0x006D, 0x0000});
  {
    static constexpr int count = std::size(soft_expected) - 1;
    std::array<unsigned short, soft_expected.size()> buffer = {};
    EXPECT_EQ(count + 1,
              FPDFText_GetText(textpage.get(), 0, count, buffer.data()));
    EXPECT_THAT(buffer, ElementsAreArray(soft_expected));
  }

  // Check that hard hyphens are included
  {
    // There isn't the \0 in the actual doc, but there is a \r\n, so need to
    // add 1 to get aligned.
    static constexpr size_t offset = std::size(soft_expected) + 1;
    // Expecting 'User-\r\ngenerated', the - is a unicode character, so cannot
    // store in a char[].
    static constexpr auto hard_expected = std::to_array<unsigned short>(
        {0x0055, 0x0073, 0x0065, 0x0072, 0x2010, 0x000d, 0x000a, 0x0067, 0x0065,
         0x006e, 0x0065, 0x0072, 0x0061, 0x0074, 0x0065, 0x0064, 0x0000});
    static constexpr int count = std::size(hard_expected) - 1;
    std::array<unsigned short, hard_expected.size()> buffer;
    EXPECT_EQ(count + 1,
              FPDFText_GetText(textpage.get(), offset, count, buffer.data()));
    for (int i = 0; i < count; i++) {
      EXPECT_EQ(hard_expected[i], buffer[i]);
    }
  }
}

TEST_F(FPDFTextEmbedderTest, Bug782596) {
  // If there is a regression in this test, it will only fail under ASAN
  ASSERT_TRUE(OpenDocument("bug_782596.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);
  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);
}

TEST_F(FPDFTextEmbedderTest, ControlCharacters) {
  ASSERT_TRUE(OpenDocument("control_characters.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  // Should not include the control characters in the output
  unsigned short buffer[128];
  std::ranges::fill(buffer, 0xbdbd);
  int num_chars = FPDFText_GetText(textpage.get(), 0, 128, buffer);
  ASSERT_EQ(kHelloGoodbyeTextSize, num_chars);
  EXPECT_THAT(pdfium::span(buffer).first<kHelloGoodbyeTextSize>(),
              ElementsAreArray(kHelloGoodbyeText));

  // Attempting to get a chunk of text after the control characters
  static const char kExpectedSubstring[] = "Goodbye, world!";
  // Offset is the length of 'Hello, world!\r\n' + 2 control characters in the
  // original stream
  std::ranges::fill(buffer, 0xbdbd);
  num_chars = FPDFText_GetText(textpage.get(), 17, 128, buffer);

  ASSERT_GE(num_chars, 0);
  EXPECT_EQ(sizeof(kExpectedSubstring), static_cast<size_t>(num_chars));
  EXPECT_THAT(pdfium::span(buffer).first(sizeof(kExpectedSubstring)),
              ElementsAreArray(kExpectedSubstring));
}

// Testing that hyphen makers (0x0002) are replacing hard hyphens when
// the word contains non-ASCII characters.
TEST_F(FPDFTextEmbedderTest, Bug1029) {
  ASSERT_TRUE(OpenDocument("bug_1029.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  static constexpr int page_range_offset = 171;
  static constexpr int page_range_length = 56;

  // This text is:
  // 'METADATA table. When the split has committed, it noti' followed
  // by a 'soft hyphen' (0x0002) and then 'fi'.
  //
  // The original text has a fi ligature, but that is broken up into
  // two characters when the PDF is processed.
  static constexpr auto expected = std::to_array<unsigned int>({
      0x004d, 0x0045, 0x0054, 0x0041, 0x0044, 0x0041, 0x0054, 0x0041,
      0x0020, 0x0074, 0x0061, 0x0062, 0x006c, 0x0065, 0x002e, 0x0020,
      0x0057, 0x0068, 0x0065, 0x006e, 0x0020, 0x0074, 0x0068, 0x0065,
      0x0020, 0x0073, 0x0070, 0x006c, 0x0069, 0x0074, 0x0020, 0x0068,
      0x0061, 0x0073, 0x0020, 0x0063, 0x006f, 0x006d, 0x006d, 0x0069,
      0x0074, 0x0074, 0x0065, 0x0064, 0x002c, 0x0020, 0x0069, 0x0074,
      0x0020, 0x006e, 0x006f, 0x0074, 0x0069, 0x0002, 0x0066, 0x0069,
  });
  static_assert(page_range_length == std::size(expected),
                "Expected should be the same size as the range being "
                "extracted from page.");
  EXPECT_LT(page_range_offset + page_range_length,
            FPDFText_CountChars(textpage.get()));

  for (int i = 0; i < page_range_length; ++i) {
    EXPECT_EQ(expected[i],
              FPDFText_GetUnicode(textpage.get(), page_range_offset + i));
  }
}

TEST_F(FPDFTextEmbedderTest, CountRects) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  // Sanity check hello_world.pdf.
  // |num_chars| check includes the terminating NUL that is provided.
  {
    unsigned short buffer[128];
    int num_chars = FPDFText_GetText(textpage.get(), 0, 128, buffer);
    ASSERT_EQ(kHelloGoodbyeTextSize, num_chars);
    EXPECT_THAT(pdfium::span(buffer).first<kHelloGoodbyeTextSize>(),
                ElementsAreArray(kHelloGoodbyeText));
  }

  // Now test FPDFText_CountRects().
  static const int kHelloWorldEnd = strlen("Hello, world!");
  static const int kGoodbyeWorldStart = kHelloWorldEnd + 2;  // "\r\n"
  for (int start = 0; start < kHelloWorldEnd; ++start) {
    // Always grab some part of "hello world" and some part of "goodbye world"
    // Since -1 means "all".
    EXPECT_EQ(2, FPDFText_CountRects(textpage.get(), start, -1));

    // No characters always means 0 rects.
    EXPECT_EQ(0, FPDFText_CountRects(textpage.get(), start, 0));

    // 1 character stays within "hello world"
    EXPECT_EQ(1, FPDFText_CountRects(textpage.get(), start, 1));

    // When |start| is 0, Having |kGoodbyeWorldStart| char count does not reach
    // "goodbye world".
    int expected_value = start ? 2 : 1;
    EXPECT_EQ(expected_value,
              FPDFText_CountRects(textpage.get(), start, kGoodbyeWorldStart));

    // Extremely large character count will always return 2 rects because
    // |start| starts inside "hello world".
    EXPECT_EQ(2, FPDFText_CountRects(textpage.get(), start, 500));
  }

  // Now test negative counts.
  for (int start = 0; start < kHelloWorldEnd; ++start) {
    EXPECT_EQ(2, FPDFText_CountRects(textpage.get(), start, -100));
    EXPECT_EQ(2, FPDFText_CountRects(textpage.get(), start, -2));
  }

  // Now test larger start values.
  const int kExpectedLength = UNSAFE_TODO(strlen(kHelloGoodbyeText));
  for (int start = kGoodbyeWorldStart + 1; start < kExpectedLength; ++start) {
    EXPECT_EQ(1, FPDFText_CountRects(textpage.get(), start, -1));
    EXPECT_EQ(0, FPDFText_CountRects(textpage.get(), start, 0));
    EXPECT_EQ(1, FPDFText_CountRects(textpage.get(), start, 1));
    EXPECT_EQ(1, FPDFText_CountRects(textpage.get(), start, 2));
    EXPECT_EQ(1, FPDFText_CountRects(textpage.get(), start, 500));
  }

  // Now test start values that starts beyond the end of the text.
  for (int start = kExpectedLength; start < 100; ++start) {
    EXPECT_EQ(0, FPDFText_CountRects(textpage.get(), start, -1));
    EXPECT_EQ(0, FPDFText_CountRects(textpage.get(), start, 0));
    EXPECT_EQ(0, FPDFText_CountRects(textpage.get(), start, 1));
    EXPECT_EQ(0, FPDFText_CountRects(textpage.get(), start, 2));
    EXPECT_EQ(0, FPDFText_CountRects(textpage.get(), start, 500));
  }
}

TEST_F(FPDFTextEmbedderTest, GetText) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage text_page(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(text_page);

  EXPECT_EQ(2, FPDFPage_CountObjects(page.get()));
  FPDF_PAGEOBJECT text_object = FPDFPage_GetObject(page.get(), 0);
  ASSERT_TRUE(text_object);

  // Positive testing.
  static constexpr char kHelloText[] = "Hello, world!";
  // Return value includes the terminating NUL that is provided.
  static constexpr unsigned long kHelloUTF16Size = std::size(kHelloText) * 2;
  static constexpr wchar_t kHelloWideText[] = L"Hello, world!";
  unsigned long size =
      FPDFTextObj_GetText(text_object, text_page.get(), nullptr, 0);
  ASSERT_EQ(kHelloUTF16Size, size);

  std::vector<unsigned short> buffer(size);
  ASSERT_EQ(size, FPDFTextObj_GetText(text_object, text_page.get(),
                                      buffer.data(), size));
  ASSERT_EQ(kHelloWideText, GetPlatformWString(buffer.data()));

  // Negative testing.
  ASSERT_EQ(0U, FPDFTextObj_GetText(nullptr, text_page.get(), nullptr, 0));
  ASSERT_EQ(0U, FPDFTextObj_GetText(text_object, nullptr, nullptr, 0));
  ASSERT_EQ(0U, FPDFTextObj_GetText(nullptr, nullptr, nullptr, 0));

  // Buffer is too small, ensure it's not modified.
  buffer.resize(2);
  buffer[0] = 'x';
  buffer[1] = '\0';
  size = FPDFTextObj_GetText(text_object, text_page.get(), buffer.data(),
                             buffer.size());
  ASSERT_EQ(kHelloUTF16Size, size);
  ASSERT_EQ('x', buffer[0]);
  ASSERT_EQ('\0', buffer[1]);
}

TEST_F(FPDFTextEmbedderTest, CroppedText) {
  static constexpr int kPageCount = 4;
  static constexpr std::array<FS_RECTF, kPageCount> kBoxes = {{
      {50.0f, 150.0f, 150.0f, 50.0f},
      {50.0f, 150.0f, 150.0f, 50.0f},
      {60.0f, 150.0f, 150.0f, 60.0f},
      {60.0f, 150.0f, 150.0f, 60.0f},
  }};
  static constexpr std::array<const char*, kPageCount> kExpectedText = {{
      " world!\r\ndbye, world!",
      " world!\r\ndbye, world!",
      "bye, world!",
      "bye, world!",
  }};

  ASSERT_TRUE(OpenDocument("cropped_text.pdf"));
  ASSERT_EQ(kPageCount, FPDF_GetPageCount(document()));

  for (int i = 0; i < kPageCount; ++i) {
    ScopedPage page = LoadScopedPage(i);
    ASSERT_TRUE(page);

    FS_RECTF box;
    EXPECT_TRUE(FPDF_GetPageBoundingBox(page.get(), &box));
    CompareFS_RECTF(kBoxes[i], box);

    ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
    ASSERT_TRUE(textpage);

    unsigned short buffer[128];
    std::ranges::fill(buffer, 0xbdbd);
    int num_chars = FPDFText_GetText(textpage.get(), 0, 128, buffer);
    ASSERT_EQ(kHelloGoodbyeTextSize, num_chars);
    EXPECT_THAT(pdfium::span(buffer).first<kHelloGoodbyeTextSize>(),
                ElementsAreArray(kHelloGoodbyeText));

    ByteStringView expected_text(kExpectedText[i]);
    ASSERT_EQ(static_cast<int>(expected_text.GetLength()),
              FPDFText_GetBoundedText(textpage.get(), box.left, box.top,
                                      box.right, box.bottom, nullptr, 0));

    std::ranges::fill(buffer, 0xbdbd);
    ASSERT_EQ(static_cast<int>(expected_text.GetLength()) + 1,
              FPDFText_GetBoundedText(textpage.get(), box.left, box.top,
                                      box.right, box.bottom, buffer, 128));
    EXPECT_THAT(pdfium::span(buffer).first(expected_text.GetLength()),
                ElementsAreArray(expected_text));
  }
}

TEST_F(FPDFTextEmbedderTest, Bug1139) {
  ASSERT_TRUE(OpenDocument("bug_1139.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage text_page(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(text_page);

  // -1 for CountChars not including the \0, but +1 for the extra control
  // character.
  EXPECT_EQ(kHelloGoodbyeTextSize, FPDFText_CountChars(text_page.get()));

  // There is an extra control character at the beginning of the string, but it
  // should not appear in the output nor prevent extracting the text.
  unsigned short buffer[128];
  int num_chars = FPDFText_GetText(text_page.get(), 0, 128, buffer);
  ASSERT_EQ(kHelloGoodbyeTextSize, num_chars);
  EXPECT_THAT(pdfium::span(buffer).first<kHelloGoodbyeTextSize>(),
              ElementsAreArray(kHelloGoodbyeText));
}

TEST_F(FPDFTextEmbedderTest, Bug642) {
  ASSERT_TRUE(OpenDocument("bug_642.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage text_page(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(text_page);

  static constexpr char kText[] = "ABCD";
  static constexpr size_t kTextSize = std::size(kText);
  // -1 for CountChars not including the \0
  EXPECT_EQ(static_cast<int>(kTextSize) - 1,
            FPDFText_CountChars(text_page.get()));

  unsigned short buffer[kTextSize];
  int num_chars =
      FPDFText_GetText(text_page.get(), 0, std::size(buffer) - 1, buffer);
  ASSERT_EQ(static_cast<int>(kTextSize), num_chars);
  EXPECT_THAT(buffer, ElementsAreArray(kText));
}

TEST_F(FPDFTextEmbedderTest, GetCharAngle) {
  ASSERT_TRUE(OpenDocument("rotated_text.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage text_page(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(text_page);

  // -1 for CountChars not including the \0, but +1 for the extra control
  // character.
  EXPECT_EQ(kHelloGoodbyeTextSize, FPDFText_CountChars(text_page.get()));

  EXPECT_FLOAT_EQ(-1.0f, FPDFText_GetCharAngle(nullptr, 0));
  EXPECT_FLOAT_EQ(-1.0f, FPDFText_GetCharAngle(text_page.get(), -1));
  EXPECT_FLOAT_EQ(
      -1.0f, FPDFText_GetCharAngle(text_page.get(), kHelloGoodbyeTextSize + 1));

  // Sanity check the characters.
  EXPECT_EQ(static_cast<uint32_t>('H'),
            FPDFText_GetUnicode(text_page.get(),
                                GetRotatedTextFirstCharIndexForQuadrant(0)));
  EXPECT_EQ(static_cast<uint32_t>('w'),
            FPDFText_GetUnicode(text_page.get(),
                                GetRotatedTextFirstCharIndexForQuadrant(1)));
  EXPECT_EQ(static_cast<uint32_t>('G'),
            FPDFText_GetUnicode(text_page.get(),
                                GetRotatedTextFirstCharIndexForQuadrant(2)));
  EXPECT_EQ(static_cast<uint32_t>('w'),
            FPDFText_GetUnicode(text_page.get(),
                                GetRotatedTextFirstCharIndexForQuadrant(3)));

  // Test GetCharAngle for every quadrant.
  EXPECT_NEAR(FXSYS_PI / 4.0,
              FPDFText_GetCharAngle(text_page.get(),
                                    GetRotatedTextFirstCharIndexForQuadrant(0)),
              0.001);
  EXPECT_NEAR(3 * FXSYS_PI / 4.0,
              FPDFText_GetCharAngle(text_page.get(),
                                    GetRotatedTextFirstCharIndexForQuadrant(1)),
              0.001);
  EXPECT_NEAR(5 * FXSYS_PI / 4.0,
              FPDFText_GetCharAngle(text_page.get(),
                                    GetRotatedTextFirstCharIndexForQuadrant(2)),
              0.001);
  EXPECT_NEAR(7 * FXSYS_PI / 4.0,
              FPDFText_GetCharAngle(text_page.get(),
                                    GetRotatedTextFirstCharIndexForQuadrant(3)),
              0.001);
}

TEST_F(FPDFTextEmbedderTest, GetFontWeight) {
  ASSERT_TRUE(OpenDocument("font_weight.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage text_page(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(text_page);

  EXPECT_EQ(2, FPDFText_CountChars(text_page.get()));

  EXPECT_EQ(-1, FPDFText_GetFontWeight(nullptr, 0));
  EXPECT_EQ(-1, FPDFText_GetFontWeight(text_page.get(), -1));
  EXPECT_EQ(-1, FPDFText_GetFontWeight(text_page.get(), 314));

  // The font used for this text only specifies /StemV (80); the weight value
  // that is returned should be calculated from that (80*5 == 400).
  EXPECT_EQ(400, FPDFText_GetFontWeight(text_page.get(), 0));

  // Using a /StemV value of 82, the estimate comes out to 410, even though
  // /FontWeight is 400.
  // TODO(crbug.com/pdfium/1420): Fix this the return value here.
  EXPECT_EQ(410, FPDFText_GetFontWeight(text_page.get(), 1));
}

TEST_F(FPDFTextEmbedderTest, GetTextRenderMode) {
  ASSERT_TRUE(OpenDocument("text_render_mode.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage text_page(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(text_page);

  ASSERT_EQ(12, FPDFText_CountChars(text_page.get()));

  ASSERT_FALSE(FPDFText_GetTextObject(nullptr, 0));
  ASSERT_FALSE(FPDFText_GetTextObject(text_page.get(), -1));
  ASSERT_FALSE(FPDFText_GetTextObject(text_page.get(), 314));

  FPDF_PAGEOBJECT text_object = FPDFText_GetTextObject(text_page.get(), 0);
  ASSERT_TRUE(text_object);
  ASSERT_EQ(FPDF_PAGEOBJ_TEXT, FPDFPageObj_GetType(text_object));
  EXPECT_EQ(FPDF_TEXTRENDERMODE_FILL,
            FPDFTextObj_GetTextRenderMode(text_object));

  text_object = FPDFText_GetTextObject(text_page.get(), 7);
  ASSERT_TRUE(text_object);
  ASSERT_EQ(FPDF_PAGEOBJ_TEXT, FPDFPageObj_GetType(text_object));
  EXPECT_EQ(FPDF_TEXTRENDERMODE_STROKE,
            FPDFTextObj_GetTextRenderMode(text_object));
}

TEST_F(FPDFTextEmbedderTest, GetFillColor) {
  ASSERT_TRUE(OpenDocument("text_color.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage text_page(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(text_page);

  ASSERT_EQ(1, FPDFText_CountChars(text_page.get()));

  ASSERT_FALSE(
      FPDFText_GetFillColor(nullptr, 0, nullptr, nullptr, nullptr, nullptr));
  ASSERT_FALSE(FPDFText_GetFillColor(text_page.get(), -1, nullptr, nullptr,
                                     nullptr, nullptr));
  ASSERT_FALSE(FPDFText_GetFillColor(text_page.get(), 314, nullptr, nullptr,
                                     nullptr, nullptr));
  ASSERT_FALSE(FPDFText_GetFillColor(text_page.get(), 0, nullptr, nullptr,
                                     nullptr, nullptr));

  unsigned int r;
  unsigned int g;
  unsigned int b;
  unsigned int a;
  ASSERT_TRUE(FPDFText_GetFillColor(text_page.get(), 0, &r, &g, &b, &a));
  ASSERT_EQ(0xffu, r);
  ASSERT_EQ(0u, g);
  ASSERT_EQ(0u, b);
  ASSERT_EQ(0xffu, a);
}

TEST_F(FPDFTextEmbedderTest, GetStrokeColor) {
  ASSERT_TRUE(OpenDocument("text_color.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage text_page(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(text_page);

  ASSERT_EQ(1, FPDFText_CountChars(text_page.get()));

  ASSERT_FALSE(
      FPDFText_GetStrokeColor(nullptr, 0, nullptr, nullptr, nullptr, nullptr));
  ASSERT_FALSE(FPDFText_GetStrokeColor(text_page.get(), -1, nullptr, nullptr,
                                       nullptr, nullptr));
  ASSERT_FALSE(FPDFText_GetStrokeColor(text_page.get(), 314, nullptr, nullptr,
                                       nullptr, nullptr));
  ASSERT_FALSE(FPDFText_GetStrokeColor(text_page.get(), 0, nullptr, nullptr,
                                       nullptr, nullptr));

  unsigned int r;
  unsigned int g;
  unsigned int b;
  unsigned int a;
  ASSERT_TRUE(FPDFText_GetStrokeColor(text_page.get(), 0, &r, &g, &b, &a));
  ASSERT_EQ(0u, r);
  ASSERT_EQ(0xffu, g);
  ASSERT_EQ(0u, b);
  ASSERT_EQ(0xffu, a);
}

TEST_F(FPDFTextEmbedderTest, GetMatrix) {
  static constexpr char kExpectedText[] = "A1\r\nA2\r\nA3";
  static constexpr size_t kExpectedTextSize = std::size(kExpectedText);
  static constexpr auto kExpectedMatrices = std::to_array<const FS_MATRIX>({
      {12.0f, 0.0f, 0.0f, 10.0f, 66.0f, 90.0f},
      {12.0f, 0.0f, 0.0f, 10.0f, 66.0f, 90.0f},
      {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
      {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
      {12.0f, 0.0f, 0.0f, 10.0f, 38.0f, 60.0f},
      {12.0f, 0.0f, 0.0f, 10.0f, 38.0f, 60.0f},
      {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
      {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
      {1.0f, 0.0f, 0.0f, 0.833333, 60.0f, 130.0f},
      {1.0f, 0.0f, 0.0f, 0.833333, 60.0f, 130.0f},
  });
  static constexpr size_t kExpectedCount = std::size(kExpectedMatrices);
  static_assert(kExpectedCount + 1 == kExpectedTextSize,
                "Bad expected matrix size");

  ASSERT_TRUE(OpenDocument("font_matrix.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage text_page(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(text_page);
  ASSERT_EQ(static_cast<int>(kExpectedCount),
            FPDFText_CountChars(text_page.get()));

  {
    // Check the characters.
    unsigned short buffer[kExpectedTextSize];
    ASSERT_EQ(static_cast<int>(kExpectedTextSize),
              FPDFText_GetText(text_page.get(), 0, kExpectedCount, buffer));
    EXPECT_THAT(pdfium::span(buffer).first(kExpectedTextSize),
                ElementsAreArray(kExpectedText));
  }

  // Check the character matrix.
  FS_MATRIX matrix;
  for (size_t i = 0; i < kExpectedCount; ++i) {
    ASSERT_TRUE(FPDFText_GetMatrix(text_page.get(), i, &matrix)) << i;
    EXPECT_FLOAT_EQ(kExpectedMatrices[i].a, matrix.a) << i;
    EXPECT_FLOAT_EQ(kExpectedMatrices[i].b, matrix.b) << i;
    EXPECT_FLOAT_EQ(kExpectedMatrices[i].c, matrix.c) << i;
    EXPECT_FLOAT_EQ(kExpectedMatrices[i].d, matrix.d) << i;
    EXPECT_FLOAT_EQ(kExpectedMatrices[i].e, matrix.e) << i;
    EXPECT_FLOAT_EQ(kExpectedMatrices[i].f, matrix.f) << i;
  }

  // Check bad parameters.
  EXPECT_FALSE(FPDFText_GetMatrix(nullptr, 0, &matrix));
  EXPECT_FALSE(FPDFText_GetMatrix(text_page.get(), 10, &matrix));
  EXPECT_FALSE(FPDFText_GetMatrix(text_page.get(), -1, &matrix));
  EXPECT_FALSE(FPDFText_GetMatrix(text_page.get(), 0, nullptr));
}

TEST_F(FPDFTextEmbedderTest, CharBox) {
  // For a size 12 letter 'A'.
  static constexpr double kExpectedCharWidth = 8.460;
  static constexpr double kExpectedCharHeight = 6.600;
  static constexpr float kExpectedLooseCharWidth = 8.664f;
  static constexpr float kExpectedLooseCharHeight = 12.82999f;

  ASSERT_TRUE(OpenDocument("font_matrix.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage text_page(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(text_page);

  // Make sure the tests below are testing the letter 'A'.
  EXPECT_EQ(static_cast<uint32_t>('A'),
            FPDFText_GetUnicode(text_page.get(), 0));
  EXPECT_EQ(static_cast<uint32_t>('A'),
            FPDFText_GetUnicode(text_page.get(), 4));
  EXPECT_EQ(static_cast<uint32_t>('A'),
            FPDFText_GetUnicode(text_page.get(), 8));

  // Check the character box size.
  double left;
  double right;
  double bottom;
  double top;
  ASSERT_TRUE(
      FPDFText_GetCharBox(text_page.get(), 0, &left, &right, &bottom, &top));
  EXPECT_NEAR(kExpectedCharWidth, right - left, 0.001);
  EXPECT_NEAR(kExpectedCharHeight, top - bottom, 0.001);
  ASSERT_TRUE(
      FPDFText_GetCharBox(text_page.get(), 4, &left, &right, &bottom, &top));
  EXPECT_NEAR(kExpectedCharWidth, right - left, 0.001);
  EXPECT_NEAR(kExpectedCharHeight, top - bottom, 0.001);
  ASSERT_TRUE(
      FPDFText_GetCharBox(text_page.get(), 8, &left, &right, &bottom, &top));
  EXPECT_NEAR(kExpectedCharWidth, right - left, 0.001);
  EXPECT_NEAR(kExpectedCharHeight, top - bottom, 0.001);

  // Check the loose character box size.
  FS_RECTF rect;
  ASSERT_TRUE(FPDFText_GetLooseCharBox(text_page.get(), 0, &rect));
  EXPECT_FLOAT_EQ(kExpectedLooseCharWidth, rect.right - rect.left);
  EXPECT_FLOAT_EQ(kExpectedLooseCharHeight, rect.top - rect.bottom);
  ASSERT_TRUE(FPDFText_GetLooseCharBox(text_page.get(), 4, &rect));
  EXPECT_FLOAT_EQ(kExpectedLooseCharWidth, rect.right - rect.left);
  EXPECT_NEAR(kExpectedLooseCharHeight, rect.top - rect.bottom, 0.00001);
  ASSERT_TRUE(FPDFText_GetLooseCharBox(text_page.get(), 8, &rect));
  EXPECT_FLOAT_EQ(kExpectedLooseCharWidth, rect.right - rect.left);
  EXPECT_NEAR(kExpectedLooseCharHeight, rect.top - rect.bottom, 0.00001);
}

TEST_F(FPDFTextEmbedderTest, CharBoxForRotated45DegreesText) {
  ASSERT_TRUE(OpenDocument("rotated_text.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage text_page(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(text_page);

  // Sanity check the characters.
  EXPECT_EQ(static_cast<uint32_t>('H'),
            FPDFText_GetUnicode(text_page.get(),
                                GetRotatedTextFirstCharIndexForQuadrant(0)));
  EXPECT_EQ(static_cast<uint32_t>('w'),
            FPDFText_GetUnicode(text_page.get(),
                                GetRotatedTextFirstCharIndexForQuadrant(1)));
  EXPECT_EQ(static_cast<uint32_t>('G'),
            FPDFText_GetUnicode(text_page.get(),
                                GetRotatedTextFirstCharIndexForQuadrant(2)));
  EXPECT_EQ(static_cast<uint32_t>('w'),
            FPDFText_GetUnicode(text_page.get(),
                                GetRotatedTextFirstCharIndexForQuadrant(3)));

  // Check the character box size.
  double left;
  double right;
  double bottom;
  double top;
  ASSERT_TRUE(FPDFText_GetCharBox(text_page.get(),
                                  GetRotatedTextFirstCharIndexForQuadrant(0),
                                  &left, &right, &bottom, &top));
  EXPECT_NEAR(11.192, right - left, 0.001);
  EXPECT_NEAR(11.192, top - bottom, 0.001);
  ASSERT_TRUE(FPDFText_GetCharBox(text_page.get(),
                                  GetRotatedTextFirstCharIndexForQuadrant(1),
                                  &left, &right, &bottom, &top));
  EXPECT_NEAR(10.055, right - left, 0.001);
  EXPECT_NEAR(10.055, top - bottom, 0.001);
  ASSERT_TRUE(FPDFText_GetCharBox(text_page.get(),
                                  GetRotatedTextFirstCharIndexForQuadrant(2),
                                  &left, &right, &bottom, &top));
  EXPECT_NEAR(11.209, right - left, 0.001);
  EXPECT_NEAR(11.209, top - bottom, 0.001);
  ASSERT_TRUE(FPDFText_GetCharBox(text_page.get(),
                                  GetRotatedTextFirstCharIndexForQuadrant(3),
                                  &left, &right, &bottom, &top));
  EXPECT_NEAR(10.055, right - left, 0.001);
  EXPECT_NEAR(10.055, top - bottom, 0.001);

  // Check the loose character box size.
  static constexpr float kExpectedLooseCharDimension = 17.013f;
  FS_RECTF rect;
  ASSERT_TRUE(FPDFText_GetLooseCharBox(
      text_page.get(), GetRotatedTextFirstCharIndexForQuadrant(0), &rect));
  EXPECT_NEAR(kExpectedLooseCharDimension, rect.right - rect.left, 0.001f);
  EXPECT_NEAR(kExpectedLooseCharDimension, rect.top - rect.bottom, 0.001f);
  ASSERT_TRUE(FPDFText_GetLooseCharBox(
      text_page.get(), GetRotatedTextFirstCharIndexForQuadrant(1), &rect));
  EXPECT_NEAR(kExpectedLooseCharDimension, rect.right - rect.left, 0.001f);
  EXPECT_NEAR(kExpectedLooseCharDimension, rect.top - rect.bottom, 0.001f);
  ASSERT_TRUE(FPDFText_GetLooseCharBox(
      text_page.get(), GetRotatedTextFirstCharIndexForQuadrant(2), &rect));
  EXPECT_NEAR(kExpectedLooseCharDimension, rect.right - rect.left, 0.001f);
  EXPECT_NEAR(kExpectedLooseCharDimension, rect.top - rect.bottom, 0.001f);
  ASSERT_TRUE(FPDFText_GetLooseCharBox(
      text_page.get(), GetRotatedTextFirstCharIndexForQuadrant(3), &rect));
  EXPECT_NEAR(kExpectedLooseCharDimension, rect.right - rect.left, 0.001f);
  EXPECT_NEAR(kExpectedLooseCharDimension, rect.top - rect.bottom, 0.001f);
}

TEST_F(FPDFTextEmbedderTest, CharBoxForRotated90DegreesText) {
  ASSERT_TRUE(OpenDocument("rotated_text_90.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage text_page(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(text_page);

  // Sanity check the characters.
  EXPECT_EQ(static_cast<uint32_t>('H'),
            FPDFText_GetUnicode(text_page.get(),
                                GetRotatedText90FirstCharIndexForQuadrant(0)));
  EXPECT_EQ(static_cast<uint32_t>('w'),
            FPDFText_GetUnicode(text_page.get(),
                                GetRotatedText90FirstCharIndexForQuadrant(1)));
  EXPECT_EQ(static_cast<uint32_t>('G'),
            FPDFText_GetUnicode(text_page.get(),
                                GetRotatedText90FirstCharIndexForQuadrant(2)));
  EXPECT_EQ(static_cast<uint32_t>('w'),
            FPDFText_GetUnicode(text_page.get(),
                                GetRotatedText90FirstCharIndexForQuadrant(3)));

  // Check the character box size.
  double left;
  double right;
  double bottom;
  double top;
  ASSERT_TRUE(FPDFText_GetCharBox(text_page.get(),
                                  GetRotatedText90FirstCharIndexForQuadrant(0),
                                  &left, &right, &bottom, &top));
  EXPECT_NEAR(7.968, right - left, 0.001);
  EXPECT_NEAR(7.86, top - bottom, 0.001);
  ASSERT_TRUE(FPDFText_GetCharBox(text_page.get(),
                                  GetRotatedText90FirstCharIndexForQuadrant(1),
                                  &left, &right, &bottom, &top));
  EXPECT_NEAR(5.616, right - left, 0.001);
  EXPECT_NEAR(8.604, top - bottom, 0.001);
  ASSERT_TRUE(FPDFText_GetCharBox(text_page.get(),
                                  GetRotatedText90FirstCharIndexForQuadrant(2),
                                  &left, &right, &bottom, &top));
  EXPECT_NEAR(7.8, right - left, 0.001);
  EXPECT_NEAR(8.052, top - bottom, 0.001);
  ASSERT_TRUE(FPDFText_GetCharBox(text_page.get(),
                                  GetRotatedText90FirstCharIndexForQuadrant(3),
                                  &left, &right, &bottom, &top));
  EXPECT_NEAR(5.616, right - left, 0.001);
  EXPECT_NEAR(8.604, top - bottom, 0.001);

  // Check the loose character box size.
  static constexpr float kExpectedLooseCharWidth = 8.664f;
  static constexpr float kExpectedLooseCharHeight = 15.396f;
  FS_RECTF rect;
  ASSERT_TRUE(FPDFText_GetLooseCharBox(
      text_page.get(), GetRotatedText90FirstCharIndexForQuadrant(0), &rect));
  EXPECT_NEAR(kExpectedLooseCharWidth, rect.right - rect.left, 0.001f);
  EXPECT_NEAR(kExpectedLooseCharHeight, rect.top - rect.bottom, 0.001f);
  ASSERT_TRUE(FPDFText_GetLooseCharBox(
      text_page.get(), GetRotatedText90FirstCharIndexForQuadrant(1), &rect));
  EXPECT_NEAR(kExpectedLooseCharHeight, rect.right - rect.left, 0.001f);
  EXPECT_NEAR(kExpectedLooseCharWidth, rect.top - rect.bottom, 0.001f);
  ASSERT_TRUE(FPDFText_GetLooseCharBox(
      text_page.get(), GetRotatedText90FirstCharIndexForQuadrant(2), &rect));
  EXPECT_NEAR(kExpectedLooseCharWidth, rect.right - rect.left, 0.001f);
  EXPECT_NEAR(kExpectedLooseCharHeight, rect.top - rect.bottom, 0.001f);
  ASSERT_TRUE(FPDFText_GetLooseCharBox(
      text_page.get(), GetRotatedText90FirstCharIndexForQuadrant(3), &rect));
  EXPECT_NEAR(kExpectedLooseCharHeight, rect.right - rect.left, 0.001f);
  EXPECT_NEAR(kExpectedLooseCharWidth, rect.top - rect.bottom, 0.001f);
}

TEST_F(FPDFTextEmbedderTest, CharBoxForLatinExtendedText) {
  ASSERT_TRUE(OpenDocument("latin_extended.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage text_page(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(text_page);

  EXPECT_EQ(u'Ā', FPDFText_GetUnicode(text_page.get(), 0));

  double left;
  double right;
  double bottom;
  double top;
  ASSERT_TRUE(
      FPDFText_GetCharBox(text_page.get(), 0, &left, &right, &bottom, &top));
  EXPECT_NEAR(7.512, right - left, 0.001);
  EXPECT_NEAR(10.488, top - bottom, 0.001);
  EXPECT_NEAR(750.238, top, 0.001);

  FS_RECTF rect;
  ASSERT_TRUE(FPDFText_GetLooseCharBox(text_page.get(), 0, &rect));
  EXPECT_NEAR(7.824f, rect.right - rect.left, 0.001f);
  EXPECT_NEAR(15.912f, rect.top - rect.bottom, 0.001f);
  EXPECT_NEAR(752.422f, rect.top, 0.001f);

  EXPECT_EQ(u'Ă', FPDFText_GetUnicode(text_page.get(), 2));

  ASSERT_TRUE(
      FPDFText_GetCharBox(text_page.get(), 2, &left, &right, &bottom, &top));
  EXPECT_NEAR(7.512, right - left, 0.001);
  EXPECT_NEAR(10.74, top - bottom, 0.001);
  EXPECT_NEAR(750.49, top, 0.001);

  ASSERT_TRUE(FPDFText_GetLooseCharBox(text_page.get(), 2, &rect));
  EXPECT_NEAR(7.824f, rect.right - rect.left, 0.001f);
  EXPECT_NEAR(15.912f, rect.top - rect.bottom, 0.001f);
  EXPECT_NEAR(752.422f, rect.top, 0.001f);
}

TEST_F(FPDFTextEmbedderTest, Bug399689604) {
  ASSERT_TRUE(OpenDocument("bug_399689604.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage text_page(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(text_page);

  EXPECT_EQ(1, FPDFText_IsGenerated(text_page.get(), 5));
  double left;
  double right;
  double bottom;
  double top;
  ASSERT_TRUE(
      FPDFText_GetCharBox(text_page.get(), 5, &left, &right, &bottom, &top));
  EXPECT_DOUBLE_EQ(0.0, right - left);
  EXPECT_DOUBLE_EQ(0.0, top - bottom);
  EXPECT_NEAR(100.0, top, 0.001);

  FS_RECTF rect;
  ASSERT_TRUE(FPDFText_GetLooseCharBox(text_page.get(), 5, &rect));
  EXPECT_NEAR(0.0f, rect.right - rect.left, 0.001f);
  EXPECT_NEAR(0.0f, rect.top - rect.bottom, 0.001f);
  EXPECT_NEAR(100.0f, rect.top, 0.001f);
}

TEST_F(FPDFTextEmbedderTest, SmallType3Glyph) {
  ASSERT_TRUE(OpenDocument("bug_1591.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage text_page(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(text_page);
  ASSERT_EQ(5, FPDFText_CountChars(text_page.get()));

  EXPECT_EQ(49u, FPDFText_GetUnicode(text_page.get(), 0));
  EXPECT_EQ(32u, FPDFText_GetUnicode(text_page.get(), 1));
  EXPECT_EQ(50u, FPDFText_GetUnicode(text_page.get(), 2));
  EXPECT_EQ(32u, FPDFText_GetUnicode(text_page.get(), 3));
  EXPECT_EQ(49u, FPDFText_GetUnicode(text_page.get(), 4));

  // Check the character box size.
  double left;
  double right;
  double bottom;
  double top;
  ASSERT_TRUE(
      FPDFText_GetCharBox(text_page.get(), 0, &left, &right, &bottom, &top));
  EXPECT_DOUBLE_EQ(63.439998626708984, left);
  EXPECT_DOUBLE_EQ(65.360000610351562, right);
  EXPECT_DOUBLE_EQ(50.0, bottom);
  EXPECT_DOUBLE_EQ(61.520000457763672, top);
  ASSERT_TRUE(
      FPDFText_GetCharBox(text_page.get(), 1, &left, &right, &bottom, &top));
  EXPECT_DOUBLE_EQ(62.007999420166016, left);
  EXPECT_DOUBLE_EQ(62.007999420166016, right);
  EXPECT_DOUBLE_EQ(50.0, bottom);
  EXPECT_DOUBLE_EQ(50.0, top);
  ASSERT_TRUE(
      FPDFText_GetCharBox(text_page.get(), 2, &left, &right, &bottom, &top));
  EXPECT_DOUBLE_EQ(86.0, left);
  EXPECT_DOUBLE_EQ(88.400001525878906, right);
  EXPECT_DOUBLE_EQ(50.0, bottom);
  EXPECT_DOUBLE_EQ(50.240001678466797, top);
  ASSERT_TRUE(
      FPDFText_GetCharBox(text_page.get(), 3, &left, &right, &bottom, &top));
  EXPECT_DOUBLE_EQ(86.010002136230469, left);
  EXPECT_DOUBLE_EQ(86.010002136230469, right);
  EXPECT_DOUBLE_EQ(50.0, bottom);
  EXPECT_DOUBLE_EQ(50.0, top);
  ASSERT_TRUE(
      FPDFText_GetCharBox(text_page.get(), 4, &left, &right, &bottom, &top));
  EXPECT_DOUBLE_EQ(99.44000244140625, left);
  EXPECT_DOUBLE_EQ(101.36000061035156, right);
  EXPECT_DOUBLE_EQ(50.0, bottom);
  EXPECT_DOUBLE_EQ(61.520000457763672, top);
}

TEST_F(FPDFTextEmbedderTest, BigtableTextExtraction) {
  static constexpr char kExpectedText[] =
      "{fay,jeff,sanjay,wilsonh,kerr,m3b,tushar,\x02k es,gruber}@google.com";
  ByteStringView expected_text(kExpectedText);

  ASSERT_TRUE(OpenDocument("bigtable_mini.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage text_page(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(text_page);
  int char_count = FPDFText_CountChars(text_page.get());
  ASSERT_EQ(static_cast<int>(expected_text.GetLength()), char_count);

  for (size_t i = 0; i < expected_text.GetLength(); ++i) {
    EXPECT_EQ(static_cast<uint32_t>(expected_text[i]),
              FPDFText_GetUnicode(text_page.get(), i));
  }
}

TEST_F(FPDFTextEmbedderTest, BigtableTextRects) {
  struct TextRect {
    double left;
    double top;
    double right;
    double bottom;
  };
  // TODO(crbug.com/40448046): The PDF uses fonts [/F2, /F1, /F2, /F1] with a
  // constant size on a single line. FPDFText_CountRects() should merge the text
  // into 4 rects.
  static constexpr auto kExpectedRects = std::to_array<TextRect>({
      {7.0195, 657.8847, 10.3102, 648.9273},
      {11.1978, 657.4722, 13.9057, 651.1599},
      {14.1085, 655.3652, 22.2230, 649.2321},
      {21.9279, 657.4722, 33.2883, 649.2590},
      {33.3711, 657.4722, 61.1938, 649.2321},
      {60.8897, 657.3826, 97.9119, 649.7881},
      {98.0787, 655.3831, 107.6010, 651.0792},
      {107.6535, 657.3826, 149.5713, 649.7881},
      {149.5072, 657.3826, 158.1329, 649.7881},
      {161.1511, 657.3826, 193.8335, 649.2321},
      {194.4253, 657.8847, 197.7160, 648.9273},
      {198.8009, 657.3826, 248.5284, 649.2321},
  });

  ASSERT_TRUE(OpenDocument("bigtable_mini.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  ASSERT_EQ(65, FPDFText_CountChars(textpage.get()));
  ASSERT_EQ(12, FPDFText_CountRects(textpage.get(), 0, 65));
  for (size_t i = 0; i < kExpectedRects.size(); ++i) {
    TextRect result;
    ASSERT_TRUE(FPDFText_GetRect(textpage.get(), i, &result.left, &result.top,
                                 &result.right, &result.bottom));
    EXPECT_NEAR(kExpectedRects[i].left, result.left, 0.001);
    EXPECT_NEAR(kExpectedRects[i].top, result.top, 0.001);
    EXPECT_NEAR(kExpectedRects[i].right, result.right, 0.001);
    EXPECT_NEAR(kExpectedRects[i].bottom, result.bottom, 0.001);
  }
}

TEST_F(FPDFTextEmbedderTest, Bug1769) {
  ASSERT_TRUE(OpenDocument("bug_1769.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  unsigned short buffer[128] = {};
  // TODO(crbug.com/pdfium/1769): Improve text extraction.
  // The first instance of "world" is visible to the human eye and should be
  // extracted as is. The second instance is not, so how it should be
  // extracted is debatable.
  static constexpr char kNeedsImprovementResult[] = "wo d wo d";
  ASSERT_EQ(10, FPDFText_GetText(textpage.get(), 0, 128, buffer));
  EXPECT_THAT(pdfium::span(buffer).first(10u),
              ElementsAreArray(kNeedsImprovementResult));
}

TEST_F(FPDFTextEmbedderTest, Bug384770169) {
  ASSERT_TRUE(OpenDocument("bug_384770169.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  static constexpr char kExpected[] = "What is my favorite food?";
  // Includes trailing NUL character.
  static constexpr int kExpectedSize = sizeof(kExpected);
  unsigned short buffer[256] = {};
  EXPECT_EQ(kExpectedSize,
            FPDFText_GetText(textpage.get(), 0, std::size(buffer), buffer));
  EXPECT_THAT(pdfium::span(buffer).first<kExpectedSize>(),
              ElementsAreArray(kExpected));
}

TEST_F(FPDFTextEmbedderTest, Bug420508260) {
  ASSERT_TRUE(OpenDocument("bug_420508260.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  static constexpr wchar_t kExpected[] = L"What is 我的 favorite 食物?";
  // Includes trailing NUL character.
  static constexpr int kExpectedSize = std::size(kExpected);
  unsigned short buffer[256] = {};
  EXPECT_EQ(kExpectedSize,
            FPDFText_GetText(textpage.get(), 0, std::size(buffer), buffer));
  EXPECT_THAT(pdfium::span(buffer).first<kExpectedSize>(),
              ElementsAreArray(kExpected));
}

TEST_F(FPDFTextEmbedderTest, TextObjectSetIsActive) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  {
    // First, sanity check hello_world.pdf.
    ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
    ASSERT_TRUE(textpage);

    unsigned short buffer[128];
    int num_chars =
        FPDFText_GetText(textpage.get(), 0, std::size(buffer), buffer);
    ASSERT_EQ(kHelloGoodbyeTextSize, num_chars);
    EXPECT_THAT(pdfium::span(buffer).first<kHelloGoodbyeTextSize>(),
                ElementsAreArray(kHelloGoodbyeText));
  }

  FPDF_PAGEOBJECT text_obj = FPDFPage_GetObject(page.get(), 0);
  ASSERT_TRUE(text_obj);
  ASSERT_EQ(FPDF_PAGEOBJ_TEXT, FPDFPageObj_GetType(text_obj));

  {
    // Deactivate `text_obj` and check `textpage` again.
    ASSERT_TRUE(FPDFPageObj_SetIsActive(text_obj, false));

    ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
    ASSERT_TRUE(textpage);

    static constexpr int kGoodbyeTextSize = 16;
    static constexpr int kOffset = kHelloGoodbyeTextSize - kGoodbyeTextSize;
    unsigned short buffer[128];
    int num_chars =
        FPDFText_GetText(textpage.get(), 0, std::size(buffer), buffer);
    ASSERT_EQ(kGoodbyeTextSize, num_chars);
    EXPECT_THAT(
        pdfium::span(buffer).first<kGoodbyeTextSize>(),
        ElementsAreArray(pdfium::span(kHelloGoodbyeText).subspan<kOffset>()));
  }

  {
    // Reactivate `text_obj` and check `textpage` again.
    ASSERT_TRUE(FPDFPageObj_SetIsActive(text_obj, true));

    ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
    ASSERT_TRUE(textpage);

    unsigned short buffer[128];
    int num_chars =
        FPDFText_GetText(textpage.get(), 0, std::size(buffer), buffer);
    ASSERT_EQ(kHelloGoodbyeTextSize, num_chars);
    EXPECT_THAT(pdfium::span(buffer).first<kHelloGoodbyeTextSize>(),
                ElementsAreArray(kHelloGoodbyeText));
  }
}

TEST_F(FPDFTextEmbedderTest, Bug425244539) {
  static constexpr std::array<unsigned short, 6> kExpectedChars = {
      'h', 'e', 'l', 'l', 'o', 0};

  ASSERT_TRUE(OpenDocument("bug_425244539.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  std::array<unsigned short, 128> buffer = {};
  int num_chars =
      FPDFText_GetText(textpage.get(), 0, buffer.size(), buffer.data());
  ASSERT_EQ(static_cast<int>(kExpectedChars.size()), num_chars);
  EXPECT_THAT(pdfium::span(buffer).first<kExpectedChars.size()>(),
              ElementsAreArray(kExpectedChars));

  ScopedFPDFWideString hello = GetFPDFWideString(L"hello");

  ScopedFPDFTextFind search(
      FPDFText_FindStart(textpage.get(), hello.get(), 0, 0));
  EXPECT_TRUE(search);
  EXPECT_EQ(22, FPDFText_GetSchResultIndex(search.get()));
  EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

  EXPECT_TRUE(FPDFText_FindNext(search.get()));
  EXPECT_EQ(22, FPDFText_GetSchResultIndex(search.get()));
  EXPECT_EQ(5, FPDFText_GetSchCount(search.get()));
}

TEST_F(FPDFTextEmbedderTest, Bug431824298) {
  // TODO(crbug.com/431824298): 0xfffe should be a dash.
  static constexpr std::array<unsigned short, 19> kExpectedChars = {
      '-', 'h', 'e', 'l', 'l', 'o',    '-',    '\r',   '\n', '-',
      'w', 'o', 'r', 'l', 'd', 0xfffe, 0x501f, 0x6b3e, 0};

  ASSERT_TRUE(OpenDocument("bug_431824298.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFTextPage textpage(FPDFText_LoadPage(page.get()));
  ASSERT_TRUE(textpage);

  std::array<unsigned short, 128> buffer = {};
  int num_chars =
      FPDFText_GetText(textpage.get(), 0, buffer.size(), buffer.data());
  ASSERT_EQ(static_cast<int>(kExpectedChars.size()), num_chars);
  EXPECT_THAT(pdfium::span(buffer).first<kExpectedChars.size()>(),
              ElementsAreArray(kExpectedChars));

  ScopedFPDFWideString world = GetFPDFWideString(L"-world-");

  ScopedFPDFTextFind search(
      FPDFText_FindStart(textpage.get(), world.get(), 0, 0));
  EXPECT_TRUE(search);
  EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
  EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

  // TODO(crbug.com/431824298): Once 0xfffe in `kExpectedChars` is a dash, this
  // search should succeed.
  EXPECT_FALSE(FPDFText_FindNext(search.get()));
  EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
  EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));
}

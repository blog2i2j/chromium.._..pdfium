// Copyright 2017 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>

#include "core/fxcodec/basic/basicmodule.h"
#include "core/fxcrt/compiler_specific.h"
#include "core/fxcrt/span.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // SAFETY: trusted arguments passed from fuzzer.
  BasicModule::RunLengthEncode(UNSAFE_BUFFERS(pdfium::span(data, size)));
  return 0;
}

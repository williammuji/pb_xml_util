// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
// https://developers.google.com/protocol-buffers/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <google/protobuf/stubs/casts.h>
#include <google/protobuf/stubs/common.h>
#include <google/protobuf/stubs/logging.h>
#include <google/protobuf/stubs/strutil.h>
#include <google/protobuf/util/internal/json_escaping.h>
#include <google/protobuf/util/internal/utility.h>
#include <google/protobuf/util/internal/xml_objectwriter.h>

#include <cmath>
#include <cstdint>
#include <limits>

namespace google {
namespace protobuf {
namespace util {
namespace converter {

XmlObjectWriter::~XmlObjectWriter() {
  if (element_ && !element_->is_root()) {
    GOOGLE_LOG(WARNING) << "XmlObjectWriter was not fully closed.";
  }
}

XmlObjectWriter* XmlObjectWriter::StartObject(StringPiece name) {
  if (element_) {
    element_->clear_has_child();
    element_->clear_has_text();
    element_->clear_has_attribute();
  }

  start_element_ = true;
  std::string tag_name(name.data(), name.size());
  if (name.empty()) {
    if (element_->is_root()) {
      tag_name = "root";
    } else if (element_->is_xml_list()) {
      tag_name.assign(element_->name().data(), element_->name().size());
    }
  }
  WritePrefix(tag_name, false /*render*/);
  WriteChar('<');
  WriteRawString(tag_name);
  tag_needs_closed_ = true;

  PushObject(tag_name);
  return this;
}

XmlObjectWriter* XmlObjectWriter::EndObject() {
  start_element_ = false;
  std::string tag_name(element_->name().data(), element_->name().size());
  WriteCloseTag();
  if (!tag_name.empty()) {
    WriteRawString("</");
    WriteRawString(tag_name);
    WriteChar('>');
  }
  Pop();
  WriteSuffix();
  if (element() && element()->is_root()) {
    NewLine();
  }
  return this;
}

XmlObjectWriter* XmlObjectWriter::StartList(StringPiece name) {
  start_element_ = true;
  WritePrefix(name, false /*render*/);
  WriteRawString("<_list_");
  WriteRawString(name);
  WriteChar('>');
  PushArray(name);
  return this;
}

XmlObjectWriter* XmlObjectWriter::EndList() {
  start_element_ = false;
  WriteCloseTag();
  std::string tag_name(element_->name().data(), element_->name().size());
  WriteRawString("</_list_");
  WriteRawString(tag_name);
  WriteRawString(">");
  Pop();
  WriteSuffix();
  if (element()->is_root()) {
    NewLine();
  }
  return this;
}

XmlObjectWriter* XmlObjectWriter::RenderBool(StringPiece name, bool value) {
  return RenderSimple(name, value ? "true" : "false");
}

XmlObjectWriter* XmlObjectWriter::RenderInt32(StringPiece name, int32_t value) {
  return RenderSimple(name, StrCat(value));
}

XmlObjectWriter* XmlObjectWriter::RenderUint32(StringPiece name,
                                               uint32_t value) {
  return RenderSimple(name, StrCat(value));
}

XmlObjectWriter* XmlObjectWriter::RenderInt64(StringPiece name, int64_t value) {
  WritePrefix(name);
  if (!name.empty()) WriteChar('"');
  WriteRawString(StrCat(value));
  if (!name.empty()) WriteChar('"');
  SetHasTextOrAttribute(name);
  WriteSuffix();
  return this;
}

XmlObjectWriter* XmlObjectWriter::RenderUint64(StringPiece name,
                                               uint64_t value) {
  WritePrefix(name);
  WriteChar('"');
  WriteRawString(StrCat(value));
  WriteChar('"');
  SetHasTextOrAttribute(name);
  WriteSuffix();
  return this;
}

XmlObjectWriter* XmlObjectWriter::RenderDouble(StringPiece name, double value) {
  if (std::isfinite(value)) {
    return RenderSimple(name, SimpleDtoa(value));
  }

  // Render quoted with NaN/Infinity-aware DoubleAsString.
  return RenderString(name, DoubleAsString(value));
}

XmlObjectWriter* XmlObjectWriter::RenderFloat(StringPiece name, float value) {
  if (std::isfinite(value)) {
    return RenderSimple(name, SimpleFtoa(value));
  }

  // Render quoted with NaN/Infinity-aware FloatAsString.
  return RenderString(name, FloatAsString(value));
}

XmlObjectWriter* XmlObjectWriter::RenderString(StringPiece name,
                                               StringPiece value) {
  WritePrefix(name);
  if (!name.empty()) WriteChar('"');
  JsonEscaping::Escape(value, &sink_);
  if (!name.empty()) WriteChar('"');
  SetHasTextOrAttribute(name);
  WriteSuffix();
  return this;
}

XmlObjectWriter* XmlObjectWriter::RenderBytes(StringPiece name,
                                              StringPiece value) {
  WritePrefix(name);
  std::string base64;

  if (use_websafe_base64_for_bytes_)
    WebSafeBase64EscapeWithPadding(std::string(value), &base64);
  else
    Base64Escape(value, &base64);

  if (!name.empty()) WriteChar('"');
  // TODO(wpoon): Consider a ByteSink solution that writes the base64 bytes
  //              directly to the stream, rather than first putting them
  //              into a string and then writing them to the stream.
  stream_->WriteRaw(base64.data(), base64.size());
  if (!name.empty()) WriteChar('"');
  SetHasTextOrAttribute(name);
  WriteSuffix();
  return this;
}

XmlObjectWriter* XmlObjectWriter::RenderNull(StringPiece name) {
  return RenderSimple(name, "null");
}

XmlObjectWriter* XmlObjectWriter::RenderComments(StringPiece comments) {
  WriteRawString("<!--");
  WriteRawString(comments);
  WriteRawString("-->");
  return this;
}

XmlObjectWriter* XmlObjectWriter::RenderNullAsEmpty(StringPiece name) {
  return RenderSimple(name, "");
}

void XmlObjectWriter::WriteCloseTag() {
  if (tag_needs_closed_) {
    WriteChar('>');
    tag_needs_closed_ = false;
  }
  if (!element()->is_root()) {
    if (start_element_) {
      NewLine();
      start_element_ = false;
    } else {
      if (element()->has_child() && !element()->anonymous()) {
        NewLine(true /*pop*/);
      }
    }
  }
}

void XmlObjectWriter::SetHasTextOrAttribute(StringPiece name) {
  name.empty() ? element_->set_has_text() : element_->set_has_attribute();
}

void XmlObjectWriter::WritePrefix(StringPiece name, bool render) {
  if (tag_needs_closed_ && !render) {
    WriteChar('>');
    tag_needs_closed_ = false;
  }

  if (!render) {
    if (!element()->is_root()) {
      if (start_element_) {
        NewLine();
        start_element_ = false;
      } else {
        if (element()->has_child()) {
          NewLine(true /*pop*/);
        }
      }
    }
  }

  if (render && element() && element()->is_xml_list()) {
    NewLine();
    WriteChar('<');
    WriteRawString("anonymous");
    element()->set_anonymous();
    element()->set_has_child();
    element()->set_list_child_needs_end_tag(true);
    tag_needs_closed_ = true;
  }

  if (render) {
    if (!name.empty()) {
      WriteChar(' ');
      JsonEscaping::Escape(name, &sink_);
      WriteChar('=');
    } else {
      WriteChar('>');
      tag_needs_closed_ = false;
    }
  } else {
    if (tag_needs_closed_) {
      WriteChar('>');
      tag_needs_closed_ = false;
    }
  }
}

void XmlObjectWriter::WriteSuffix() {
  if (element() && element()->is_xml_list()) {
    if (element()->list_child_needs_end_tag()) {
      WriteCloseTag();
      WriteRawString("</");
      if (element()->anonymous()) {
        WriteRawString("anonymous");
        element()->clear_anonymous();
      } else {
        WriteRawString(element()->name());
      }
      WriteChar('>');
      element()->set_list_child_needs_end_tag(false);
    }
  }
}

}  // namespace converter
}  // namespace util
}  // namespace protobuf
}  // namespace google

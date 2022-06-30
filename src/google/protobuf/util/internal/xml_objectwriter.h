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

#ifndef GOOGLE_PROTOBUF_UTIL_INTERNAL_XML_OBJECTWRITER_H__
#define GOOGLE_PROTOBUF_UTIL_INTERNAL_XML_OBJECTWRITER_H__

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/stubs/bytestream.h>
#include <google/protobuf/util/internal/structured_objectwriter.h>

#include <cstdint>
#include <memory>
#include <string>

// clang-format off
#include <google/protobuf/port_def.inc>
// clang-format on

namespace google {
namespace protobuf {
namespace util {
namespace converter {

// An ObjectWriter implementation that outputs XML. This ObjectWriter
// supports writing a compact form or a pretty printed form.
//
// Sample usage:
//   string output;
//   StringOutputStream* str_stream = new StringOutputStream(&output);
//   CodedOutputStream* out_stream = new CodedOutputStream(str_stream);
//   XmlObjectWriter* ow = new XmlObjectWriter("  ", out_stream);
//   ow->StartObject("root")
//       ->RenderString("name", "value")
//       ->RenderString("emptystring", string())
//       ->StartObject("nested")
//         ->RenderString("light", 299792458);
//         ->RenderString("pi", 3.141592653589793);
//       ->EndObject()
//       ->StartObject("empty")
//       ->EndObject()
//       ->StartObject("text")
//         ->RenderString("", "abc");
//       ->EndObject()
//     ->EndObject();
//
// And then the output string would become:
// <root name="value" emptystring="">
//   <nested light="299792458" pi="3.141592653589793"></nested>
//   <empty></empty>
//   <text>abc</text>
//</root>
//
// XmlObjectWriter does not validate if calls actually result in valid XML.
// For example, passing an empty name when one would be required won't result
// in an error, just an invalid output.
//
// Note that all int64 and uint64 are rendered as strings instead of numbers.
// This is because JavaScript parses numbers as 64-bit float thus int64 and
// uint64 would lose precision if rendered as numbers.
//
// XmlObjectWriter is thread-unsafe.
class PROTOBUF_EXPORT XmlObjectWriter : public StructuredObjectWriter {
 public:
  XmlObjectWriter(StringPiece indent_string, io::CodedOutputStream* out)
      : element_(new Element(/*parent=*/nullptr, /*is_xml_object=*/false,
                             /*is_xml_list=*/false, "")),
        stream_(out),
        sink_(out),
        indent_string_(indent_string),
        indent_char_('\0'),
        indent_count_(0),
        use_websafe_base64_for_bytes_(false),
        tag_needs_closed_(false),
        start_element_(false) {
    // See if we have a trivial sequence of indent characters.
    if (!indent_string.empty()) {
      indent_char_ = indent_string[0];
      indent_count_ = indent_string.length();
      for (int i = 1; i < indent_string.length(); i++) {
        if (indent_char_ != indent_string_[i]) {
          indent_char_ = '\0';
          indent_count_ = 0;
          break;
        }
      }
    }
  }
  ~XmlObjectWriter() override;

  // ObjectWriter methods.
  XmlObjectWriter* StartObject(StringPiece name) override;
  XmlObjectWriter* EndObject() override;
  XmlObjectWriter* StartList(StringPiece name) override;
  XmlObjectWriter* EndList() override;
  XmlObjectWriter* RenderBool(StringPiece name, bool value) override;
  XmlObjectWriter* RenderInt32(StringPiece name, int32_t value) override;
  XmlObjectWriter* RenderUint32(StringPiece name, uint32_t value) override;
  XmlObjectWriter* RenderInt64(StringPiece name, int64_t value) override;
  XmlObjectWriter* RenderUint64(StringPiece name, uint64_t value) override;
  XmlObjectWriter* RenderDouble(StringPiece name, double value) override;
  XmlObjectWriter* RenderFloat(StringPiece name, float value) override;
  XmlObjectWriter* RenderString(StringPiece name, StringPiece value) override;
  XmlObjectWriter* RenderBytes(StringPiece name, StringPiece value) override;
  XmlObjectWriter* RenderNull(StringPiece name) override;
  virtual XmlObjectWriter* RenderNullAsEmpty(StringPiece name);

  XmlObjectWriter* RenderComments(StringPiece comments);

  void set_use_websafe_base64_for_bytes(bool value) {
    use_websafe_base64_for_bytes_ = value;
  }

 protected:
  class PROTOBUF_EXPORT Element : public BaseElement {
   public:
    Element(Element* parent, bool is_xml_object, bool is_xml_list,
            StringPiece name)
        : BaseElement(parent),
          name_(name),
          is_first_(true),
          is_xml_object_(is_xml_object),
          is_xml_list_(is_xml_list),
          has_child_(false),
          has_text_(false),
          has_attribute_(false),
          list_child_needs_end_tag_(false),
          anonymous_(false) {
      if (parent != nullptr) {
        parent->set_has_child();
      }
    }

    // Called before each field of the Element is to be processed.
    // Returns true if this is the first call (processing the first field).
    bool is_first() {
      if (is_first_) {
        is_first_ = false;
        return true;
      }
      return false;
    }

    // Whether we are currently rendering inside a XML object (i.e., between
    // StartObject() and EndObject()).
    bool is_xml_object() const { return is_xml_object_; }
    bool is_xml_list() const { return is_xml_list_; }

    void set_has_child() { has_child_ = true; }
    void clear_has_child() { has_child_ = false; }
    bool has_child() const { return has_child_; }
    void set_has_text() { has_text_ = true; }
    void clear_has_text() { has_text_ = false; }
    bool has_text() const { return has_text_; }
    void set_has_attribute() { has_attribute_ = true; }
    void clear_has_attribute() { has_attribute_ = false; }
    bool has_attribute() const { return has_attribute_; }
    bool is_empty() const {
      return !has_child() && !has_text() && !has_attribute();
    }
    bool list_child_needs_end_tag() const { return list_child_needs_end_tag_; }
    void set_list_child_needs_end_tag(bool need) {
      list_child_needs_end_tag_ = need;
    }

    bool anonymous() const { return anonymous_; }
    void set_anonymous() { anonymous_ = true; }
    void clear_anonymous() { anonymous_ = false; }

    StringPiece name() const { return name_; }
    void set_name(StringPiece name) { name_.assign(name.data(), name.size()); }

   private:
    std::string name_;
    bool is_first_;
    bool is_xml_object_;
    bool is_xml_list_;
    bool has_child_;
    bool has_text_;
    bool has_attribute_;
    bool list_child_needs_end_tag_;
    bool anonymous_;

    GOOGLE_DISALLOW_IMPLICIT_CONSTRUCTORS(Element);
  };

  Element* element() override { return element_.get(); }

 private:
  class PROTOBUF_EXPORT ByteSinkWrapper : public strings::ByteSink {
   public:
    explicit ByteSinkWrapper(io::CodedOutputStream* stream) : stream_(stream) {}
    ~ByteSinkWrapper() override {}

    // ByteSink methods.
    void Append(const char* bytes, size_t n) override {
      stream_->WriteRaw(bytes, n);
    }

   private:
    io::CodedOutputStream* stream_;

    GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(ByteSinkWrapper);
  };

  // Renders a simple value as a string. By default all non-string Render
  // methods convert their argument to a string and call this method. This
  // method can then be used to render the simple value without escaping it.
  XmlObjectWriter* RenderSimple(StringPiece name, StringPiece value) {
    WritePrefix(name);
    if (!name.empty()) WriteChar('"');
    WriteRawString(value);
    if (!name.empty()) WriteChar('"');
    SetHasTextOrAttribute(name);
    WriteSuffix();
    return this;
  }

  // Pushes a new XML array element to the stack.
  void PushArray(StringPiece name) {
    element_.reset(new Element(element_.release(), /*is_xml_object=*/false,
                               /*is_xml_list=*/true, name));
  }

  // Pushes a new XML object element to the stack.
  void PushObject(StringPiece name) {
    element_.reset(new Element(element_.release(), /*is_xml_object=*/true,
                               /*is_xml_list=*/false, name));
  }

  // Pops an element off of the stack and deletes the popped element.
  void Pop() { element_.reset(element_->pop<Element>()); }

  // If pretty printing is enabled, this will write a newline to the output,
  // followed by optional indentation. Otherwise this method is a noop.
  void NewLine(bool pop = false) {
    int level = pop ? element()->level() - 1 : element()->level();
    if (!indent_string_.empty()) {
      size_t len = sizeof('\n') + (indent_string_.size() * level);

      // Take the slow-path if we don't have sufficient characters remaining in
      // our buffer or we have a non-trivial indent string which would prevent
      // us from using memset.
      uint8_t* out = nullptr;
      if (indent_count_ > 0) {
        out = stream_->GetDirectBufferForNBytesAndAdvance(len);
      }

      if (out != nullptr) {
        out[0] = '\n';
        memset(&out[1], indent_char_, len - 1);
      } else {
        // Slow path, no contiguous output buffer available.
        WriteChar('\n');
        for (int i = 0; i < level; i++) {
          stream_->WriteRaw(indent_string_.c_str(), indent_string_.length());
        }
      }
    }
  }

  // Writes a prefix. This will write out any pretty printing and
  // commas that are required, followed by the name and a ':' if
  // the name is not null.
  void WritePrefix(StringPiece name, bool render = true);

  void WriteSuffix();

  // Writes an individual character to the output.
  void WriteChar(const char c) { stream_->WriteRaw(&c, sizeof(c)); }

  // Writes a string to the output.
  void WriteRawString(StringPiece s) {
    stream_->WriteRaw(s.data(), s.length());
  }

  void WriteCloseTag();

  void SetHasTextOrAttribute(StringPiece name);

  std::unique_ptr<Element> element_;
  io::CodedOutputStream* stream_;
  ByteSinkWrapper sink_;
  const std::string indent_string_;

  // For the common case of indent being a single character repeated.
  char indent_char_;
  int indent_count_;

  // Whether to use regular or websafe base64 encoding for byte fields. Defaults
  // to regular base64 encoding.
  bool use_websafe_base64_for_bytes_;

  bool tag_needs_closed_;

  bool start_element_;

  GOOGLE_DISALLOW_IMPLICIT_CONSTRUCTORS(XmlObjectWriter);
};

}  // namespace converter
}  // namespace util
}  // namespace protobuf
}  // namespace google

#include <google/protobuf/port_undef.inc>

#endif  // GOOGLE_PROTOBUF_UTIL_INTERNAL_XML_OBJECTWRITER_H__

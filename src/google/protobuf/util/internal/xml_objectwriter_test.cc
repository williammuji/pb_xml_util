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

#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/util/internal/utility.h>
#include <google/protobuf/util/internal/xml_objectwriter.h>
#include <gtest/gtest.h>

#include <cstdint>

namespace google {
namespace protobuf {
namespace util {
namespace converter {

using io::CodedOutputStream;
using io::StringOutputStream;

class XmlObjectWriterTest : public ::testing::Test {
 protected:
  XmlObjectWriterTest()
      : str_stream_(new StringOutputStream(&output_)),
        out_stream_(new CodedOutputStream(str_stream_)),
        ow_(nullptr) {}

  ~XmlObjectWriterTest() override { delete ow_; }

  std::string CloseStreamAndGetString() {
    delete out_stream_;
    delete str_stream_;
    return output_;
  }

  std::string output_;
  StringOutputStream* const str_stream_;
  CodedOutputStream* const out_stream_;
  XmlObjectWriter* ow_;
};

TEST_F(XmlObjectWriterTest, EmptyRootObject) {
  ow_ = new XmlObjectWriter("", out_stream_);
  ow_->StartObject("")->EndObject();
  EXPECT_EQ("<root></root>", CloseStreamAndGetString());
}

TEST_F(XmlObjectWriterTest, EmptyObject) {
  ow_ = new XmlObjectWriter("", out_stream_);
  ow_->StartObject("")
      ->RenderString("test", "value")
      ->StartObject("empty")
      ->EndObject()
      ->EndObject();
  EXPECT_EQ("<root test=\"value\"><empty></empty></root>",
            CloseStreamAndGetString());
}

TEST_F(XmlObjectWriterTest, EmptyRootList) {
  ow_ = new XmlObjectWriter("", out_stream_);
  ow_->StartList("test")->EndList();
  EXPECT_EQ("<_list_test></_list_test>", CloseStreamAndGetString());
}

TEST_F(XmlObjectWriterTest, EmptyList) {
  ow_ = new XmlObjectWriter("", out_stream_);
  ow_->StartObject("")
      ->RenderString("test", "value")
      ->StartList("empty")
      ->EndList()
      ->EndObject();
  EXPECT_EQ("<root test=\"value\"><_list_empty></_list_empty></root>",
            CloseStreamAndGetString());
}

TEST_F(XmlObjectWriterTest, EmptyObjectKey) {
  ow_ = new XmlObjectWriter("", out_stream_);
  ow_->StartObject("")->RenderString("", "value")->EndObject();
  EXPECT_EQ("<root>value</root>", CloseStreamAndGetString());
}

TEST_F(XmlObjectWriterTest, ObjectInObject) {
  ow_ = new XmlObjectWriter("", out_stream_);
  ow_->StartObject("")
      ->StartObject("nested")
      ->RenderString("field", "value")
      ->EndObject()
      ->EndObject();
  EXPECT_EQ("<root><nested field=\"value\"></nested></root>",
            CloseStreamAndGetString());
}

TEST_F(XmlObjectWriterTest, ListInObject) {
  ow_ = new XmlObjectWriter("", out_stream_);
  ow_->StartObject("")
      ->StartList("nested")
      ->RenderString("", "value")
      ->EndList()
      ->EndObject();
  EXPECT_EQ(
      "<root><_list_nested><anonymous>value</anonymous></_list_nested></root>",
      CloseStreamAndGetString());
}

TEST_F(XmlObjectWriterTest, ObjectInList) {
  ow_ = new XmlObjectWriter("", out_stream_);
  ow_->StartObject("")
      ->StartList("test")
      ->StartObject("")
      ->RenderString("field", "value")
      ->EndObject()
      ->EndList()
      ->EndObject();
  EXPECT_EQ(
      "<root><_list_test><test field=\"value\"></test></_list_test></root>",
      CloseStreamAndGetString());
}

TEST_F(XmlObjectWriterTest, ListInList) {
  ow_ = new XmlObjectWriter("", out_stream_);
  ow_->StartObject("")
      ->StartList("bar")
      ->StartObject("")
      ->StartList("foo")
      ->StartObject("")
      ->RenderString("", "value")
      ->EndObject()
      ->EndList()
      ->EndObject()
      ->EndList()
      ->EndObject();
  EXPECT_EQ(
      "<root><_list_bar><bar><_list_foo><foo>value</foo></_list_foo></bar></"
      "_list_bar></root>",
      CloseStreamAndGetString());
}

TEST_F(XmlObjectWriterTest, RenderPrimitives) {
  ow_ = new XmlObjectWriter("", out_stream_);
  ow_->StartObject("")
      ->RenderBool("bool", true)
      ->RenderDouble("double", std::numeric_limits<double>::max())
      ->RenderFloat("float", std::numeric_limits<float>::max())
      ->RenderInt32("int", std::numeric_limits<int32_t>::min())
      ->RenderInt64("long", std::numeric_limits<int64_t>::min())
      ->RenderBytes("bytes", "abracadabra")
      ->RenderString("string", "string")
      ->RenderBytes("emptybytes", "")
      ->RenderString("emptystring", std::string())
      ->EndObject();
  EXPECT_EQ("<root bool=\"true\" double=\"" +
                ValueAsString<double>(std::numeric_limits<double>::max()) +
                "\" float=\"" +
                ValueAsString<float>(std::numeric_limits<float>::max()) +
                "\" int=\"-2147483648\""
                " long=\"-9223372036854775808\""
                " bytes=\"YWJyYWNhZGFicmE=\""
                " string=\"string\""
                " emptybytes=\"\""
                " emptystring=\"\""
                "></root>",
            CloseStreamAndGetString());
}

TEST_F(XmlObjectWriterTest, BytesEncodesAsNonWebSafeBase64) {
  std::string s;
  s.push_back('\377');
  s.push_back('\357');
  ow_ = new XmlObjectWriter("", out_stream_);
  ow_->StartObject("")->RenderBytes("bytes", s)->EndObject();
  // Non-web-safe would encode this as "/+8="
  EXPECT_EQ("<root bytes=\"/+8=\"></root>", CloseStreamAndGetString());
}

TEST_F(XmlObjectWriterTest, PrettyPrintList) {
  ow_ = new XmlObjectWriter(" ", out_stream_);
  ow_->StartObject("")
      ->StartList("items")
      ->RenderString("", "item1")
      ->RenderString("", "item2")
      ->RenderString("", "item3")
      ->EndList()
      ->StartList("empty")
      ->EndList()
      ->EndObject();
  EXPECT_EQ(
      "<root>\n"
      " <_list_items>\n"
      "  <anonymous>item1</anonymous>\n"
      "  <anonymous>item2</anonymous>\n"
      "  <anonymous>item3</anonymous>\n"
      " </_list_items>\n"
      " <_list_empty></_list_empty>\n"
      "</root>\n",
      CloseStreamAndGetString());
}

TEST_F(XmlObjectWriterTest, PrettyPrintObject) {
  ow_ = new XmlObjectWriter(" ", out_stream_);
  ow_->StartObject("")
      ->StartObject("items")
      ->RenderString("key1", "item1")
      ->RenderString("key2", "item2")
      ->RenderString("key3", "item3")
      ->EndObject()
      ->StartObject("empty")
      ->EndObject()
      ->EndObject();
  EXPECT_EQ(
      "<root>\n"
      " <items key1=\"item1\" key2=\"item2\" key3=\"item3\"></items>\n"
      " <empty></empty>\n"
      "</root>\n",
      CloseStreamAndGetString());
}

TEST_F(XmlObjectWriterTest, PrettyPrintEmptyObjectInEmptyList) {
  ow_ = new XmlObjectWriter(" ", out_stream_);
  ow_->StartObject("")->StartList("list")->EndList()->EndObject();
  EXPECT_EQ(
      "<root>\n"
      " <_list_list></_list_list>\n"
      "</root>\n",
      CloseStreamAndGetString());
}

TEST_F(XmlObjectWriterTest, PrettyPrintDoubleIndent) {
  ow_ = new XmlObjectWriter("  ", out_stream_);
  ow_->StartObject("")
      ->RenderBool("bool", true)
      ->RenderInt32("int", 42)
      ->EndObject();
  EXPECT_EQ("<root bool=\"true\" int=\"42\"></root>\n",
            CloseStreamAndGetString());
}

TEST_F(XmlObjectWriterTest, StringsEscapedAndEnclosedInDoubleQuotes) {
  ow_ = new XmlObjectWriter("", out_stream_);
  ow_->StartObject("")->RenderString("string", "'<>&amp;\\\"\r\n")->EndObject();
  EXPECT_EQ("<root string=\"'\\u003c\\u003e&amp;\\\\\\\"\\r\\n\"></root>",
            CloseStreamAndGetString());
}

TEST_F(XmlObjectWriterTest, Stringification) {
  ow_ = new XmlObjectWriter("", out_stream_);
  ow_->StartObject("")
      ->RenderDouble("double_nan", std::numeric_limits<double>::quiet_NaN())
      ->RenderFloat("float_nan", std::numeric_limits<float>::quiet_NaN())
      ->RenderDouble("double_pos", std::numeric_limits<double>::infinity())
      ->RenderFloat("float_pos", std::numeric_limits<float>::infinity())
      ->RenderDouble("double_neg", -std::numeric_limits<double>::infinity())
      ->RenderFloat("float_neg", -std::numeric_limits<float>::infinity())
      ->EndObject();
  EXPECT_EQ(
      "<root double_nan=\"NaN\" float_nan=\"NaN\" double_pos=\"Infinity\" "
      "float_pos=\"Infinity\" double_neg=\"-Infinity\" "
      "float_neg=\"-Infinity\"></root>",
      CloseStreamAndGetString());
}

TEST_F(XmlObjectWriterTest, TestRegularByteEncoding) {
  ow_ = new XmlObjectWriter("", out_stream_);
  ow_->StartObject("")->RenderBytes("bytes", "\x03\xef\xc0")->EndObject();

  // Test that we get regular (non websafe) base64 encoding on byte fields by
  // default.
  EXPECT_EQ("<root bytes=\"A+/A\"></root>", CloseStreamAndGetString());
}

TEST_F(XmlObjectWriterTest, TestWebsafeByteEncoding) {
  ow_ = new XmlObjectWriter("", out_stream_);
  ow_->set_use_websafe_base64_for_bytes(true);
  ow_->StartObject("")->RenderBytes("bytes", "\x03\xef\xc0\x10")->EndObject();

  // Test that we get websafe base64 encoding when explicitly asked.
  EXPECT_EQ("<root bytes=\"A-_AEA==\"></root>", CloseStreamAndGetString());
}

}  // namespace converter
}  // namespace util
}  // namespace protobuf
}  // namespace google

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

#include <google/protobuf/stubs/common.h>
#include <google/protobuf/stubs/logging.h>
#include <google/protobuf/stubs/status.h>
#include <google/protobuf/stubs/time.h>
#include <google/protobuf/util/internal/expecting_objectwriter.h>
#include <google/protobuf/util/internal/object_writer.h>
#include <google/protobuf/util/internal/xml_stream_parser.h>
#include <gtest/gtest.h>

#include <cstdint>

namespace google {
namespace protobuf {
namespace util {
namespace converter {

using ParseErrorType =
    ::google::protobuf::util::converter::XmlStreamParser::ParseErrorType;

// Tests for the XML Stream Parser. These tests are intended to be
// comprehensive and cover the following:
//
// Positive tests:
// - true, false, null
// - empty object or array.
// - negative and positive double and int, unsigned int
// - single and double quoted strings
// - string key, unquoted key, numeric key
// - array containing array, object, value
// - object containing array, object, value
// - unicode handling in strings
// - ascii escaping (\b, \f, \n, \r, \t, \v)
// - trailing commas
//
// Negative tests:
// - illegal literals
// - mismatched quotes failure on strings
// - unterminated string failure
// - unexpected end of string failure
// - mismatched object and array closing
// - Failure to close array or object
// - numbers too large
// - invalid unicode escapes.
// - invalid unicode sequences.
// - numbers as keys
//
// For each test we split the input string on every possible character to ensure
// the parser is able to handle arbitrarily split input for all cases. We also
// do a final test of the entire test case one character at a time.
//
// It is verified that expected calls to the mocked objects are in sequence.
class XmlStreamParserTest : public ::testing::Test {
 protected:
  XmlStreamParserTest() : mock_(), ow_(&mock_) {}
  ~XmlStreamParserTest() override {}

  util::Status RunTest(StringPiece xml, int split,
                       std::function<void(XmlStreamParser*)> setup) {
    XmlStreamParser parser(&mock_);
    setup(&parser);

    // Special case for split == length, test parsing one character at a time.
    if (split == xml.length()) {
      GOOGLE_LOG(INFO) << "Testing split every char: " << xml;
      for (int i = 0; i < xml.length(); ++i) {
        StringPiece single = xml.substr(i, 1);
        util::Status result = parser.Parse(single);
        if (!result.ok()) {
          return result;
        }
      }
      return parser.FinishParse();
    }

    // Normal case, split at the split point and parse two substrings.
    StringPiece first = xml.substr(0, split);
    StringPiece rest = xml.substr(split);
    GOOGLE_LOG(INFO) << "Testing split: " << first << "><" << rest;
    util::Status result = parser.Parse(first);
    if (result.ok()) {
      result = parser.Parse(rest);
      if (result.ok()) {
        result = parser.FinishParse();
      }
    }
    if (result.ok()) {
      EXPECT_EQ(parser.recursion_depth(), 0);
    }
    return result;
  }

  void DoTest(
      StringPiece xml, int split,
      std::function<void(XmlStreamParser*)> setup = [](XmlStreamParser* p) {}) {
    util::Status result = RunTest(xml, split, setup);
    if (!result.ok()) {
      GOOGLE_LOG(WARNING) << result;
    }
    EXPECT_TRUE(result.ok());
  }

  void DoErrorTest(
      StringPiece xml, int split, StringPiece error_prefix,
      std::function<void(XmlStreamParser*)> setup = [](XmlStreamParser* p) {}) {
    util::Status result = RunTest(xml, split, setup);
    EXPECT_TRUE(util::IsInvalidArgument(result));
    StringPiece error_message(result.message());
    EXPECT_EQ(error_prefix, error_message.substr(0, error_prefix.size()));
  }

  void DoErrorTest(
      StringPiece xml, int split, StringPiece error_prefix,
      ParseErrorType expected_parse_error_type,
      std::function<void(XmlStreamParser*)> setup = [](XmlStreamParser* p) {}) {
    util::Status result = RunTest(xml, split, setup);
    EXPECT_TRUE(util::IsInvalidArgument(result));
    StringPiece error_message(result.message());
    EXPECT_EQ(error_prefix, error_message.substr(0, error_prefix.size()));
  }

#ifndef _MSC_VER
  // TODO(xiaofeng): We have to disable InSequence check for MSVC because it
  // causes stack overflow due to its use of a linked list that is destructed
  // recursively.
  ::testing::InSequence in_sequence_;
#endif  // !_MSC_VER
  MockObjectWriter mock_;
  ExpectingObjectWriter ow_;
};

// Positive tests

// - true, false, null
TEST_F(XmlStreamParserTest, SimpleTrue) {
  StringPiece str = "<root>true</root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")->RenderString("", "true")->EndObject();
    // ow_.StartObject("")->RenderBool("", true)->EndObject();
    DoTest(str, i);
  }
}

TEST_F(XmlStreamParserTest, SimpleFalse) {
  StringPiece str = "<root>false</root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")->RenderString("", "false")->EndObject();
    DoTest(str, i);
  }
}

TEST_F(XmlStreamParserTest, SimpleNull) {
  StringPiece str = "<root>null</root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")->RenderString("", "null")->EndObject();
    DoTest(str, i);
  }
}

// - empty object and array.
TEST_F(XmlStreamParserTest, EmptyObject) {
  StringPiece str = "<root></root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")->EndObject();
    DoTest(str, i);
  }
}

TEST_F(XmlStreamParserTest, EmptyList) {
  StringPiece str = "<_list_empty></_list_empty>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartList("empty")->EndList();
    DoTest(str, i);
  }
}

// - negative and positive double and int, unsigned int
TEST_F(XmlStreamParserTest, SimpleDouble) {
  StringPiece str = "<root>42.5</root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")->RenderString("", "42.5")->EndObject();
    DoTest(str, i);
  }
}

TEST_F(XmlStreamParserTest, ScientificDouble) {
  StringPiece str = "<root>1.2345e-10</root>";
  for (int i = 0; i < str.length(); ++i) {
    ow_.StartObject("")->RenderString("", "1.2345e-10")->EndObject();
    DoTest(str, i);
  }
}

TEST_F(XmlStreamParserTest, SimpleNegativeDouble) {
  StringPiece str = "<root>-1045.235</root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")->RenderString("", "-1045.235")->EndObject();
    DoTest(str, i);
  }
}

TEST_F(XmlStreamParserTest, SimpleInt) {
  StringPiece str = "<root>123456</root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")->RenderString("", "123456")->EndObject();
    DoTest(str, i);
  }
}

TEST_F(XmlStreamParserTest, SimpleNegativeInt) {
  StringPiece str = "<root>-79497823553162765</root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")->RenderString("", "-79497823553162765")->EndObject();
    DoTest(str, i);
  }
}

TEST_F(XmlStreamParserTest, SimpleUnsignedInt) {
  StringPiece str = "<root>11779497823553162765</root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")->RenderString("", "11779497823553162765")->EndObject();
    DoTest(str, i);
  }
}

TEST_F(XmlStreamParserTest, AttributeKeyIsInvalid) {
  StringPiece str = "<root 01234=\"x\"></root>";
  for (int i = 0; i <= str.length(); ++i) {
    DoErrorTest(str, i, "Expected a begin key or a slash.",
                ParseErrorType::EXPECTED_BEGIN_KEY_OR_SLASH);
  }
  str = "<root -01234=\"x\"></root>";
  for (int i = 0; i <= str.length(); ++i) {
    DoErrorTest(str, i, "Expected a begin key or a slash.",
                ParseErrorType::EXPECTED_BEGIN_KEY_OR_SLASH);
  }
  str = "<root \'a1234\'=\"x\"></root>";
  for (int i = 0; i <= str.length(); ++i) {
    DoErrorTest(str, i, "Expected a begin key or a slash.",
                ParseErrorType::EXPECTED_BEGIN_KEY_OR_SLASH);
  }
  str = "<root \"a1234\"=\"x\"></root>";
  for (int i = 0; i <= str.length(); ++i) {
    DoErrorTest(str, i, "Expected a begin key or a slash.",
                ParseErrorType::EXPECTED_BEGIN_KEY_OR_SLASH);
  }
}

TEST_F(XmlStreamParserTest, TagNameIsInvalid) {
  StringPiece str = "<root><0x1234></0x1234></root>";
  for (int i = 0; i <= str.length(); ++i) {
    DoErrorTest(str, i, "Expected a tag name.",
                ParseErrorType::EXPECTED_TAG_NAME);
  }
  str = "<root><-0x1234></-0x1234></root>";
  for (int i = 0; i <= str.length(); ++i) {
    DoErrorTest(str, i, "Expected a tag name.",
                ParseErrorType::EXPECTED_TAG_NAME);
  }
  str = "<root><12x34></12x34></root>";
  for (int i = 0; i <= str.length(); ++i) {
    DoErrorTest(str, i, "Expected a tag name.",
                ParseErrorType::EXPECTED_TAG_NAME);
  }
}

// - single and double quoted strings
TEST_F(XmlStreamParserTest, EmptyDoubleQuotedString) {
  StringPiece str = "<root test=\"\"></root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")->RenderString("test", "")->EndObject();
    DoTest(str, i);
  }
}

TEST_F(XmlStreamParserTest, EmptySingleQuotedString) {
  StringPiece str = "<root test=''></root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")->RenderString("test", "")->EndObject();
    DoTest(str, i);
  }
}

TEST_F(XmlStreamParserTest, SimpleDoubleQuotedString) {
  StringPiece str = "<root test=\"Some String\"></root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")->RenderString("test", "Some String")->EndObject();
    DoTest(str, i);
  }
}

TEST_F(XmlStreamParserTest, SimpleSingleQuotedString) {
  StringPiece str = "<root test=\'Another String\'></root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")->RenderString("test", "Another String")->EndObject();
    DoTest(str, i);
  }
}

// - string key, unquoted key, numeric key
TEST_F(XmlStreamParserTest, ObjectKeyTypes) {
  StringPiece str =
      "<root s=\"true\" d=\"false\" "
      "key=\"null\"><_list_snake_key></_list_snake_key><camelKey></camelKey></"
      "root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")
        ->RenderString("s", "true")
        ->RenderString("d", "false")
        ->RenderString("key", "null")
        ->StartList("snake_key")
        ->EndList()
        ->StartObject("camelKey")
        ->EndObject()
        ->EndObject();
    DoTest(str, i);
  }
}

TEST_F(XmlStreamParserTest, UnquotedObjectKeyWithReservedPrefxes) {
  StringPiece str = "<root nullkey=\"a\" truekey=\"b\" falsekey=\"c\"></root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")
        ->RenderString("nullkey", "a")
        ->RenderString("truekey", "b")
        ->RenderString("falsekey", "c")
        ->EndObject();
    DoTest(str, i);
  }
}

TEST_F(XmlStreamParserTest, UnquotedAttributeValue) {
  StringPiece str = "<root foo-bar-baz=a></root>";
  for (int i = 0; i <= str.length(); ++i) {
    DoErrorTest(str, i, "Expected a quote before attribute value.",
                ParseErrorType::EXPECTED_QUOTE_BEFORE_ATTR_VALUE);
  }
}

// - array containing primitive values (true, false, null, num, string)
TEST_F(XmlStreamParserTest, ArrayPrimitiveValues) {
  StringPiece str =
      "<root><_list_test><test>true</test><test>false</test><test>null</"
      "test><test>one</test><test>two</test></_list_test></root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")
        ->StartList("test")
        ->StartObject("")
        ->RenderString("", "true")
        ->EndObject()
        ->StartObject("")
        ->RenderString("", "false")
        ->EndObject()
        ->StartObject("")
        ->RenderString("", "null")
        ->EndObject()
        ->StartObject("")
        ->RenderString("", "one")
        ->EndObject()
        ->StartObject("")
        ->RenderString("", "two")
        ->EndObject()
        ->EndList()
        ->EndObject();
    DoTest(str, i);
  }
}

// - array containing array, object
TEST_F(XmlStreamParserTest, ArrayComplexValues) {
  StringPiece str =
      "<root><_list_test><test><_list_test11><test11>22</"
      "test11><test11>-127</"
      "test11><test11>45.3</test11><test11>-1056.4</"
      "test11><test11>11779497823553162765u</test11></_list_test11></"
      "test>"
      "<test key=\"true\"></test></_list_test></root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")
        ->StartList("test")
        ->StartObject("")
        ->StartList("test11")
        ->StartObject("")
        ->RenderString("", "22")
        ->EndObject()
        ->StartObject("")
        ->RenderString("", "-127")
        ->EndObject()
        ->StartObject("")
        ->RenderString("", "45.3")
        ->EndObject()
        ->StartObject("")
        ->RenderString("", "-1056.4")
        ->EndObject()
        ->StartObject("")
        ->RenderString("", "11779497823553162765u")
        ->EndObject()
        ->EndList()
        ->EndObject()
        ->StartObject("")
        ->RenderString("key", "true")
        ->EndObject()
        ->EndList()
        ->EndObject();
    DoTest(str, i);
  }
}

// - object containing array, object, value (true, false, null, num, string)
TEST_F(XmlStreamParserTest, ObjectValues) {
  StringPiece str =
      "<root t=\"true\" f=\"false\" n=\"null\" s=\"a string\" d=\"another "
      "string\" pi=\"22\" ni=\"-127\" pd=\"45.3\" nd=\"-1056.4\" "
      "pl=\"11779497823553162765u\"><_list_l2><l2><_list_l22></_list_l22></"
      "l2></_list_l2><o "
      "key=\"true\"></o></root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")
        ->RenderString("t", "true")
        ->RenderString("f", "false")
        ->RenderString("n", "null")
        ->RenderString("s", "a string")
        ->RenderString("d", "another string")
        ->RenderString("pi", "22")
        ->RenderString("ni", "-127")
        ->RenderString("pd", "45.3")
        ->RenderString("nd", "-1056.4")
        ->RenderString("pl", "11779497823553162765u")
        ->StartList("l2")
        ->StartObject("")
        ->StartList("l22")
        ->EndList()
        ->EndObject()
        ->EndList()
        ->StartObject("o")
        ->RenderString("key", "true")
        ->EndObject()
        ->EndObject();
    DoTest(str, i);
  }
}

TEST_F(XmlStreamParserTest, RejectNonUtf8WhenNotCoerced) {
  StringPiece xml = "<root address=\"\xFF\"חרושת 23, רעננה, ישראל\"></root>";
  for (int i = 0; i <= xml.length(); ++i) {
    DoErrorTest(xml, i, "Encountered non UTF-8 code points.",
                ParseErrorType::NON_UTF_8);
  }
  xml = "<root address=\"חרושת 23,\xFFרעננה, ישראל\"></root>";
  for (int i = 0; i <= xml.length(); ++i) {
    DoErrorTest(xml, i, "Encountered non UTF-8 code points.",
                ParseErrorType::NON_UTF_8);
  }
  xml = "<root address=\"\xFF\"></root>";
  DoErrorTest(xml, 0, "Encountered non UTF-8 code points.",
              ParseErrorType::NON_UTF_8);
}

// - unicode handling in strings
TEST_F(XmlStreamParserTest, UnicodeEscaping) {
  StringPiece str = "<root>\"\\u0639\\u0631\\u0628\\u0649\"</root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")
        ->RenderString("", "\"\\u0639\\u0631\\u0628\\u0649\"")
        ->EndObject();
    DoTest(str, i);
  }
}

// - unicode UTF-16 surrogate pair handling in strings
TEST_F(XmlStreamParserTest, UnicodeSurrogatePairEscaping) {
  StringPiece str =
      "<root>"
      "\"\\u0bee\\ud800\\uddf1\\uD80C\\uDDA4\\uD83d\\udC1D\\uD83C\\uDF6F\"</"
      "root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")
        ->RenderString("",
                       "\"\\u0bee\\ud800\\uddf1\\uD80C\\uDDA4\\uD83d\\udC1D\\uD"
                       "83C\\uDF6F\"")
        ->EndObject();
    DoTest(str, i);
  }
}

/*
// - ascii escaping (\b, \f, \n, \r, \t, \v)
TEST_F(XmlStreamParserTest, AsciiEscaping) {
  StringPiece str =
      "<root><_list_test><test>\b</test><test>\ning</test><test>test\f</"
      "test><test>\r\t</test><test>test\\\ving</test></_list_test></root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")
        ->StartList("test")
        ->RenderString("", "\b")
        ->RenderString("", "\ning")
        ->RenderString("", "test\f")
        ->RenderString("", "\r\t")
        ->RenderString("", "test\\\ving")
        ->EndList()
        ->EndObject();
    DoTest(str, i);
  }
}
*/

// - trailing commas, we support a single trailing comma but no internal commas.
TEST_F(XmlStreamParserTest, TrailingCommas) {
  StringPiece str =
      "<root><_list_test><test><_list_test2><test2>a</test2><test2>true</"
      "test2></_list_test2></test><test><test3 "
      "b=\"null\"></test3></test></_list_test></root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")
        ->StartList("test")
        ->StartObject("")
        ->StartList("test2")
        ->StartObject("")
        ->RenderString("", "a")
        ->EndObject()
        ->StartObject("")
        ->RenderString("", "true")
        ->EndObject()
        ->EndList()
        ->EndObject()
        ->StartObject("")
        ->StartObject("test3")
        ->RenderString("b", "null")
        ->EndObject()
        ->EndObject()
        ->EndList()
        ->EndObject();
    DoTest(str, i);
  }
}

// Negative tests

// mismatched quotes failure on strings
TEST_F(XmlStreamParserTest, MismatchedSingleQuotedLiteral) {
  StringPiece str = "<root test='Some str\"></root>";
  for (int i = 0; i <= str.length(); ++i) {
    DoErrorTest(str, i, "Closing quote expected in string.",
                ParseErrorType::EXPECTED_CLOSING_QUOTE);
  }
}

TEST_F(XmlStreamParserTest, MismatchedDoubleQuotedLiteral) {
  StringPiece str = "<root test=\"Another string that ends poorly!'></root>";
  for (int i = 0; i <= str.length(); ++i) {
    DoErrorTest(str, i, "Closing quote expected in string.",
                ParseErrorType::EXPECTED_CLOSING_QUOTE);
  }
}

// unterminated strings
TEST_F(XmlStreamParserTest, UnterminatedLiteralString) {
  StringPiece str = "<root test=\"Forgot the rest of i></root>";
  for (int i = 0; i <= str.length(); ++i) {
    DoErrorTest(str, i, "Closing quote expected in string.",
                ParseErrorType::EXPECTED_CLOSING_QUOTE);
  }
}

TEST_F(XmlStreamParserTest, UnterminatedStringEscape) {
  StringPiece str = "<root test=\"Forgot the rest of \\></root>";
  for (int i = 0; i <= str.length(); ++i) {
    DoErrorTest(str, i, "Closing quote expected in string.",
                ParseErrorType::EXPECTED_CLOSING_QUOTE);
  }
}

TEST_F(XmlStreamParserTest, UnterminatedStringInArray) {
  StringPiece str =
      "<_list_test test=\"Forgot to close the string></_list_test>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartList("test");
    DoErrorTest(str, i, "Closing quote expected in string.",
                ParseErrorType::EXPECTED_CLOSING_QUOTE);
  }
}

TEST_F(XmlStreamParserTest, UnterminatedStringInObject) {
  StringPiece str = "<root f=\"Forgot to close the string></root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("");
    DoErrorTest(str, i, "Closing quote expected in string.",
                ParseErrorType::EXPECTED_CLOSING_QUOTE);
  }
}

TEST_F(XmlStreamParserTest, UnterminatedObject) {
  StringPiece str = "<root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("");
    DoErrorTest(str, i, "Unexpected end of string.",
                ParseErrorType::EXPECTED_OBJECT_KEY_OR_BRACES);
  }
}

// mismatched tag name
TEST_F(XmlStreamParserTest, MismatchedCloseObject) {
  StringPiece str = "<root></true>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("");
    DoErrorTest(str, i, "Tag name not match.",
                ParseErrorType::TAG_NAME_NOT_MATCH);
  }
}

TEST_F(XmlStreamParserTest, MismatchedCloseArray) {
  StringPiece str = "<_list_true></_list_null>}";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartList("true");
    DoErrorTest(str, i, "Tag name not match.",
                ParseErrorType::TAG_NAME_NOT_MATCH);
  }
}

// Invalid attribute keys.
TEST_F(XmlStreamParserTest, InvalidNumericAttributeKey) {
  StringPiece str = "<root 42=\"true\">";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("");
    DoErrorTest(str, i, "Expected a begin key or a slash.",
                ParseErrorType::EXPECTED_BEGIN_KEY_OR_SLASH);
  }
}

TEST_F(XmlStreamParserTest, InvalidLiteralObjectInObject) {
  StringPiece str = "<root \"true\"></root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("");
    DoErrorTest(str, i, "Expected a begin key or a slash.",
                ParseErrorType::EXPECTED_BEGIN_KEY_OR_SLASH);
  }
}

TEST_F(XmlStreamParserTest, InvalidLiteralArrayInObject) {
  StringPiece str = "<_list_test \"null\"></_list_test>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartList("test");
    DoErrorTest(str, i, "Expected a begin key or a slash.",
                ParseErrorType::EXPECTED_BEGIN_KEY_OR_SLASH);
  }
}

TEST_F(XmlStreamParserTest, MissingColonAfterKeyInObject) {
  StringPiece str = "<root key></root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("");
    DoErrorTest(str, i, "Expected a equal mark.",
                ParseErrorType::EXPECTED_EQUAL_MARK);
  }
}

TEST_F(XmlStreamParserTest, EndOfTextAfterKeyInObject) {
  StringPiece str = "<root key>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("");
    DoErrorTest(str, i, "Expected a equal mark.",
                ParseErrorType::EXPECTED_EQUAL_MARK);
  }
}

TEST_F(XmlStreamParserTest, MissingValueAfterColonInObject) {
  StringPiece str = "<root key=>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("");
    DoErrorTest(str, i, "Expected a quote before attribute value.",
                ParseErrorType::EXPECTED_QUOTE_BEFORE_ATTR_VALUE);
  }
}

TEST_F(XmlStreamParserTest, MissingSpaceBetweenAttributes) {
  StringPiece str = "<root key=\"20\",hello=\"true\">";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")->RenderString("key", "20");
    DoErrorTest(str, i, "Expected a space or a close tag.",
                ParseErrorType::EXPECTED_SPACE_OR_CLOSE_TAG);
  }
}

TEST_F(XmlStreamParserTest, ExtraCharactersAfterObject) {
  StringPiece str = "<root></root></root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")->EndObject();
    DoErrorTest(str, i, "Parsing terminated before end of input.",
                ParseErrorType::PARSING_TERMINATED_BEFORE_END_OF_INPUT);
  }
}

TEST_F(XmlStreamParserTest, PositiveNumberTooBigIsDouble) {
  StringPiece str = "<root>18446744073709552000.0</root>";  // 2^64
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")->RenderString("", "18446744073709552000.0");
    DoTest(str, i);
  }
}

TEST_F(XmlStreamParserTest, NegativeNumberTooBigIsDouble) {
  StringPiece str = "<root>-18446744073709551616.0</root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")->RenderString("", "-18446744073709551616.0");
    DoTest(str, i);
  }
}

// invalid bare backslash.
TEST_F(XmlStreamParserTest, UnfinishedEscape) {
  StringPiece str = "<root test=\"\\>";
  for (int i = 0; i <= str.length(); ++i) {
    DoErrorTest(str, i, "Closing quote expected in string.",
                ParseErrorType::EXPECTED_CLOSING_QUOTE);
  }
}

// invalid bare backslash u.
TEST_F(XmlStreamParserTest, UnfinishedUnicodeEscape) {
  StringPiece str = "<root test=\"\\u>";
  for (int i = 0; i <= str.length(); ++i) {
    DoErrorTest(str, i, "Illegal hex string.",
                ParseErrorType::ILLEGAL_HEX_STRING);
  }
}

// invalid unicode sequence.
TEST_F(XmlStreamParserTest, UnicodeEscapeCutOff) {
  StringPiece str = "<root test=\"\\u12>";
  for (int i = 0; i <= str.length(); ++i) {
    DoErrorTest(str, i, "Illegal hex string.",
                ParseErrorType::ILLEGAL_HEX_STRING);
  }
}

// invalid unicode sequence (valid in modern EcmaScript but not in XML).
TEST_F(XmlStreamParserTest, BracketedUnicodeEscape) {
  StringPiece str = "<root test=\"\\u{1f36f}\">";
  for (int i = 0; i <= str.length(); ++i) {
    DoErrorTest(str, i, "Invalid escape sequence.",
                ParseErrorType::INVALID_ESCAPE_SEQUENCE);
  }
}

TEST_F(XmlStreamParserTest, UnicodeEscapeInvalidCharacters) {
  StringPiece str = "<root test=\"\\u12$4hello>";
  for (int i = 0; i <= str.length(); ++i) {
    DoErrorTest(str, i, "Invalid escape sequence.",
                ParseErrorType::INVALID_ESCAPE_SEQUENCE);
  }
}

// invalid unicode sequence in low half surrogate: g is not a hex digit.
TEST_F(XmlStreamParserTest, UnicodeEscapeLowHalfSurrogateInvalidCharacters) {
  StringPiece str = "<root test=\"\\ud800\\udcfg\">";
  for (int i = 0; i <= str.length(); ++i) {
    DoErrorTest(str, i, "Invalid escape sequence.",
                ParseErrorType::INVALID_ESCAPE_SEQUENCE);
  }
}

// Extra commas with an object or array.
TEST_F(XmlStreamParserTest, ExtraCommaInObject) {
  StringPiece str = "<root k1=\"true\",k2=\"false\">";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")->RenderString("k1", "true");
    DoErrorTest(str, i, "Expected a space or a close tag.",
                ParseErrorType::EXPECTED_SPACE_OR_CLOSE_TAG);
  }
}

// Extra text beyond end of value.
TEST_F(XmlStreamParserTest, ExtraTextAfterLiteral) {
  StringPiece str = "<root>hello</root>world";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")->RenderString("", "hello");
    DoErrorTest(str, i, "Parsing terminated before end of input.",
                ParseErrorType::PARSING_TERMINATED_BEFORE_END_OF_INPUT);
  }
}

TEST_F(XmlStreamParserTest, ExtraTextAfterObject) {
  StringPiece str = "<root key=\"true\"></root>oops";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")->RenderString("key", "true")->EndObject();
    DoErrorTest(str, i, "Parsing terminated before end of input.",
                ParseErrorType::PARSING_TERMINATED_BEFORE_END_OF_INPUT);
  }
}

TEST_F(XmlStreamParserTest, ExtraTextAfterArray) {
  StringPiece str = "<_list_test>null</_list_test>oops'";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartList("test")->RenderString("", "null")->EndList();
    DoErrorTest(str, i, "Parsing terminated before end of input.",
                ParseErrorType::PARSING_TERMINATED_BEFORE_END_OF_INPUT);
  }
}

// Random unknown text in the value.
TEST_F(XmlStreamParserTest, UnknownCharactersAsAttributeValue) {
  StringPiece str = "<root key=\"*&#25\"></root>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartObject("")->RenderString("key", "*&#25")->EndObject();
    DoTest(str, i);
  }
}

TEST_F(XmlStreamParserTest, UnknownCharactersAsText) {
  StringPiece str = "<root>*&#25</root>";
  for (int i = 0; i <= str.length(); ++i) {
    DoErrorTest(str, i, "Invalid text.", ParseErrorType::INVALID_TEXT);
  }
}

TEST_F(XmlStreamParserTest, UnknownCharactersInArray) {
  StringPiece str = "<_list_key><key>*&#25</key></_list_key>";
  for (int i = 0; i <= str.length(); ++i) {
    ow_.StartList("key");
    DoErrorTest(str, i, "Invalid text.", ParseErrorType::INVALID_TEXT);
  }
}

TEST_F(XmlStreamParserTest, DeepNestXmlNotExceedLimit) {
  std::string str;
  StrAppend(&str, "<root>");
  int count = 98;
  for (int i = 0; i < count; ++i) {
    StrAppend(&str, "<a");
    StrAppend(&str, std::to_string(i));
    StrAppend(&str, ">");
  }
  StrAppend(&str, "<nest64>v1</nest64>");
  for (int i = count; i > 0; --i) {
    StrAppend(&str, "</a");
    StrAppend(&str, std::to_string(i - 1));
    StrAppend(&str, ">");
  }
  StrAppend(&str, "</root>");

  ow_.StartObject("");
  for (int i = 0; i < count; ++i) {
    std::string tag("a");
    tag += std::to_string(i);
    ow_.StartObject(tag);
  }
  ow_.StartObject("nest64")->RenderString("", "v1")->EndObject();
  for (int i = 0; i < count; ++i) {
    ow_.EndObject();
  }
  ow_.EndObject();
  DoTest(str, 0);
}

TEST_F(XmlStreamParserTest, DeepNestXmlExceedLimit) {
  std::string str;
  StrAppend(&str, "<root>");
  int count = 97;
  for (int i = 0; i < count; ++i) {
    StrAppend(&str, "<a");
    StrAppend(&str, std::to_string(i));
    StrAppend(&str, ">");
  }
  // Supports trailing commas.
  StrAppend(&str,
            "<nest11><nest12></nest12></nest11><nest21><nest22><nest23></"
            "nest23></nest22></nest21>");
  for (int i = count; i > 0; --i) {
    StrAppend(&str, "</a");
    StrAppend(&str, std::to_string(i - 1));
    StrAppend(&str, ">");
  }
  StrAppend(&str, "</root>");

  DoErrorTest(str, 0,
              "Message too deep. Max recursion depth reached for tag 'nest23'");
}

}  // namespace converter
}  // namespace util
}  // namespace protobuf
}  // namespace google

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

#ifndef GOOGLE_PROTOBUF_UTIL_INTERNAL_XML_STREAM_PARSER_H__
#define GOOGLE_PROTOBUF_UTIL_INTERNAL_XML_STREAM_PARSER_H__

#include <google/protobuf/stubs/common.h>
#include <google/protobuf/stubs/status.h>
#include <google/protobuf/stubs/strutil.h>

#include <cstdint>
#include <stack>
#include <string>

// Must be included last.
#include <google/protobuf/port_def.inc>

namespace google {
namespace protobuf {
namespace util {
namespace converter {

class ObjectWriter;

// A XML parser that can parse a stream of XML chunks rather than needing the
// entire XML string up front. It is a modified version of the parser in
// //net/proto/xml/xml-parser.h that has been changed in the following ways:
// - Changed from recursion to an explicit stack to allow resumption
// - Writes directly to an ObjectWriter rather than using subclassing
//
// Here is an example usage:
// XmlStreamParser parser(ow_.get());
// util::Status result = parser.Parse(chunk1);
// result.Update(parser.Parse(chunk2));
// result.Update(parser.FinishParse());
// GOOGLE_DCHECK(result.ok()) << "Failed to parse XML";
//
// This parser is thread-compatible as long as only one thread is calling a
// Parse() method at a time.
class PROTOBUF_EXPORT XmlStreamParser {
 public:
  // Creates a XmlStreamParser that will write to the given ObjectWriter.
  explicit XmlStreamParser(ObjectWriter* ow);
  virtual ~XmlStreamParser();

  // Parses a UTF-8 encoded XML string from a StringPiece. If the returned
  // status is non-ok, the status might contain a payload ParseErrorType with
  // type_url kParseErrorTypeUrl and a payload containing string snippet of the
  // error with type_url kParseErrorSnippetUrl.
  util::Status Parse(StringPiece xml);

  // Finish parsing the XML string. If the returned status is non-ok, the
  // status might contain a payload ParseErrorType with type_url
  // kParseErrorTypeUrl and a payload containing string snippet of the error
  // with type_url kParseErrorSnippetUrl.
  util::Status FinishParse();

  // Sets the max recursion depth of XML message to be deserialized. XML
  // messages over this depth will fail to be deserialized.
  // Default value is 100.
  void set_max_recursion_depth(int max_depth) {
    max_recursion_depth_ = max_depth;
  }

  // Denotes the cause of error.
  enum ParseErrorType {
    INVALID_KEY,
    NON_UTF_8,
    PARSING_TERMINATED_BEFORE_END_OF_INPUT,
    EXPECTED_CLOSING_QUOTE,
    EXPECTED_TAG_NAME,
    ILLEGAL_HEX_STRING,
    INVALID_ESCAPE_SEQUENCE,
    MISSING_LOW_SURROGATE,
    INVALID_LOW_SURROGATE,
    INVALID_UNICODE,
    UNABLE_TO_PARSE_NUMBER,
    EXPECTED_OPEN_TAG,
    EXPECTED_OPEN_TAG_IN_END_ELEMENT,
    EXPECTED_CLOSE_TAG_IN_BEGIN_ELEMENT,
    INVALID_TEXT,
    INVALID_END_TAG_NAME,
    TAG_NAME_NOT_MATCH,
    EXPECTED_TAG_NAME_IN_END_TAG,
    EXPECTED_BEGIN_KEY_OR_SLASH,
    EXPECTED_QUOTE_BEFORE_ATTR_VALUE,
    ILLEGAL_COMMENT,
    EXPECTED_DASH_IN_COMMENT,
    ILLEGAL_CLOSE_COMMENT,
    EXPECTED_CLOSE_DASH_IN_COMMENT,
    ILLEGAL_DECLARATION,
    EXPECTED_QUESTION_MARK_IN_COMMENT,
    ILLEGAL_CLOSE_DECLARATION,
    EXPECTED_CLOSE_QUESTION_MARK_IN_DECLARATION,
    EXPECTED_CLOSING_TAG,
    INVALID_TAG_NAME,
    EXPECTED_END_TAG_SLASH,
    OCTAL_OR_HEX_ARE_NOT_VALID_JSON_VALUES,
    EXPECTED_OBJECT_KEY_OR_BRACES,
    UNEXPECTED_TOKEN,
    EXPECTED_VALID_TAG,
    EXPECTED_COMMA_OR_BRACES,
    EXPECTED_COLON,
    EXPECTED_COMMA_OR_BRACKET,
    NUMBER_EXCEEDS_RANGE_DOUBLE,
    EXPECTED_VALUE,
    OCTAL_OR_HEX_ARE_NOT_VALID_XML_VALUES,
    EXPECTED_SPACE_OR_CLOSE_TAG,
    EXPECTED_CLOSE_TAG,
    EXPECTED_SLASH,
    EXPECTED_EQUAL_MARK,
    EXPECTED_CLOSE_IN_END_ELEMENT
  };

 private:
  friend class XmlStreamParserTest;
  // Return the current recursion depth.
  int recursion_depth() { return recursion_depth_; }

  enum TokenType {
    OPEN_TAG,              // <
    CLOSE_TAG,             // >
    END_TAG_SLASH,         // /
    DECLARATION,           // ?
    COMMENT,               // !
    BEGIN_STRING,          // " or '
    ATTR_SEPARATOR,        // space
    ATTR_VALUE_SEPARATOR,  // =
    BEGIN_KEY,             // letter, _, $ or digit.  Must begin with non-digit
    BEGIN_TEXT,            // any character except <
    UNKNOWN                // Unknown token or we ran out of the stream.
  };

  enum ParseType {
    BEGIN_ELEMENT,        // Expects a <
    START_TAG,            // Expects a tagname, /, ! or ?
    BEGIN_ELEMENT_MID,    // Expects a space or >
    ATTR_KEY,             // Expects a key or /
    ATTR_MID,             // Expects a =
    ATTR_VALUE,           // Expects a quote or a double quote
    BEGIN_ELEMENT_CLOSE,  // Expects a >
    TEXT,                 // Expects a text or <
    END_ELEMENT,          // Expects a <
    END_ELEMENT_MID,      // Expects a /
    END_TAG,              // Expects a tagname
    END_ELEMENT_CLOSE,    // Expects a >
    ELEMENT_MID,          // Expects a close tag or />
  };

  enum ElementType {
    OBJECT,
    LIST,
  };

  // Parses a single chunk of XML, returning an error if the XML was invalid.
  util::Status ParseChunk(StringPiece chunk);

  // Runs the parser based on stack_ and p_, until the stack is empty or p_ runs
  // out of data. If we unexpectedly run out of p_ we push the latest back onto
  // the stack and return.
  util::Status RunParser();

  util::Status ParseBeginElement(TokenType type);

  util::Status ParseStartTag(TokenType type);

  util::Status ParseBeginElementMid(TokenType type);

  util::Status ParseText(TokenType type);

  util::Status ParseText();

  util::Status ParseEndElement(TokenType type);

  util::Status ParseBeginElementClose(TokenType type);

  util::Status ParseEndElement();

  util::Status ParseEndElementMid(TokenType type);

  util::Status ParseEndElementMid();

  util::Status ParseEndElementClose(TokenType type);

  util::Status ParseEndTag(TokenType type);

  util::Status ParseAttrKey(TokenType type);

  util::Status ParseAttrMid(TokenType type);

  util::Status ParseAttrValue(TokenType type);

  util::Status ParseComments();

  util::Status ParseDeclaration();

  // Expects p_ to point to the beginning of a key.
  util::Status ParseKey();

  util::Status ParseStartTagName();

  util::Status ParseEndTagName();

  util::Status ParseTagName();

  // Parses a string and writes it out to the ow_.
  util::Status ParseString();

  // Parses a string, storing the result in parsed_.
  util::Status ParseStringHelper();

  // This function parses unicode escape sequences in strings. It returns an
  // error when there's a parsing error, either the size is not the expected
  // size or a character is not a hex digit.  When it returns str will contain
  // what has been successfully parsed so far.
  util::Status ParseUnicodeEscape();

  // Report a failure as a util::Status.
  util::Status ReportFailure(StringPiece message, ParseErrorType parse_code);

  // Report a failure due to an UNKNOWN token type. We check if we hit the
  // end of the stream and if we're finishing or not to detect what type of
  // status to return in this case.
  util::Status ReportUnknown(StringPiece message, ParseErrorType parse_code);

  // Helper function to check recursion depth and increment it. It will return
  // OkStatus() if the current depth is allowed. Otherwise an error is returned.
  // key is used for error reporting.
  util::Status IncrementRecursionDepth(StringPiece tag_name) const;

  // Advance p_ past all whitespace or until the end of the string.
  void SkipWhitespace();
  void SkipWhitespace(ParseType type);

  // Advance p_ one UTF-8 character
  void Advance();

  // Return the type of the next token at p_.
  TokenType GetNextTokenType(ParseType type);

  // The object writer to write parse events to.
  ObjectWriter* ow_;

  // The stack of parsing we still need to do. When the stack runs empty we will
  // have parsed a single value from the root (e.g. an object or list).
  std::stack<ParseType> stack_;

  // Contains any leftover text from a previous chunk that we weren't able to
  // fully parse, for example the start of a key or number.
  std::string leftover_;

  // The current chunk of XML being parsed. Primarily used for providing
  // context during error reporting.
  StringPiece xml_;

  // A pointer within the current XML being parsed, used to track location.
  StringPiece p_;

  // Stores the last key read, as we separate parsing of keys and values.
  StringPiece key_;

  // Storage for key_ if we need to keep ownership, for example between chunks
  // or if the key was unescaped from a XML string.
  std::string key_storage_;

  // True during the FinishParse() call, so we know that any errors are fatal.
  // For example an unterminated string will normally result in cancelling and
  // trying during the next chunk, but during FinishParse() it is an error.
  bool finishing_;

  // Whether non whitespace tokens have been seen during parsing.
  // It is used to handle the case of a pure whitespace stream input.
  bool seen_non_whitespace_;

  // The XmlStreamParser requires a root element by default and it will raise
  // error if the root element is missing. If `allow_no_root_element_` is true,
  // the XmlStreamParser can also handle this case.
  bool allow_no_root_element_;

  // String we parsed during a call to ParseStringHelper().
  StringPiece parsed_;

  // Storage for the string we parsed. This may be empty if the string was able
  // to be parsed directly from the input.
  std::string parsed_storage_;

  // The character that opened the string, either ' or ".
  // A value of 0 indicates that string parsing is not in process.
  char string_open_;

  // Storage for the chunk that are being parsed in ParseChunk().
  std::string chunk_storage_;

  // Whether to allow non UTF-8 encoded input and replace invalid code points.
  bool coerce_to_utf8_;

  // Replacement character for invalid UTF-8 code points.
  std::string utf8_replacement_character_;

  // Tracks current recursion depth.
  mutable int recursion_depth_;

  // Maximum allowed recursion depth.
  int max_recursion_depth_;

  StringPiece text_;

  // Stores the last tag name read
  StringPiece tag_name_;

  std::stack<std::pair<std::string, bool>> tag_name_stack_;

  std::stack<ElementType> element_type_stack_;

  GOOGLE_DISALLOW_IMPLICIT_CONSTRUCTORS(XmlStreamParser);
};

}  // namespace converter
}  // namespace util
}  // namespace protobuf
}  // namespace google

#include <google/protobuf/port_undef.inc>

#endif  // GOOGLE_PROTOBUF_UTIL_INTERNAL_XML_STREAM_PARSER_H__

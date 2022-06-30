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
#include <google/protobuf/stubs/strutil.h>
#include <google/protobuf/util/internal/json_escaping.h>
#include <google/protobuf/util/internal/object_writer.h>
#include <google/protobuf/util/internal/xml_stream_parser.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <stack>
#include <string>

namespace google {
namespace protobuf {
namespace util {

namespace converter {

// Number of digits in an escaped UTF-16 code unit ('\\' 'u' X X X X)
static const int kUnicodeEscapedLength = 6;

static const int kDefaultMaxRecursionDepth = 100;

inline bool IsLetter(char c) {
  return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || (c == '_');
}

inline bool IsAlphanumeric(char c) {
  return IsLetter(c) || ('0' <= c && c <= '9');
}

inline bool IsAlphanumericOrHyphen(char c) {
  return IsAlphanumeric(c) || (c == '-');
}

// Indicates a character may not be part of an unquoted key.
inline bool IsKeySeparator(char c) {
  return (ascii_isspace(c) || c == '"' || c == '\'' || c == '{' || c == '}' ||
          c == '[' || c == ']' || c == ':' || c == ',');
}

inline bool IsPredefinedEntities(StringPiece input) {
  return (input == "&lt;" || input == "&gt;" || input == "&amp;" ||
          input == "&apos;" || input == "&quot;");
}

inline void ReplaceInvalidCodePoints(StringPiece str,
                                     const std::string& replacement,
                                     std::string* dst) {
  while (!str.empty()) {
    int n_valid_bytes = internal::UTF8SpnStructurallyValid(str);
    StringPiece valid_part = str.substr(0, n_valid_bytes);
    StrAppend(dst, valid_part);

    if (n_valid_bytes == str.size()) {
      break;
    }

    // Append replacement value.
    StrAppend(dst, replacement);

    // Move past valid bytes + one invalid byte.
    str.remove_prefix(n_valid_bytes + 1);
  }
}

static bool ConsumeKey(StringPiece* input, StringPiece* key) {
  if (input->empty() || !IsLetter((*input)[0])) return false;
  int len = 1;
  for (; len < input->size(); ++len) {
    if (!IsAlphanumericOrHyphen((*input)[len])) {
      break;
    }
  }
  *key = StringPiece(input->data(), len);
  *input = StringPiece(input->data() + len, input->size() - len);
  return true;
}

static bool MatchKey(StringPiece input) {
  return !input.empty() && IsLetter(input[0]);
}

// Tag names cannot contain any of the characters
// !"#$%&'()*+,/;<=>?@[\]^`{|}~, nor a space character,
// and cannot begin with "-", ".", or a numeric digit.
static bool ConsumeTagName(StringPiece* input, StringPiece* tag_name) {
  if (input->empty() || !IsLetter((*input)[0])) return false;
  int len = 1;
  for (; len < input->size(); ++len) {
    if (!IsAlphanumericOrHyphen((*input)[len])) {
      break;
    }
  }
  *tag_name = StringPiece(input->data(), len);
  *input = StringPiece(input->data() + len, input->size() - len);
  return true;
}

static bool ConsumeText(StringPiece* input, StringPiece* text) {
  if (input->empty()) return false;
  int len = 1;
  for (; len < input->size(); ++len) {
    if ((*input)[len] == '<') {
      break;
    } else if ((*input)[len] == '&') {
      if (!IsPredefinedEntities(
              StringPiece(input->data() + len, input->size() - len))) {
        return false;
      }
    }
  }
  *text = StringPiece(input->data(), len);
  *input = StringPiece(input->data() + len, input->size() - len);
  return true;
}

XmlStreamParser::XmlStreamParser(ObjectWriter* ow)
    : ow_(ow),
      stack_(),
      leftover_(),
      xml_(),
      p_(),
      key_(),
      key_storage_(),
      finishing_(false),
      seen_non_whitespace_(false),
      allow_no_root_element_(false),
      parsed_(),
      parsed_storage_(),
      string_open_(0),
      chunk_storage_(),
      coerce_to_utf8_(false),
      utf8_replacement_character_(" "),
      recursion_depth_(0),
      max_recursion_depth_(kDefaultMaxRecursionDepth),
      text_(),
      tag_name_(),
      tag_name_stack_(),
      element_type_stack_() {
  // Initialize the stack with a single value to be parsed.
  stack_.push(BEGIN_ELEMENT);
}

XmlStreamParser::~XmlStreamParser() {}

util::Status XmlStreamParser::Parse(StringPiece xml) {
  StringPiece chunk = xml;
  // If we have leftovers from a previous chunk, append the new chunk to it
  // and create a new StringPiece pointing at the string's data. This could
  // be large but we rely on the chunks to be small, assuming they are
  // fragments of a Cord.
  if (!leftover_.empty()) {
    // Don't point chunk to leftover_ because leftover_ will be updated in
    // ParseChunk(chunk).
    chunk_storage_.swap(leftover_);
    StrAppend(&chunk_storage_, xml);
    chunk = StringPiece(chunk_storage_);
  }

  // FIXME
  // Find the structurally valid UTF8 prefix and parse only that.
  int n = internal::UTF8SpnStructurallyValid(chunk);
  if (n > 0) {
    util::Status status = ParseChunk(chunk.substr(0, n));

    // Any leftover characters are stashed in leftover_ for later parsing when
    // there is more data available.
    StrAppend(&leftover_, chunk.substr(n));
    return status;
  } else {
    leftover_.assign(chunk.data(), chunk.size());
    return util::Status();
  }
}

util::Status XmlStreamParser::FinishParse() {
  // If we do not expect anything and there is nothing left to parse we're all
  // done.

  if (stack_.empty() && leftover_.empty() && tag_name_stack_.empty()) {
    return util::Status();
  }

  // Lifetime needs to last until RunParser returns, so keep this variable
  // outside of the coerce_to_utf8 block.
  std::unique_ptr<std::string> scratch;

  bool is_valid_utf8 = internal::IsStructurallyValidUTF8(leftover_);
  if (coerce_to_utf8_ && !is_valid_utf8) {
    scratch.reset(new std::string);
    scratch->reserve(leftover_.size() * utf8_replacement_character_.size());
    ReplaceInvalidCodePoints(leftover_, utf8_replacement_character_,
                             scratch.get());
    p_ = xml_ = *scratch;
  } else {
    p_ = xml_ = leftover_;
    if (!is_valid_utf8) {
      return ReportFailure("Encountered non UTF-8 code points.",
                           ParseErrorType::NON_UTF_8);
    }
  }

  // Parse the remainder in finishing mode, which reports errors for things like
  // unterminated strings or unknown tokens that would normally be retried.
  finishing_ = true;
  util::Status result = RunParser();
  if (result.ok()) {
    SkipWhitespace();
    if (!p_.empty()) {
      result =
          ReportFailure("Parsing terminated before end of input.",
                        ParseErrorType::PARSING_TERMINATED_BEFORE_END_OF_INPUT);
    }
  }
  return result;
}

util::Status XmlStreamParser::ParseChunk(StringPiece chunk) {
  // Do not do any work if the chunk is empty.
  if (chunk.empty()) return util::Status();

  p_ = xml_ = chunk;

  finishing_ = false;
  util::Status result = RunParser();
  if (!result.ok()) return result;

  SkipWhitespace();
  if (p_.empty()) {
    // If we parsed everything we had, clear the leftover.
    leftover_.clear();
  } else {
    // If we do not expect anything i.e. stack is empty, and we have non-empty
    // string left to parse, we report an error.
    if (stack_.empty()) {
      return ReportFailure(
          "Parsing terminated before end of input.",
          ParseErrorType::PARSING_TERMINATED_BEFORE_END_OF_INPUT);
    }
    // If we expect future data i.e. stack is non-empty, and we have some
    // unparsed data left, we save it for later parse.
    leftover_ = std::string(p_);
  }
  return util::Status();
}

util::Status XmlStreamParser::RunParser() {
  while (!stack_.empty()) {
    ParseType type = stack_.top();
    TokenType t = (string_open_ == 0) ? GetNextTokenType(type) : BEGIN_STRING;
    stack_.pop();
    util::Status result;
    switch (type) {
      case BEGIN_ELEMENT:
        result = ParseBeginElement(t);
        break;

      case START_TAG:
        result = ParseStartTag(t);
        break;

      case BEGIN_ELEMENT_MID:
        result = ParseBeginElementMid(t);
        break;

      case ATTR_KEY:
        result = ParseAttrKey(t);
        break;

      case ATTR_MID:
        result = ParseAttrMid(t);
        break;

      case ATTR_VALUE:
        result = ParseAttrValue(t);
        break;

      case BEGIN_ELEMENT_CLOSE:
        result = ParseBeginElementClose(t);
        break;

      case TEXT:
        result = ParseText(t);
        break;

      case END_ELEMENT:
        result = ParseEndElement(t);
        break;

      case END_ELEMENT_MID:
        result = ParseEndElementMid(t);
        break;

      case END_TAG:
        result = ParseEndTag(t);
        break;

      case END_ELEMENT_CLOSE:
        result = ParseEndElementClose(t);
        break;

      default:
        result = util::InternalError(StrCat("Unknown parse type: ", type));
        break;
    }
    if (!result.ok()) {
      // If we were cancelled, save our state and try again later.
      if (!finishing_ && util::IsCancelled(result)) {
        stack_.push(type);
        // If we have a key we still need to render, make sure to save off the
        // contents in our own storage.
        if (!key_.empty() && key_storage_.empty()) {
          StrAppend(&key_storage_, key_);
          key_ = StringPiece(key_storage_);
        }
        result = util::Status();
      }
      return result;
    }
  }
  return util::Status();
}

util::Status XmlStreamParser::ParseBeginElement(TokenType type) {
  if (type == OPEN_TAG) {
    Advance();
    stack_.push(START_TAG);
    return util::Status();
  } else if (type == UNKNOWN) {
    return ReportUnknown("Expected an open tag.",
                         ParseErrorType::EXPECTED_OPEN_TAG);
  }
  return ReportFailure("Expected an open tag.",
                       ParseErrorType::EXPECTED_OPEN_TAG);
}

util::Status XmlStreamParser::ParseStartTag(TokenType type) {
  switch (type) {
    case DECLARATION:
      return ParseDeclaration();
    case COMMENT:
      return ParseComments();
    case BEGIN_KEY:
      return ParseStartTagName();
    case END_TAG_SLASH:
      return ParseEndElementMid();
    case UNKNOWN:
      return ReportUnknown("Expected a tag name.",
                           ParseErrorType::EXPECTED_TAG_NAME);
    default: {
      return ReportFailure("Expected a tag name.",
                           ParseErrorType::EXPECTED_TAG_NAME);
    }
  }
}

util::Status XmlStreamParser::ParseBeginElementMid(TokenType type) {
  switch (type) {
    case ATTR_SEPARATOR:
      Advance();
      stack_.push(ATTR_KEY);
      return util::Status();
    case CLOSE_TAG:
      Advance();
      stack_.push(TEXT);
      return util::Status();
    case UNKNOWN:
      return ReportUnknown("Expected a space or a close tag.",
                           ParseErrorType::EXPECTED_SPACE_OR_CLOSE_TAG);
    default: {
      return ReportFailure("Expected a space or a close tag.",
                           ParseErrorType::EXPECTED_SPACE_OR_CLOSE_TAG);
    }
  }
}

util::Status XmlStreamParser::ParseText(TokenType type) {
  if (type == OPEN_TAG) {
    Advance();
    stack_.push(TEXT);
    stack_.push(START_TAG);
    return util::Status();
  } else if (type == UNKNOWN) {
    return ReportUnknown("Expected an open tag.",
                         ParseErrorType::EXPECTED_OPEN_TAG);
  } else {
    return ParseText();
  }
}

util::Status XmlStreamParser::ParseText() {
  StringPiece original = p_;

  if (!ConsumeText(&p_, &text_)) {
    return ReportFailure("Invalid text.", ParseErrorType::INVALID_TEXT);
  }

  // If we consumed everything but expect more data, reset p_ and cancel since
  // we can't know if the key was complete or not.
  if (!finishing_ && p_.empty()) {
    p_ = original;
    return util::CancelledError("");
  }

  ow_->RenderString("", text_);
  stack_.push(END_ELEMENT);
  return util::Status();
}

util::Status XmlStreamParser::ParseEndElement(TokenType type) {
  if (type == OPEN_TAG) {
    return ParseEndElement();
  } else if (type == UNKNOWN) {
    return ReportUnknown("Expected an open tag.",
                         ParseErrorType::EXPECTED_OPEN_TAG);
  }
  return ReportFailure("Expected a open tag in end element.",
                       ParseErrorType::EXPECTED_OPEN_TAG_IN_END_ELEMENT);
}

util::Status XmlStreamParser::ParseBeginElementClose(TokenType type) {
  if (type == CLOSE_TAG) {
    Advance();
    stack_.push(TEXT);
    return util::Status();
  } else if (type == UNKNOWN) {
    return ReportUnknown("Expected a close tag.",
                         ParseErrorType::EXPECTED_CLOSE_TAG);
  }
  return ReportFailure("Expected a close tag in begin element.",
                       ParseErrorType::EXPECTED_CLOSE_TAG_IN_BEGIN_ELEMENT);
}

util::Status XmlStreamParser::ParseEndElement() {
  GOOGLE_DCHECK_EQ('<', *p_.data());
  Advance();
  stack_.push(END_ELEMENT_MID);
  return util::Status();
}

util::Status XmlStreamParser::ParseEndElementMid(TokenType type) {
  if (type == END_TAG_SLASH) {
    Advance();
    stack_.push(END_TAG);
    return util::Status();
  } else if (type == UNKNOWN) {
    return ReportUnknown("Expected a slash.", ParseErrorType::EXPECTED_SLASH);
  }
  return ReportFailure("Expected an end tag slash.",
                       ParseErrorType::EXPECTED_END_TAG_SLASH);
}

util::Status XmlStreamParser::ParseEndElementMid() {
  GOOGLE_DCHECK_EQ('/', *p_.data());
  Advance();
  if (stack_.top() == TEXT) {
    stack_.pop();
  }
  stack_.push(END_TAG);
  return util::Status();
}

util::Status XmlStreamParser::ParseEndElementClose(TokenType type) {
  if (type == CLOSE_TAG) {
    Advance();
    return util::Status();
  } else if (type == UNKNOWN) {
    return ReportUnknown("Expected a close tag.",
                         ParseErrorType::EXPECTED_CLOSE_TAG);
  }
  return ReportFailure("Expected an close tag in end element.",
                       ParseErrorType::EXPECTED_CLOSE_IN_END_ELEMENT);
}

util::Status XmlStreamParser::ParseEndTag(TokenType type) {
  StringPiece original = p_;

  // end tag name
  if (type == BEGIN_KEY) {
    if (!ConsumeTagName(&p_, &tag_name_)) {
      return ReportFailure("Invalid end tag name.",
                           ParseErrorType::INVALID_END_TAG_NAME);
    }
    // If we consumed everything but expect more data, reset p_ and cancel since
    // we can't know if the key was complete or not.
    if (!finishing_ && p_.empty()) {
      p_ = original;
      return util::CancelledError("");
    }
    bool end_list = false;
    StringPiece tag_name = tag_name_;
    if (tag_name.starts_with("_list_")) {
      tag_name = tag_name_.substr(6);
      end_list = true;
    }
    auto& [start_tag_name, _] = tag_name_stack_.top();
    if (start_tag_name == tag_name) {
      if (end_list) {
        ow_->EndList();
        element_type_stack_.pop();
      } else {
        if (tag_name != "anonymous") {
          ow_->EndObject();
        }
        element_type_stack_.pop();
        --recursion_depth_;
      }
      tag_name_stack_.pop();
      stack_.push(END_ELEMENT_CLOSE);
    } else {
      return ReportFailure("Tag name not match.",
                           ParseErrorType::TAG_NAME_NOT_MATCH);
    }
    return util::Status();
  } else if (type == UNKNOWN) {
    return ReportUnknown("Expected a tag name.",
                         ParseErrorType::EXPECTED_TAG_NAME);
  }

  // Illegal token after open angle bracket in end tag.
  return ReportFailure("Expected a tag name in end tag.",
                       ParseErrorType::EXPECTED_TAG_NAME_IN_END_TAG);
}

util::Status XmlStreamParser::ParseAttrKey(TokenType type) {
  if (type == END_TAG_SLASH) {
    Advance();
    stack_.push(BEGIN_ELEMENT_CLOSE);
    return util::Status();
  } else if (type == BEGIN_KEY) {
    util::Status result = ParseKey();
    if (result.ok()) {
      stack_.push(ATTR_MID);
    }
    return result;
  } else if (type == UNKNOWN) {
    return ReportUnknown("Expected a begin key or a slash.",
                         ParseErrorType::EXPECTED_BEGIN_KEY_OR_SLASH);
  }

  // Illegal token after space.
  return ReportFailure("Expected a begin key or a slash.",
                       ParseErrorType::EXPECTED_BEGIN_KEY_OR_SLASH);
}

util::Status XmlStreamParser::ParseAttrMid(TokenType type) {
  if (type == ATTR_VALUE_SEPARATOR) {
    Advance();
    stack_.push(ATTR_VALUE);
    return util::Status();
  } else if (type == UNKNOWN) {
    return ReportUnknown("Expected a equal mark.",
                         ParseErrorType::EXPECTED_EQUAL_MARK);
  }
  return ReportFailure("Expected a equal mark.",
                       ParseErrorType::EXPECTED_EQUAL_MARK);
}

util::Status XmlStreamParser::ParseAttrValue(TokenType type) {
  if (type == BEGIN_STRING) {
    util::Status result = ParseStringHelper();
    if (result.ok()) {
      ow_->RenderString(key_, parsed_);
      key_ = StringPiece();
      parsed_ = StringPiece();
      key_storage_.clear();
      parsed_storage_.clear();
      stack_.push(BEGIN_ELEMENT_MID);
    }
    return result;
  } else if (type == UNKNOWN) {
    return ReportUnknown("Expected a quote before attribute value.",
                         ParseErrorType::EXPECTED_QUOTE_BEFORE_ATTR_VALUE);
  }
  return ReportFailure("Expected a quote before attribute value.",
                       ParseErrorType::EXPECTED_QUOTE_BEFORE_ATTR_VALUE);
}

util::Status XmlStreamParser::ParseComments() {
  if (p_.length() < 2) {
    if (!finishing_) {
      return util::CancelledError("");
    }
    return ReportFailure("Illegal comment.", ParseErrorType::ILLEGAL_COMMENT);
  }

  const char* data = p_.data();
  if (data[0] != '-' || data[1] != '-') {
    return ReportFailure("Dash expected in comment.",
                         ParseErrorType::EXPECTED_DASH_IN_COMMENT);
  } else {
    // We handled two characters, so advance past them and continue.
    p_.remove_prefix(2);
  }

  while (!p_.empty()) {
    const char* data = p_.data();
    if (data[0] == '-') {
      if (p_.length() < 3) {
        if (!finishing_) {
          return util::CancelledError("");
        }
        return ReportFailure("Illegal close comment.",
                             ParseErrorType::ILLEGAL_CLOSE_COMMENT);
      }
      if (data[1] != '-' || data[2] != '>') {
        return ReportFailure("Illegal close comment.",
                             ParseErrorType::ILLEGAL_CLOSE_COMMENT);
      } else {
        p_.remove_prefix(3);
        return util::Status();
      }
    } else {
      // Normal character, just advance past it.
      Advance();
    }
  }
  // If we didn't find the closing quote but we expect more data, cancel for now
  if (!finishing_) {
    return util::CancelledError("");
  }
  return ReportFailure("Close dash expected in comment.",
                       ParseErrorType::EXPECTED_CLOSE_DASH_IN_COMMENT);
}

util::Status XmlStreamParser::ParseDeclaration() {
  if (p_.length() < 1) {
    if (!finishing_) {
      return util::CancelledError("");
    }
    return ReportFailure("Illegal comment.",
                         ParseErrorType::ILLEGAL_DECLARATION);
  }

  const char* data = p_.data();
  if (data[0] != '?') {
    return ReportFailure("Question mark expected in comment.",
                         ParseErrorType::EXPECTED_QUESTION_MARK_IN_COMMENT);
  } else {
    p_.remove_prefix(1);
  }

  while (!p_.empty()) {
    const char* data = p_.data();
    if (data[0] == '?') {
      if (p_.length() < 2) {
        if (!finishing_) {
          return util::CancelledError("");
        }
        return ReportFailure("Illegal close declaration.",
                             ParseErrorType::ILLEGAL_CLOSE_DECLARATION);
      }
      if (data[1] != '>') {
        return ReportFailure("Illegal close declaration.",
                             ParseErrorType::ILLEGAL_CLOSE_DECLARATION);
      } else {
        p_.remove_prefix(2);
        return util::Status();
      }
    } else {
      // Normal character, just advance past it.
      Advance();
    }
  }
  // If we didn't find the closing quote but we expect more data, cancel for now
  if (!finishing_) {
    return util::CancelledError("");
  }
  return ReportFailure(
      "Close question mark expected in comment.",
      ParseErrorType::EXPECTED_CLOSE_QUESTION_MARK_IN_DECLARATION);
}

util::Status XmlStreamParser::ParseKey() {
  StringPiece original = p_;

  if (!ConsumeKey(&p_, &key_)) {
    return ReportFailure("Invalid key.", ParseErrorType::INVALID_KEY);
  }

  // If we consumed everything but expect more data, reset p_ and cancel since
  // we can't know if the key was complete or not.
  if (!finishing_ && p_.empty()) {
    p_ = original;
    return util::CancelledError("");
  }
  // Since we aren't using the key storage, clear it out.
  key_storage_.clear();
  return util::Status();
}

util::Status XmlStreamParser::ParseStartTagName() {
  util::Status result = ParseTagName();
  if (result.ok()) {
    StringPiece tag_name = tag_name_;
    bool is_list_object = false;
    if (tag_name_.starts_with("_list_")) {
      tag_name = tag_name_.substr(6);
      ow_->StartList(tag_name);
      element_type_stack_.push(LIST);
      is_list_object = true;
      tag_name_stack_.push(
          std::make_pair(tag_name.as_string(), is_list_object));
    } else {
      bool parent_is_list_object = false;
      if (!tag_name_stack_.empty()) {
        parent_is_list_object = tag_name_stack_.top().second;
      }

      util::Status status;
      if (tag_name != "anonymous") {
        if (tag_name == "root" || parent_is_list_object) {
          ow_->StartObject("");
        } else {
          ow_->StartObject(tag_name);
        }
      }
      element_type_stack_.push(OBJECT);
      status = IncrementRecursionDepth(tag_name);
      if (!status.ok()) {
        return status;
      }
      tag_name_stack_.push(
          std::make_pair(tag_name.as_string(), is_list_object));
    }

    tag_name_ = StringPiece();
    stack_.push(BEGIN_ELEMENT_MID);
  }
  return result;
}

util::Status XmlStreamParser::ParseEndTagName() {
  util::Status result = ParseTagName();
  if (result.ok()) {
    auto& [start_tag_name, _] = tag_name_stack_.top();
    StringPiece tag_name = tag_name_;
    if (start_tag_name == tag_name) {
      if (tag_name.starts_with("_list_")) {
        ow_->EndList();
        element_type_stack_.pop();
      } else {
        if (tag_name != "anonymous") {
          ow_->EndObject();
        }
        element_type_stack_.pop();
        --recursion_depth_;
      }
      tag_name_stack_.pop();
      ParseType type = stack_.top();
      GOOGLE_DCHECK_EQ(type, TEXT);
      stack_.pop();
      stack_.push(END_ELEMENT_CLOSE);
    }
  } else {
    return ReportFailure("Tag name not match.",
                         ParseErrorType::TAG_NAME_NOT_MATCH);
  }
  return result;
}

util::Status XmlStreamParser::ParseTagName() {
  StringPiece original = p_;

  if (!ConsumeTagName(&p_, &tag_name_)) {
    return ReportFailure("Invalid tag name.", ParseErrorType::INVALID_TAG_NAME);
  }

  // If we consumed everything but expect more data, reset p_ and cancel since
  // we can't know if the tag name was complete or not.
  if (!finishing_ && p_.empty()) {
    p_ = original;
    return util::CancelledError("");
  }

  return util::Status();
}

util::Status XmlStreamParser::ParseString() {
  util::Status result = ParseStringHelper();
  if (result.ok()) {
    ow_->RenderString(key_, parsed_);
    key_ = StringPiece();
    parsed_ = StringPiece();
    parsed_storage_.clear();
  }
  return result;
}

util::Status XmlStreamParser::ParseStringHelper() {
  // If we haven't seen the start quote, grab it and remember it for later.
  if (string_open_ == 0) {
    string_open_ = *p_.data();
    GOOGLE_DCHECK(string_open_ == '\"' || string_open_ == '\'');
    Advance();
  }
  // Track where we last copied data from so we can minimize copying.
  const char* last = p_.data();
  while (!p_.empty()) {
    const char* data = p_.data();
    if (*data == '\\') {
      // We're about to handle an escape, copy all bytes from last to data.
      if (last < data) {
        parsed_storage_.append(last, data - last);
      }
      // If we ran out of string after the \, cancel or report an error
      // depending on if we expect more data later.
      if (p_.length() == 1) {
        if (!finishing_) {
          return util::CancelledError("");
        }
        return ReportFailure("Closing quote expected in string.",
                             ParseErrorType::EXPECTED_CLOSING_QUOTE);
      }
      // Parse a unicode escape if we found \u in the string.
      if (data[1] == 'u') {
        util::Status result = ParseUnicodeEscape();
        if (!result.ok()) {
          return result;
        }
        // Move last pointer past the unicode escape and continue.
        last = p_.data();
        continue;
      }
      // Handle the standard set of backslash-escaped characters.
      switch (data[1]) {
        case 'b':
          parsed_storage_.push_back('\b');
          break;
        case 'f':
          parsed_storage_.push_back('\f');
          break;
        case 'n':
          parsed_storage_.push_back('\n');
          break;
        case 'r':
          parsed_storage_.push_back('\r');
          break;
        case 't':
          parsed_storage_.push_back('\t');
          break;
        case 'v':
          parsed_storage_.push_back('\v');
          break;
        default:
          parsed_storage_.push_back(data[1]);
      }
      // We handled two characters, so advance past them and continue.
      p_.remove_prefix(2);
      last = p_.data();
      continue;
    }
    // If we found the closing quote note it, advance past it, and return.
    if (*data == string_open_) {
      // If we didn't copy anything, reuse the input buffer.
      if (parsed_storage_.empty()) {
        parsed_ = StringPiece(last, data - last);
      } else {
        if (last < data) {
          parsed_storage_.append(last, data - last);
        }
        parsed_ = StringPiece(parsed_storage_);
      }
      // Clear the quote char so next time we try to parse a string we'll
      // start fresh.
      string_open_ = 0;
      Advance();
      return util::Status();
    }
    // Normal character, just advance past it.
    Advance();
  }
  // If we ran out of characters, copy over what we have so far.
  if (last < p_.data()) {
    parsed_storage_.append(last, p_.data() - last);
  }
  // If we didn't find the closing quote but we expect more data, cancel for
  // now
  if (!finishing_) {
    return util::CancelledError("");
  }
  // End of string reached without a closing quote, report an error.
  string_open_ = 0;
  return ReportFailure("Closing quote expected in string.",
                       ParseErrorType::EXPECTED_CLOSING_QUOTE);
}

// Converts a unicode escaped character to a decimal value stored in a char32
// for use in UTF8 encoding utility.  We assume that str begins with \uhhhh
// and convert that from the hex number to a decimal value.
//
// There are some security exploits with UTF-8 that we should be careful of:
//   - http://www.unicode.org/reports/tr36/#UTF-8_Exploit
//   - http://sites/intl-eng/design-guide/core-application
util::Status XmlStreamParser::ParseUnicodeEscape() {
  if (p_.length() < kUnicodeEscapedLength) {
    if (!finishing_) {
      return util::CancelledError("");
    }
    return ReportFailure("Illegal hex string.",
                         ParseErrorType::ILLEGAL_HEX_STRING);
  }
  GOOGLE_DCHECK_EQ('\\', p_.data()[0]);
  GOOGLE_DCHECK_EQ('u', p_.data()[1]);
  uint32_t code = 0;
  for (int i = 2; i < kUnicodeEscapedLength; ++i) {
    if (!isxdigit(p_.data()[i])) {
      return ReportFailure("Invalid escape sequence.",
                           ParseErrorType::INVALID_ESCAPE_SEQUENCE);
    }
    code = (code << 4) + hex_digit_to_int(p_.data()[i]);
  }
  if (code >= JsonEscaping::kMinHighSurrogate &&
      code <= JsonEscaping::kMaxHighSurrogate) {
    if (p_.length() < 2 * kUnicodeEscapedLength) {
      if (!finishing_) {
        return util::CancelledError("");
      }
      if (!coerce_to_utf8_) {
        return ReportFailure("Missing low surrogate.",
                             ParseErrorType::MISSING_LOW_SURROGATE);
      }
    } else if (p_.data()[kUnicodeEscapedLength] == '\\' &&
               p_.data()[kUnicodeEscapedLength + 1] == 'u') {
      uint32_t low_code = 0;
      for (int i = kUnicodeEscapedLength + 2; i < 2 * kUnicodeEscapedLength;
           ++i) {
        if (!isxdigit(p_.data()[i])) {
          return ReportFailure("Invalid escape sequence.",
                               ParseErrorType::INVALID_ESCAPE_SEQUENCE);
        }
        low_code = (low_code << 4) + hex_digit_to_int(p_.data()[i]);
      }
      if (low_code >= JsonEscaping::kMinLowSurrogate &&
          low_code <= JsonEscaping::kMaxLowSurrogate) {
        // Convert UTF-16 surrogate pair to 21-bit Unicode codepoint.
        code = (((code & 0x3FF) << 10) | (low_code & 0x3FF)) +
               JsonEscaping::kMinSupplementaryCodePoint;
        // Advance past the first code unit escape.
        p_.remove_prefix(kUnicodeEscapedLength);
      } else if (!coerce_to_utf8_) {
        return ReportFailure("Invalid low surrogate.",
                             ParseErrorType::INVALID_LOW_SURROGATE);
      }
    } else if (!coerce_to_utf8_) {
      return ReportFailure("Missing low surrogate.",
                           ParseErrorType::MISSING_LOW_SURROGATE);
    }
  }
  if (!coerce_to_utf8_ && !IsValidCodePoint(code)) {
    return ReportFailure("Invalid unicode code point.",
                         ParseErrorType::INVALID_UNICODE);
  }
  char buf[UTFmax];
  int len = EncodeAsUTF8Char(code, buf);
  // Advance past the [final] code unit escape.
  p_.remove_prefix(kUnicodeEscapedLength);
  parsed_storage_.append(buf, len);
  return util::Status();
}

util::Status XmlStreamParser::ReportFailure(StringPiece message,
                                            ParseErrorType parse_code) {
  (void)parse_code;  // Parameter is used in Google-internal code.
  static const int kContextLength = 20;
  const char* p_start = p_.data();
  const char* xml_start = xml_.data();
  const char* begin = std::max(p_start - kContextLength, xml_start);
  const char* end = std::min(p_start + kContextLength, xml_start + xml_.size());
  StringPiece segment(begin, end - begin);
  std::string location(p_start - begin, ' ');
  location.push_back('^');
  auto status = util::InvalidArgumentError(
      StrCat(message, "\n", segment, "\n", location));
  return status;
}

util::Status XmlStreamParser::ReportUnknown(StringPiece message,
                                            ParseErrorType parse_code) {
  // If we aren't finishing the parse, cancel parsing and try later.
  if (!finishing_) {
    return util::CancelledError("");
  }
  if (p_.empty()) {
    return ReportFailure(StrCat("Unexpected end of string. ", message),
                         parse_code);
  }
  return ReportFailure(message, parse_code);
}

util::Status XmlStreamParser::IncrementRecursionDepth(
    StringPiece tag_name) const {
  if (++recursion_depth_ > max_recursion_depth_) {
    return util::InvalidArgumentError(
        StrCat("Message too deep. Max recursion depth reached for tag '",
               tag_name, "'"));
  }
  return util::Status();
}

void XmlStreamParser::SkipWhitespace() {
  while (!p_.empty() && ascii_isspace(*p_.data())) {
    Advance();
  }
  if (!p_.empty() && !ascii_isspace(*p_.data())) {
    seen_non_whitespace_ = true;
  }
}

void XmlStreamParser::SkipWhitespace(ParseType type) {
  while (!p_.empty() && ascii_isspace(*p_.data())) {
    // reserve a space
    if (type == BEGIN_ELEMENT_MID) {
      if (p_.size() == 1) {
        seen_non_whitespace_ = true;
        break;
      } else if (p_.size() >= 2 && !ascii_isspace(p_.data()[1])) {
        seen_non_whitespace_ = true;
        break;
      }
    }
    Advance();
  }
  if (!p_.empty() && !ascii_isspace(*p_.data())) {
    seen_non_whitespace_ = true;
  }
}

void XmlStreamParser::Advance() {
  // Advance by moving one UTF8 character while making sure we don't go beyond
  // the length of StringPiece.
  p_.remove_prefix(std::min<int>(
      p_.length(), UTF8FirstLetterNumBytes(p_.data(), p_.length())));
}

XmlStreamParser::TokenType XmlStreamParser::GetNextTokenType(ParseType type) {
  SkipWhitespace(type);

  int size = p_.size();
  if (size == 0) {
    // If we ran out of data, report unknown and we'll place the previous
    // parse type onto the stack and try again when we have more data.
    return UNKNOWN;
  }
  // TODO(sven): Split this method based on context since different contexts
  // support different tokens. Would slightly speed up processing?
  const char* data = p_.data();
  if (*data == '\"' || *data == '\'') return BEGIN_STRING;
  if (*data == '<') return OPEN_TAG;
  if (*data == '>') return CLOSE_TAG;
  if (*data == '/') return END_TAG_SLASH;
  if (*data == '?') return DECLARATION;
  if (*data == '!') return COMMENT;
  if (*data == ' ') return ATTR_SEPARATOR;
  if (*data == '=') return ATTR_VALUE_SEPARATOR;
  if (MatchKey(p_)) {
    return BEGIN_KEY;
  }

  // We don't know that we necessarily have an invalid token here, just that
  // we can't parse what we have so far. So we don't report an error and just
  // return UNKNOWN so we can try again later when we have more data, or if we
  // finish and we have leftovers.
  return BEGIN_TEXT;
}

}  // namespace converter
}  // namespace util
}  // namespace protobuf
}  // namespace google

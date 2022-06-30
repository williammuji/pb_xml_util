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

#include <gmock/gmock.h>
#include <google/protobuf/descriptor_database.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/stubs/status.h>
#include <google/protobuf/stubs/status_macros.h>
#include <google/protobuf/stubs/statusor.h>
#include <google/protobuf/stubs/strutil.h>
#include <google/protobuf/util/internal/testdata/maps.pb.h>
#include <google/protobuf/util/json_format.pb.h>
#include <google/protobuf/util/json_format_proto3.pb.h>
#include <google/protobuf/util/type_resolver.h>
#include <google/protobuf/util/type_resolver_util.h>
#include <google/protobuf/util/xml_util.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// Must be included last.
#include <google/protobuf/port_def.inc>

namespace google {
namespace protobuf {
namespace util {
namespace {

using ::proto3::TestAny;
using ::proto3::TestEnumValue;
using ::proto3::TestMap;
using ::proto3::TestMessage;
using ::proto3::TestOneof;
using ::proto_util_converter::testing::MapIn;

// TODO(b/234474291): Use the gtest versions once that's available in OSS.
MATCHER_P(IsOkAndHolds, inner,
          StrCat("is OK and holds ", testing::PrintToString(inner))) {
  if (!arg.ok()) {
    *result_listener << arg.status();
    return false;
  }
  return testing::ExplainMatchResult(inner, *arg, result_listener);
}

util::Status GetStatus(const util::Status& s) { return s; }
template <typename T>
util::Status GetStatus(const util::StatusOr<T>& s) {
  return s.status();
}

MATCHER_P(StatusIs, status,
          StrCat(".status() is ", testing::PrintToString(status))) {
  return GetStatus(arg).code() == status;
}

#define EXPECT_OK(x) EXPECT_THAT(x, StatusIs(util::StatusCode::kOk))
#define ASSERT_OK(x) ASSERT_THAT(x, StatusIs(util::StatusCode::kOk))

// As functions defined in xml_util.h are just thin wrappers around the
// XML conversion code in //net/proto2/util/converter, in this test we
// only cover some very basic cases to make sure the wrappers have forwarded
// parameters to the underlying implementation correctly. More detailed
// tests are contained in the //net/proto2/util/converter directory.

util::StatusOr<std::string> ToXml(const Message& message,
                                  const XmlPrintOptions& options = {}) {
  std::string result;
  RETURN_IF_ERROR(MessageToXmlString(message, &result, options));
  std::cout << "result:" << result << std::endl;
  return result;
}

util::Status FromXml(StringPiece xml, Message* message,
                     const XmlParseOptions& options = {}) {
  return XmlStringToMessage(xml, message, options);
}

TEST(XmlUtilTest, TestWhitespaces) {
  TestMessage m;
  m.mutable_message_value();

  EXPECT_THAT(ToXml(m),
              IsOkAndHolds("<root><messageValue></messageValue></root>"));

  XmlPrintOptions options;
  options.add_whitespace = true;
  EXPECT_THAT(ToXml(m, options),
              IsOkAndHolds("<root>\n"
                           " <messageValue\></messageValue\>\n"
                           "</root>\n"));
}

TEST(XmlUtilTest, TestDefaultValues) {
  TestMessage m;
  EXPECT_THAT(ToXml(m), IsOkAndHolds("<root></root>"));

  XmlPrintOptions options;
  options.always_print_primitive_fields = true;
  EXPECT_THAT(ToXml(m, options), IsOkAndHolds("<root boolValue=\"false\""
                                              " int32Value=\"0\""
                                              " int64Value=\"0\""
                                              " uint32Value=\"0\""
                                              " uint64Value=\"0\""
                                              " floatValue=\"0\""
                                              " doubleValue=\"0\""
                                              " stringValue=\"\""
                                              " bytesValue=\"\""
                                              " enumValue=\"FOO\""
                                              ">"
                                              "<_list_repeatedBoolValue>"
                                              "</_list_repeatedBoolValue>"
                                              "<_list_repeatedInt32Value>"
                                              "</_list_repeatedInt32Value>"
                                              "<_list_repeatedInt64Value>"
                                              "</_list_repeatedInt64Value>"
                                              "<_list_repeatedUint32Value>"
                                              "</_list_repeatedUint32Value>"
                                              "<_list_repeatedUint64Value>"
                                              "</_list_repeatedUint64Value>"
                                              "<_list_repeatedFloatValue>"
                                              "</_list_repeatedFloatValue>"
                                              "<_list_repeatedDoubleValue>"
                                              "</_list_repeatedDoubleValue>"
                                              "<_list_repeatedStringValue>"
                                              "</_list_repeatedStringValue>"
                                              "<_list_repeatedBytesValue>"
                                              "</_list_repeatedBytesValue>"
                                              "<_list_repeatedEnumValue>"
                                              "</_list_repeatedEnumValue>"
                                              "<_list_repeatedMessageValue>"
                                              "</_list_repeatedMessageValue>"
                                              "</root>"));

  options.always_print_primitive_fields = true;
  m.set_string_value("i am a test string value");
  m.set_bytes_value("i am a test bytes value");
  EXPECT_THAT(
      ToXml(m, options),
      IsOkAndHolds("<root boolValue=\"false\""
                   " int32Value=\"0\""
                   " int64Value=\"0\""
                   " uint32Value=\"0\""
                   " uint64Value=\"0\""
                   " floatValue=\"0\""
                   " doubleValue=\"0\""
                   " stringValue=\"i am a test string value\""
                   " bytesValue=\"aSBhbSBhIHRlc3QgYnl0ZXMgdmFsdWU=\""
                   " enumValue=\"FOO\">"
                   "<_list_repeatedBoolValue></_list_repeatedBoolValue>"
                   "<_list_repeatedInt32Value></_list_repeatedInt32Value>"
                   "<_list_repeatedInt64Value></_list_repeatedInt64Value>"
                   "<_list_repeatedUint32Value></_list_repeatedUint32Value>"
                   "<_list_repeatedUint64Value></_list_repeatedUint64Value>"
                   "<_list_repeatedFloatValue></_list_repeatedFloatValue>"
                   "<_list_repeatedDoubleValue></_list_repeatedDoubleValue>"
                   "<_list_repeatedStringValue></_list_repeatedStringValue>"
                   "<_list_repeatedBytesValue></_list_repeatedBytesValue>"
                   "<_list_repeatedEnumValue></_list_repeatedEnumValue>"
                   "<_list_repeatedMessageValue></_list_repeatedMessageValue>"
                   "</root>"));

  options.preserve_proto_field_names = true;
  m.set_string_value("i am a test string value");
  m.set_bytes_value("i am a test bytes value");
  EXPECT_THAT(
      ToXml(m, options),
      IsOkAndHolds(
          "<root bool_value=\"false\""
          " int32_value=\"0\""
          " int64_value=\"0\""
          " uint32_value=\"0\""
          " uint64_value=\"0\""
          " float_value=\"0\""
          " double_value=\"0\""
          " string_value=\"i am a test string value\""
          " bytes_value=\"aSBhbSBhIHRlc3QgYnl0ZXMgdmFsdWU=\""
          " enum_value=\"FOO\">"
          "<_list_repeated_bool_value></_list_repeated_bool_value>"
          "<_list_repeated_int32_value></_list_repeated_int32_value>"
          "<_list_repeated_int64_value></_list_repeated_int64_value>"
          "<_list_repeated_uint32_value></_list_repeated_uint32_value>"
          "<_list_repeated_uint64_value></_list_repeated_uint64_value>"
          "<_list_repeated_float_value></_list_repeated_float_value>"
          "<_list_repeated_double_value></_list_repeated_double_value>"
          "<_list_repeated_string_value></_list_repeated_string_value>"
          "<_list_repeated_bytes_value></_list_repeated_bytes_value>"
          "<_list_repeated_enum_value></_list_repeated_enum_value>"
          "<_list_repeated_message_value></_list_repeated_message_value>"
          "</root>"));
}

TEST(XmlUtilTest, TestPreserveProtoFieldNames) {
  TestMessage m;
  m.mutable_message_value();

  XmlPrintOptions options;
  options.preserve_proto_field_names = true;
  EXPECT_THAT(ToXml(m, options),
              IsOkAndHolds("<root><message_value></message_value></root>"));
}

TEST(XmlUtilTest, TestAlwaysPrintEnumsAsInts) {
  TestMessage orig;
  orig.set_enum_value(proto3::BAR);
  orig.add_repeated_enum_value(proto3::FOO);
  orig.add_repeated_enum_value(proto3::BAR);

  XmlPrintOptions print_options;
  print_options.always_print_enums_as_ints = true;

  auto printed = ToXml(orig, print_options);
  ASSERT_THAT(
      printed,
      IsOkAndHolds("<root "
                   "enumValue=\"1\"><_list_repeatedEnumValue><anonymous>"
                   "0</anonymous><anonymous>1</"
                   "anonymous></_list_repeatedEnumValue></root>"));

  TestMessage parsed;
  ASSERT_OK(FromXml(*printed, &parsed));

  EXPECT_EQ(parsed.enum_value(), proto3::BAR);
  EXPECT_EQ(parsed.repeated_enum_value_size(), 2);
  EXPECT_EQ(parsed.repeated_enum_value(0), proto3::FOO);
  EXPECT_EQ(parsed.repeated_enum_value(1), proto3::BAR);
}

TEST(XmlUtilTest, TestPrintEnumsAsIntsWithDefaultValue) {
  TestEnumValue orig;
  // orig.set_enum_value1(proto3::FOO)
  orig.set_enum_value2(proto3::FOO);
  orig.set_enum_value3(proto3::BAR);

  XmlPrintOptions print_options;
  print_options.always_print_enums_as_ints = true;
  print_options.always_print_primitive_fields = true;

  auto printed = ToXml(orig, print_options);
  ASSERT_THAT(
      printed,
      IsOkAndHolds(
          "<root enumValue1=\"0\" enumValue2=\"0\" enumValue3=\"1\"></root>"));

  TestEnumValue parsed;
  ASSERT_OK(FromXml(*printed, &parsed));

  EXPECT_EQ(parsed.enum_value1(), proto3::FOO);
  EXPECT_EQ(parsed.enum_value2(), proto3::FOO);
  EXPECT_EQ(parsed.enum_value3(), proto3::BAR);
}

TEST(XmlUtilTest, TestPrintProto2EnumAsIntWithDefaultValue) {
  protobuf_unittest::TestDefaultEnumValue orig;

  XmlPrintOptions print_options;
  // use enum as int
  print_options.always_print_enums_as_ints = true;
  print_options.always_print_primitive_fields = true;

  // result should be int rather than string
  auto printed = ToXml(orig, print_options);
  ASSERT_THAT(printed, IsOkAndHolds("<root enumValue=\"2\"></root>"));

  protobuf_unittest::TestDefaultEnumValue parsed;
  ASSERT_OK(FromXml(*printed, &parsed));

  EXPECT_EQ(parsed.enum_value(), protobuf_unittest::DEFAULT);
}

TEST(XmlUtilTest, ParseMessage) {
  // Some random message but good enough to verify that the parsing wrapper
  // functions are working properly.
  TestMessage m;
  XmlParseOptions options;
  ASSERT_OK(FromXml(
      R"xml(
    <root int32Value="1234567891" int64Value="5302428716536692736" floatValue="3.402823466e+38">
      <_list_repeatedInt32Value>
        <anonymous>1</anonymous>
        <anonymous>2</anonymous>
      </_list_repeatedInt32Value>
      <messageValue value="2048"></messageValue>
      <_list_repeatedMessageValue>
        <repeatedMessageValue value="40"></repeatedMessageValue>
        <repeatedMessageValue value="96"></repeatedMessageValue>
      </_list_repeatedMessageValue>
    </root>
    )xml",
      &m, options));

  EXPECT_EQ(m.int32_value(), 1234567891);
  EXPECT_EQ(m.int64_value(), 5302428716536692736);
  EXPECT_EQ(m.float_value(), 3.402823466e+38f);
  ASSERT_EQ(m.repeated_int32_value_size(), 2);
  EXPECT_EQ(m.repeated_int32_value(0), 1);
  EXPECT_EQ(m.repeated_int32_value(1), 2);
  EXPECT_EQ(m.message_value().value(), 2048);
  ASSERT_EQ(m.repeated_message_value_size(), 2);
  EXPECT_EQ(m.repeated_message_value(0).value(), 40);
  EXPECT_EQ(m.repeated_message_value(1).value(), 96);
}

TEST(XmlUtilTest, ParseMap) {
  TestMap message;
  (*message.mutable_string_map())["hello"] = 1234;
  auto printed = ToXml(message);
  ASSERT_THAT(
      printed,
      IsOkAndHolds("<root><stringMap hello=\"1234\"></stringMap></root>"));

  TestMap other;
  ASSERT_OK(FromXml(*printed, &other));
  EXPECT_EQ(other.DebugString(), message.DebugString());
}

TEST(XmlUtilTest, ParsePrimitiveMapIn) {
  MapIn message;
  XmlPrintOptions print_options;
  print_options.always_print_primitive_fields = true;
  auto printed = ToXml(message, print_options);
  ASSERT_THAT(
      ToXml(message, print_options),
      IsOkAndHolds("<root "
                   "other=\"\"><_list_things></_list_things><mapInput></"
                   "mapInput><mapAny></mapAny></root>"));

  MapIn other;
  ASSERT_OK(FromXml(*printed, &other));
  EXPECT_EQ(other.DebugString(), message.DebugString());
}

TEST(XmlUtilTest, PrintPrimitiveOneof) {
  TestOneof message;
  XmlPrintOptions options;
  options.always_print_primitive_fields = true;
  message.mutable_oneof_message_value();
  EXPECT_THAT(
      ToXml(message, options),
      IsOkAndHolds(
          "<root><oneofMessageValue value=\"0\"></oneofMessageValue></root>"));

  message.set_oneof_int32_value(1);
  EXPECT_THAT(ToXml(message, options),
              IsOkAndHolds("<root oneofInt32Value=\"1\"></root>"));
}

TEST(XmlUtilTest, TestParseIgnoreUnknownFields) {
  TestMessage m;
  XmlParseOptions options;
  options.ignore_unknown_fields = true;
  EXPECT_OK(FromXml("<root unknownName=\"0\"></root>", &m, options));
}

TEST(XmlUtilTest, TestParseErrors) {
  TestMessage m;
  // Parsing should fail if the field name can not be recognized.
  EXPECT_THAT(FromXml(R"xml(<root unknownName="0"></root>)xml", &m),
              StatusIs(util::StatusCode::kInvalidArgument));
  // Parsing should fail if the value is invalid.
  EXPECT_THAT(FromXml(R"xml(<root int32Value="2147483648"></root>)xml", &m),
              StatusIs(util::StatusCode::kInvalidArgument));
}

TEST(XmlUtilTest, TestDynamicMessage) {
  // Create a new DescriptorPool with the same protos as the generated one.
  DescriptorPoolDatabase database(*DescriptorPool::generated_pool());
  DescriptorPool pool(&database);
  // A dynamic version of the test proto.
  DynamicMessageFactory factory;
  std::unique_ptr<Message> message(
      factory.GetPrototype(pool.FindMessageTypeByName("proto3.TestMessage"))
          ->New());
  EXPECT_OK(FromXml(R"xml(
    <root int32Value="1024">
      <_list_repeatedInt32Value>
        <anonymous>1</anonymous>
        <anonymous>2</anonymous>
      </_list_repeatedInt32Value>
      <messageValue value="2048"></messageValue>
      <_list_repeatedMessageValue>
          <repeatedMessageValue value="40"></repeatedMessageValue>
          <repeatedMessageValue value="96"></repeatedMessageValue>
      </_list_repeatedMessageValue>
    </root> 
  )xml",
                    message.get()));

  // Convert to generated message for easy inspection.
  TestMessage generated;
  EXPECT_TRUE(generated.ParseFromString(message->SerializeAsString()));
  EXPECT_EQ(generated.int32_value(), 1024);
  ASSERT_EQ(generated.repeated_int32_value_size(), 2);
  EXPECT_EQ(generated.repeated_int32_value(0), 1);
  EXPECT_EQ(generated.repeated_int32_value(1), 2);
  EXPECT_EQ(generated.message_value().value(), 2048);
  ASSERT_EQ(generated.repeated_message_value_size(), 2);
  EXPECT_EQ(generated.repeated_message_value(0).value(), 40);
  EXPECT_EQ(generated.repeated_message_value(1).value(), 96);

  auto message_xml = ToXml(*message);
  ASSERT_OK(message_xml);
  auto generated_xml = ToXml(generated);
  ASSERT_OK(generated_xml);
  EXPECT_EQ(*message_xml, *generated_xml);
}

TEST(XmlUtilTest, TestParsingUnknownAnyFields) {
  StringPiece input = R"xml(
    <root>
      <value @type="type.googleapis.com/proto3.TestMessage" unknown_field="UNKNOWN_VALUE" string_value="expected_value"></value>
    </root>
  )xml";

  TestAny m;
  EXPECT_THAT(FromXml(input, &m), StatusIs(util::StatusCode::kInvalidArgument));

  XmlParseOptions options;
  options.ignore_unknown_fields = true;
  EXPECT_OK(FromXml(input, &m, options));

  TestMessage t;
  EXPECT_TRUE(m.value().UnpackTo(&t));
  EXPECT_EQ(t.string_value(), "expected_value");
}

TEST(XmlUtilTest, TestParsingUnknownEnumsProto2) {
  StringPiece input = R"xml(<root a="UNKNOWN_VALUE"></root>)xml";
  protobuf_unittest::TestNumbers m;
  XmlParseOptions options;
  EXPECT_THAT(FromXml(input, &m, options),
              StatusIs(util::StatusCode::kInvalidArgument));

  options.ignore_unknown_fields = true;
  EXPECT_OK(FromXml(input, &m, options));
  EXPECT_FALSE(m.has_a());
}

TEST(XmlUtilTest, TestParsingUnknownEnumsProto3) {
  TestMessage m;
  StringPiece input = R"xml(<root enum_value="UNKNOWN_VALUE"></root>)xml";

  m.set_enum_value(proto3::BAR);
  EXPECT_THAT(FromXml(input, &m), StatusIs(util::StatusCode::kInvalidArgument));
  ASSERT_EQ(m.enum_value(), proto3::BAR);  // Keep previous value

  XmlParseOptions options;
  options.ignore_unknown_fields = true;
  EXPECT_OK(FromXml(input, &m, options));
  EXPECT_EQ(m.enum_value(), 0);  // Unknown enum value must be decoded as 0
}

TEST(XmlUtilTest, TestParsingUnknownEnumsProto3FromInt) {
  TestMessage m;
  StringPiece input = R"xml(<root enum_value="1"></root>)xml";

  m.set_enum_value(proto3::BAR);
  EXPECT_OK(FromXml(input, &m));
  ASSERT_EQ(m.enum_value(), 1);

  XmlParseOptions options;
  options.ignore_unknown_fields = true;
  EXPECT_OK(FromXml(input, &m, options));
  EXPECT_EQ(m.enum_value(), 1);
}

// Trying to pass an object as an enum field value is always treated as an
// error
TEST(XmlUtilTest, TestParsingUnknownEnumsProto3FromObject) {
  TestMessage m;
  StringPiece input = R"xml(<root><enum_value></enum_value></root>)xml";

  XmlParseOptions options;
  options.ignore_unknown_fields = true;
  EXPECT_THAT(FromXml(input, &m, options),
              StatusIs(util::StatusCode::kInvalidArgument));

  EXPECT_THAT(FromXml(input, &m), StatusIs(util::StatusCode::kInvalidArgument));
}

TEST(XmlUtilTest, TestParsingUnknownEnumsProto3FromArray) {
  TestMessage m;
  StringPiece input =
      R"xml(<root><_list_enum_value></_list_enum_value></root>)xml";

  EXPECT_THAT(FromXml(input, &m), StatusIs(util::StatusCode::kInvalidArgument));

  XmlParseOptions options;
  options.ignore_unknown_fields = true;
  EXPECT_THAT(FromXml(input, &m, options),
              StatusIs(util::StatusCode::kInvalidArgument));
}

TEST(XmlUtilTest, TestParsingEnumCaseSensitive) {
  TestMessage m;

  StringPiece input = R"xml(<root enum_value="bar"></root>)xml";

  m.set_enum_value(proto3::FOO);
  EXPECT_THAT(FromXml(input, &m), StatusIs(util::StatusCode::kInvalidArgument));
  // Default behavior is case-sensitive, so keep previous value.
  ASSERT_EQ(m.enum_value(), proto3::FOO);
}

TEST(XmlUtilTest, TestParsingEnumIgnoreCase) {
  TestMessage m;
  StringPiece input = R"xml(<root enum_value="bar"></root>)xml";

  m.set_enum_value(proto3::FOO);
  XmlParseOptions options;
  options.case_insensitive_enum_parsing = true;
  EXPECT_OK(FromXml(input, &m, options));
  ASSERT_EQ(m.enum_value(), proto3::BAR);
}

// A ZeroCopyOutputStream that writes to multiple buffers.
class SegmentedZeroCopyOutputStream : public io::ZeroCopyOutputStream {
 public:
  explicit SegmentedZeroCopyOutputStream(std::vector<StringPiece> segments)
      : segments_(segments) {
    // absl::c_* functions are not cloned in OSS.
    std::reverse(segments_.begin(), segments_.end());
  }

  bool Next(void** buffer, int* length) override {
    if (segments_.empty()) {
      return false;
    }
    last_segment_ = segments_.back();
    segments_.pop_back();
    // TODO(b/234159981): This is only ever constructed in test code, and only
    // from non-const bytes, so this is a valid cast. We need to do this since
    // OSS proto does not yet have absl::Span; once we take a full Abseil
    // dependency we should use that here instead.
    *buffer = const_cast<char*>(last_segment_.data());
    *length = static_cast<int>(last_segment_.size());
    byte_count_ += static_cast<int64_t>(last_segment_.size());
    return true;
  }

  void BackUp(int length) override {
    GOOGLE_CHECK(length <= static_cast<int>(last_segment_.size()));

    size_t backup = last_segment_.size() - static_cast<size_t>(length);
    segments_.push_back(last_segment_.substr(backup));
    last_segment_ = last_segment_.substr(0, backup);
    byte_count_ -= static_cast<int64_t>(length);
  }

  int64_t ByteCount() const override { return byte_count_; }

 private:
  std::vector<StringPiece> segments_;
  StringPiece last_segment_;
  int64_t byte_count_ = 0;
};

// This test splits the output buffer and also the input data into multiple
// segments and checks that the implementation of ZeroCopyStreamByteSink
// handles all possible cases correctly.
TEST(ZeroCopyStreamByteSinkTest, TestAllInputOutputPatterns) {
  static constexpr int kOutputBufferLength = 10;
  // An exhaustive test takes too long, skip some combinations to make the test
  // run faster.
  static constexpr int kSkippedPatternCount = 7;

  char buffer[kOutputBufferLength];
  for (int split_pattern = 0; split_pattern < (1 << (kOutputBufferLength - 1));
       split_pattern += kSkippedPatternCount) {
    // Split the buffer into small segments according to the split_pattern.
    std::vector<StringPiece> segments;
    int segment_start = 0;
    for (int i = 0; i < kOutputBufferLength - 1; ++i) {
      if (split_pattern & (1 << i)) {
        segments.push_back(
            StringPiece(buffer + segment_start, i - segment_start + 1));
        segment_start = i + 1;
      }
    }
    segments.push_back(StringPiece(buffer + segment_start,
                                   kOutputBufferLength - segment_start));

    // Write exactly 10 bytes through the ByteSink.
    std::string input_data = "0123456789";
    for (int input_pattern = 0; input_pattern < (1 << (input_data.size() - 1));
         input_pattern += kSkippedPatternCount) {
      memset(buffer, 0, sizeof(buffer));
      {
        SegmentedZeroCopyOutputStream output_stream(segments);
        xml_internal::ZeroCopyStreamByteSink byte_sink(&output_stream);
        int start = 0;
        for (int j = 0; j < input_data.length() - 1; ++j) {
          if (input_pattern & (1 << j)) {
            byte_sink.Append(&input_data[start], j - start + 1);
            start = j + 1;
          }
        }
        byte_sink.Append(&input_data[start], input_data.length() - start);
      }
      EXPECT_EQ(std::string(buffer, input_data.length()), input_data);
    }

    // Write only 9 bytes through the ByteSink.
    input_data = "012345678";
    for (int input_pattern = 0; input_pattern < (1 << (input_data.size() - 1));
         input_pattern += kSkippedPatternCount) {
      memset(buffer, 0, sizeof(buffer));
      {
        SegmentedZeroCopyOutputStream output_stream(segments);
        xml_internal::ZeroCopyStreamByteSink byte_sink(&output_stream);
        int start = 0;
        for (int j = 0; j < input_data.length() - 1; ++j) {
          if (input_pattern & (1 << j)) {
            byte_sink.Append(&input_data[start], j - start + 1);
            start = j + 1;
          }
        }
        byte_sink.Append(&input_data[start], input_data.length() - start);
      }
      EXPECT_EQ(std::string(buffer, input_data.length()), input_data);
      EXPECT_EQ(buffer[input_data.length()], 0);
    }

    // Write 11 bytes through the ByteSink. The extra byte will just
    // be ignored.
    input_data = "0123456789A";
    for (int input_pattern = 0; input_pattern < (1 << (input_data.size() - 1));
         input_pattern += kSkippedPatternCount) {
      memset(buffer, 0, sizeof(buffer));
      {
        SegmentedZeroCopyOutputStream output_stream(segments);
        xml_internal::ZeroCopyStreamByteSink byte_sink(&output_stream);
        int start = 0;
        for (int j = 0; j < input_data.length() - 1; ++j) {
          if (input_pattern & (1 << j)) {
            byte_sink.Append(&input_data[start], j - start + 1);
            start = j + 1;
          }
        }
        byte_sink.Append(&input_data[start], input_data.length() - start);
      }
      EXPECT_EQ(input_data.substr(0, kOutputBufferLength),
                std::string(buffer, kOutputBufferLength));
    }
  }
}

TEST(XmlUtilTest, TestWrongXmlInput) {
  StringPiece xml = "<root unknown_field=\"some_value\"></root>";
  io::ArrayInputStream input_stream(xml.data(), xml.size());
  char proto_buffer[10000];

  io::ArrayOutputStream output_stream(proto_buffer, sizeof(proto_buffer));
  std::string message_type = "type.googleapis.com/proto3.TestMessage";

  auto* resolver = NewTypeResolverForDescriptorPool(
      "type.googleapis.com", DescriptorPool::generated_pool());

  EXPECT_THAT(
      XmlToBinaryStream(resolver, message_type, &input_stream, &output_stream),
      StatusIs(util::StatusCode::kInvalidArgument));
  delete resolver;
}

TEST(XmlUtilTest, HtmlEscape) {
  TestMessage m;
  m.set_string_value("</script>");
  XmlPrintOptions options;
  EXPECT_THAT(
      ToXml(m, options),
      IsOkAndHolds("<root stringValue=\"\\u003c/script\\u003e\"></root>"));
}

}  // namespace
}  // namespace util
}  // namespace protobuf
}  // namespace google

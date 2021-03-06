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

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/stubs/bytestream.h>
#include <google/protobuf/stubs/common.h>
#include <google/protobuf/stubs/once.h>
#include <google/protobuf/stubs/status.h>
#include <google/protobuf/stubs/status_macros.h>
#include <google/protobuf/stubs/strutil.h>
#include <google/protobuf/util/internal/default_value_objectwriter.h>
#include <google/protobuf/util/internal/error_listener.h>
#include <google/protobuf/util/internal/protostream_objectsource.h>
#include <google/protobuf/util/internal/protostream_objectwriter.h>
#include <google/protobuf/util/internal/xml_objectwriter.h>
#include <google/protobuf/util/internal/xml_stream_parser.h>
#include <google/protobuf/util/type_resolver.h>
#include <google/protobuf/util/type_resolver_util.h>
#include <google/protobuf/util/xml_util.h>

// clang-format off
#include <google/protobuf/port_def.inc>
// clang-format on

namespace google {
namespace protobuf {
namespace util {

namespace xml_internal {
ZeroCopyStreamByteSink::~ZeroCopyStreamByteSink() {
  if (buffer_size_ > 0) {
    stream_->BackUp(buffer_size_);
  }
}

void ZeroCopyStreamByteSink::Append(const char* bytes, size_t len) {
  while (true) {
    if (len <= buffer_size_) {  // NOLINT
      memcpy(buffer_, bytes, len);
      buffer_ = static_cast<char*>(buffer_) + len;
      buffer_size_ -= len;
      return;
    }
    if (buffer_size_ > 0) {
      memcpy(buffer_, bytes, buffer_size_);
      bytes += buffer_size_;
      len -= buffer_size_;
    }
    if (!stream_->Next(&buffer_, &buffer_size_)) {
      // There isn't a way for ByteSink to report errors.
      buffer_size_ = 0;
      return;
    }
  }
}
}  // namespace xml_internal

util::Status BinaryToXmlStream(TypeResolver* resolver,
                               const std::string& type_url,
                               io::ZeroCopyInputStream* binary_input,
                               io::ZeroCopyOutputStream* xml_output,
                               const XmlPrintOptions& options) {
  io::CodedInputStream in_stream(binary_input);
  google::protobuf::Type type;
  RETURN_IF_ERROR(resolver->ResolveMessageType(type_url, &type));
  converter::ProtoStreamObjectSource::RenderOptions render_options;
  render_options.use_ints_for_enums = options.always_print_enums_as_ints;
  render_options.preserve_proto_field_names =
      options.preserve_proto_field_names;
  converter::ProtoStreamObjectSource proto_source(&in_stream, resolver, type,
                                                  render_options);
  io::CodedOutputStream out_stream(xml_output);
  converter::XmlObjectWriter xml_writer(options.add_whitespace ? " " : "",
                                        &out_stream);
  if (options.always_print_primitive_fields) {
    converter::DefaultValueObjectWriter default_value_writer(resolver, type,
                                                             &xml_writer);
    default_value_writer.set_preserve_proto_field_names(
        options.preserve_proto_field_names);
    default_value_writer.set_print_enums_as_ints(
        options.always_print_enums_as_ints);
    return proto_source.WriteTo(&default_value_writer);
  } else {
    return proto_source.WriteTo(&xml_writer);
  }
}

util::Status BinaryToXmlString(TypeResolver* resolver,
                               const std::string& type_url,
                               const std::string& binary_input,
                               std::string* xml_output,
                               const XmlPrintOptions& options) {
  io::ArrayInputStream input_stream(binary_input.data(), binary_input.size());
  io::StringOutputStream output_stream(xml_output);
  return BinaryToXmlStream(resolver, type_url, &input_stream, &output_stream,
                           options);
}

namespace {
class StatusErrorListener : public converter::ErrorListener {
 public:
  StatusErrorListener() {}
  ~StatusErrorListener() override {}

  util::Status GetStatus() { return status_; }

  void InvalidName(const converter::LocationTrackerInterface& loc,
                   StringPiece unknown_name, StringPiece message) override {
    std::string loc_string = GetLocString(loc);
    if (!loc_string.empty()) {
      loc_string.append(" ");
    }
    status_ = util::InvalidArgumentError(
        StrCat(loc_string, unknown_name, ": ", message));
  }

  void InvalidValue(const converter::LocationTrackerInterface& loc,
                    StringPiece type_name, StringPiece value) override {
    status_ = util::InvalidArgumentError(
        StrCat(GetLocString(loc), ": invalid value ", std::string(value),
               " for type ", std::string(type_name)));
  }

  void MissingField(const converter::LocationTrackerInterface& loc,
                    StringPiece missing_name) override {
    status_ = util::InvalidArgumentError(StrCat(
        GetLocString(loc), ": missing field ", std::string(missing_name)));
  }

 private:
  util::Status status_;

  std::string GetLocString(const converter::LocationTrackerInterface& loc) {
    std::string loc_string = loc.ToString();
    StripWhitespace(&loc_string);
    if (!loc_string.empty()) {
      loc_string = StrCat("(", loc_string, ")");
    }
    return loc_string;
  }

  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(StatusErrorListener);
};
}  // namespace

util::Status XmlToBinaryStream(TypeResolver* resolver,
                               const std::string& type_url,
                               io::ZeroCopyInputStream* xml_input,
                               io::ZeroCopyOutputStream* binary_output,
                               const XmlParseOptions& options) {
  google::protobuf::Type type;
  RETURN_IF_ERROR(resolver->ResolveMessageType(type_url, &type));
  xml_internal::ZeroCopyStreamByteSink sink(binary_output);
  StatusErrorListener listener;
  converter::ProtoStreamObjectWriter::Options proto_writer_options;
  proto_writer_options.ignore_unknown_fields = options.ignore_unknown_fields;
  proto_writer_options.ignore_unknown_enum_values =
      options.ignore_unknown_fields;
  proto_writer_options.case_insensitive_enum_parsing =
      options.case_insensitive_enum_parsing;
  converter::ProtoStreamObjectWriter proto_writer(
      resolver, type, &sink, &listener, proto_writer_options);

  converter::XmlStreamParser parser(&proto_writer);
  const void* buffer;
  int length;
  while (xml_input->Next(&buffer, &length)) {
    if (length == 0) continue;
    RETURN_IF_ERROR(
        parser.Parse(StringPiece(static_cast<const char*>(buffer), length)));
  }
  RETURN_IF_ERROR(parser.FinishParse());

  return listener.GetStatus();
}

util::Status XmlToBinaryString(TypeResolver* resolver,
                               const std::string& type_url,
                               StringPiece xml_input,
                               std::string* binary_output,
                               const XmlParseOptions& options) {
  io::ArrayInputStream input_stream(xml_input.data(), xml_input.size());
  io::StringOutputStream output_stream(binary_output);
  return XmlToBinaryStream(resolver, type_url, &input_stream, &output_stream,
                           options);
}

namespace {
const char* kTypeUrlPrefix = "type.googleapis.com";
TypeResolver* generated_type_resolver_ = nullptr;
PROTOBUF_NAMESPACE_ID::internal::once_flag generated_type_resolver_init_;

std::string GetTypeUrl(const Message& message) {
  return std::string(kTypeUrlPrefix) + "/" +
         message.GetDescriptor()->full_name();
}

void DeleteGeneratedTypeResolver() {  // NOLINT
  delete generated_type_resolver_;
}

void InitGeneratedTypeResolver() {
  generated_type_resolver_ = NewTypeResolverForDescriptorPool(
      kTypeUrlPrefix, DescriptorPool::generated_pool());
  ::google::protobuf::internal::OnShutdown(&DeleteGeneratedTypeResolver);
}

TypeResolver* GetGeneratedTypeResolver() {
  PROTOBUF_NAMESPACE_ID::internal::call_once(generated_type_resolver_init_,
                                             InitGeneratedTypeResolver);
  return generated_type_resolver_;
}
}  // namespace

util::Status MessageToXmlString(const Message& message, std::string* output,
                                const XmlOptions& options) {
  const DescriptorPool* pool = message.GetDescriptor()->file()->pool();
  TypeResolver* resolver =
      pool == DescriptorPool::generated_pool()
          ? GetGeneratedTypeResolver()
          : NewTypeResolverForDescriptorPool(kTypeUrlPrefix, pool);
  util::Status result =
      BinaryToXmlString(resolver, GetTypeUrl(message),
                        message.SerializeAsString(), output, options);
  if (pool != DescriptorPool::generated_pool()) {
    delete resolver;
  }
  return result;
}

util::Status XmlStringToMessage(StringPiece input, Message* message,
                                const XmlParseOptions& options) {
  const DescriptorPool* pool = message->GetDescriptor()->file()->pool();
  TypeResolver* resolver =
      pool == DescriptorPool::generated_pool()
          ? GetGeneratedTypeResolver()
          : NewTypeResolverForDescriptorPool(kTypeUrlPrefix, pool);
  std::string binary;
  util::Status result = XmlToBinaryString(resolver, GetTypeUrl(*message), input,
                                          &binary, options);
  if (result.ok() && !message->ParseFromString(binary)) {
    result = util::InvalidArgumentError(
        "XML transcoder produced invalid protobuf output.");
  }
  if (pool != DescriptorPool::generated_pool()) {
    delete resolver;
  }
  return result;
}

}  // namespace util
}  // namespace protobuf
}  // namespace google

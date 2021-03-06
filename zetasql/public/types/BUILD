#
# Copyright 2019 ZetaSQL Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

package(default_visibility = ["//:__subpackages__"])

cc_library(
    name = "types",
    srcs = [
        "array_type.cc",
        "enum_type.cc",
        "internal_utils.cc",
        "internal_utils.h",
        "proto_type.cc",
        "simple_type.cc",
        "struct_type.cc",
        "type.cc",
        "type_factory.cc",
    ],
    hdrs = [
        "array_type.h",
        "enum_type.h",
        "proto_type.h",
        "simple_type.h",
        "struct_type.h",
        "type.h",
        "type_factory.h",
    ],
    copts = ["-Wno-sign-compare"],
    deps = [
        "//zetasql/base",
        "//zetasql/base:case",
        "//zetasql/base:cleanup",
        "//zetasql/base:map_util",
        "//zetasql/base:status",
        "//zetasql/common:errors",
        "//zetasql/common:proto_helper",
        "//zetasql/public:language_options",
        "//zetasql/public:options_cc_proto",
        "//zetasql/public:strings",
        "//zetasql/public:type_annotation_cc_proto",
        "//zetasql/public:type_cc_proto",
        "//zetasql/public/functions:datetime_cc_proto",
        "//zetasql/public/functions:normalize_mode_cc_proto",
        "//zetasql/public/proto:type_annotation_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:cc_wkt_protos",
        "@com_google_protobuf//:protobuf",
    ],
)

//
// Copyright 2019 ZetaSQL Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

// Tests for the ZetaSQL JSON functions.

#include "zetasql/public/functions/json.h"

#include <stddef.h>

#include <cctype>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "zetasql/base/logging.h"
#include "zetasql/common/testing/proto_matchers.h"
#include "zetasql/base/testing/status_matchers.h"
#include "zetasql/compliance/functions_testlib.h"
#include "zetasql/public/functions/json_internal.h"
#include "zetasql/public/value.h"
#include "zetasql/testing/test_function.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "zetasql/base/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/substitute.h"
#include "zetasql/base/status_macros.h"
#include "zetasql/base/statusor.h"

namespace zetasql {
namespace functions {
namespace {

using ::testing::ElementsAreArray;
using ::testing::HasSubstr;
using ::zetasql_base::testing::StatusIs;

// Note that the compliance tests below are more exhaustive.
TEST(JsonTest, JsonExtract) {
  const std::string json = R"({"a": {"b": [ { "c" : "foo" } ] } })";
  const std::vector<std::pair<std::string, std::string>> inputs_and_outputs = {
      {"$", R"({"a":{"b":[{"c":"foo"}]}})"},
      {"$.a", R"({"b":[{"c":"foo"}]})"},
      {"$.a.b", R"([{"c":"foo"}])"},
      {"$.a.b[0]", R"({"c":"foo"})"},
      {"$.a.b[0].c", R"("foo")"}};
  for (const auto& input_and_output : inputs_and_outputs) {
    SCOPED_TRACE(absl::Substitute("JSON_EXTRACT('$0', '$1')", json,
                                  input_and_output.first));
    ZETASQL_ASSERT_OK_AND_ASSIGN(
        const std::unique_ptr<JsonPathEvaluator> evaluator,
        JsonPathEvaluator::Create(input_and_output.first,
                                  /*sql_standard_mode=*/false));
    std::string value;
    bool is_null;
    ZETASQL_ASSERT_OK(evaluator->Extract(json, &value, &is_null));
    EXPECT_EQ(input_and_output.second, value);
    EXPECT_FALSE(is_null);
  }
}

TEST(JsonTest, JsonExtractScalar) {
  const std::string json = R"({"a": {"b": [ { "c" : "foo" } ] } })";
  const std::vector<std::pair<std::string, std::string>> inputs_and_outputs = {
      {"$", ""},
      {"$.a", ""},
      {"$.a.b", ""},
      {"$.a.b[0]", ""},
      {"$.a.b[0].c", "foo"}};
  for (const auto& input_and_output : inputs_and_outputs) {
    SCOPED_TRACE(absl::Substitute("JSON_EXTRACT_SCALAR('$0', '$1')", json,
                                  input_and_output.first));
    ZETASQL_ASSERT_OK_AND_ASSIGN(
        const std::unique_ptr<JsonPathEvaluator> evaluator,
        JsonPathEvaluator::Create(input_and_output.first,
                                  /*sql_standard_mode=*/false));
    std::string value;
    bool is_null;
    ZETASQL_ASSERT_OK(evaluator->ExtractScalar(json, &value, &is_null));
    if (!input_and_output.second.empty()) {
      EXPECT_EQ(input_and_output.second, value);
      EXPECT_FALSE(is_null);
    } else {
      EXPECT_TRUE(is_null);
    }
  }
}

void ExpectExtractScalar(absl::string_view json, absl::string_view path,
                         absl::string_view expected) {
  SCOPED_TRACE(absl::Substitute("JSON_EXTRACT_SCALAR('$0', '$1')", json, path));
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<JsonPathEvaluator> evaluator,
      JsonPathEvaluator::Create(path, /*sql_standard_mode=*/true));
  std::string value;
  bool is_null;
  ZETASQL_ASSERT_OK(evaluator->ExtractScalar(json, &value, &is_null));
  if (!expected.empty()) {
    EXPECT_EQ(expected, value);
    EXPECT_FALSE(is_null);
  } else {
    EXPECT_TRUE(is_null);
  }
}

TEST(JsonTest, JsonExtractScalarBadBehavior) {
  // This is almost certainly an unintentional bug in the implementation. The
  // root cause is that, in general, parsing stops once the scalar is found.
  // Thus what the parser sees is for example '"{"a": 0"<etc>'.  So all manner
  // of terrible stuff can be beyond the parsed string.

  // It is not clear if this is desired behavior, for now, this simply records
  // that this is the _current_ behavior.
  ExpectExtractScalar(R"({"a": 0001})", "$.a", "0");
  ExpectExtractScalar(R"({"a": 123abc})", "$.a", "123");
  ExpectExtractScalar(R"({"a": 1ab\\unicorn\0{{{{{{)", "$.a", "1");
}

TEST(JsonTest, JsonExtractScalarExpectVeryLongIntegersPassthrough) {
  std::string long_integer_str(500, '1');
  CHECK_EQ(long_integer_str.size(), 500);
  ExpectExtractScalar(absl::StrFormat(R"({"a": %s})", long_integer_str), "$.a",
                      long_integer_str);
}

TEST(JsonTest, Compliance) {
  std::vector<std::vector<FunctionTestCall>> all_tests = {
      GetFunctionTestsJsonExtract(), GetFunctionTestsJson()};
  for (std::vector<FunctionTestCall>& tests : all_tests) {
    for (const FunctionTestCall& test : tests) {
      if (test.params.params()[0].is_null() ||
          test.params.params()[1].is_null()) {
        continue;
      }
      const std::string json = test.params.param(0).string_value();
      const std::string json_path = test.params.param(1).string_value();
      SCOPED_TRACE(absl::Substitute("$0('$1', '$2')", test.function_name, json,
                                    json_path));

      std::string value;
      bool is_null = false;
      zetasql_base::Status status;
      bool sql_standard_mode = test.function_name == "json_query" ||
                               test.function_name == "json_value";
      auto evaluator_status =
          JsonPathEvaluator::Create(json_path, sql_standard_mode);
      if (evaluator_status.ok()) {
        const std::unique_ptr<JsonPathEvaluator>& evaluator =
            evaluator_status.ValueOrDie();
        evaluator->enable_special_character_escaping();
        if (test.function_name == "json_extract" ||
            test.function_name == "json_query") {
          status = evaluator->Extract(json, &value, &is_null);
        } else {
          status = evaluator->ExtractScalar(json, &value, &is_null);
        }
      } else {
        status = evaluator_status.status();
      }
      if (!status.ok() || !test.params.status().ok()) {
        EXPECT_EQ(test.params.status().code(), status.code()) << status;
      } else {
        EXPECT_EQ(test.params.result().is_null(), is_null);
        if (!test.params.result().is_null() && !is_null) {
          EXPECT_EQ(test.params.result().string_value(), value);
        }
      }
    }
  }
}

TEST(JsonPathTest, JsonPathEndedWithDotNonStandardMode) {
  const std::string json = R"({"a": {"b": [ { "c" : "foo" } ] } })";
  const std::vector<std::pair<std::string, std::string>> inputs_and_outputs = {
      {"$.", R"({"a":{"b":[{"c":"foo"}]}})"},
      {"$.a.", R"({"b":[{"c":"foo"}]})"},
      {"$.a.b.", R"([{"c":"foo"}])"},
      {"$.a.b[0].", R"({"c":"foo"})"},
      {"$.a.b[0].c.", R"("foo")"}};
  for (const auto& input_and_output : inputs_and_outputs) {
    SCOPED_TRACE(absl::Substitute("JSON_EXTRACT('$0', '$1')", json,
                                  input_and_output.first));
    ZETASQL_ASSERT_OK_AND_ASSIGN(
        const std::unique_ptr<JsonPathEvaluator> evaluator,
        JsonPathEvaluator::Create(input_and_output.first,
                                  /*sql_standard_mode=*/false));
    std::string value;
    bool is_null;
    ZETASQL_ASSERT_OK(evaluator->Extract(json, &value, &is_null));
    EXPECT_EQ(input_and_output.second, value);
    EXPECT_FALSE(is_null);
  }
}

TEST(JsonPathTest, JsonPathEndedWithDotStandardMode) {
  const std::string json = R"({"a": {"b": [ { "c" : "foo" } ] } })";
  const std::vector<std::pair<std::string, std::string>> inputs_and_outputs = {
      {"$.", R"({"a":{"b":[{"c":"foo"}]}})"},
      {"$.a.", R"({"b":[{"c":"foo"}]})"},
      {"$.a.b.", R"([{"c":"foo"}])"},
      {"$.a.b[0].", R"({"c":"foo"})"},
      {"$.a.b[0].c.", R"("foo")"}};
  for (const auto& input_and_output : inputs_and_outputs) {
    SCOPED_TRACE(absl::Substitute("JSON_QUERY('$0', '$1')", json,
                                  input_and_output.first));

    EXPECT_THAT(JsonPathEvaluator::Create(input_and_output.first,
                                          /*sql_standard_mode=*/true),
                StatusIs(zetasql_base::StatusCode::kOutOfRange,
                         HasSubstr("Invalid token in JSONPath at:")));
  }
}

}  // namespace

namespace json_internal {
namespace {

using ::testing::HasSubstr;
using ::zetasql_base::testing::StatusIs;

// Unit tests for the JSONPathExtractor and ValidJSONPathIterator.
static std::string Normalize(const std::string& in) {
  std::string output;
  std::string::const_iterator in_itr = in.begin();
  for (; in_itr != in.end(); ++in_itr) {
    if (!std::isspace(*in_itr)) {
      output.push_back(*in_itr);
    }
  }
  return output;
}

TEST(JsonPathExtractorTest, ScanTester) {
  std::unique_ptr<ValidJSONPathIterator> iptr;
  {
    std::string non_persisting_path = "$.a.b.c.d";
    ZETASQL_ASSERT_OK_AND_ASSIGN(
        iptr, ValidJSONPathIterator::Create(non_persisting_path,
                                            /*sql_standard_mode=*/true));
    iptr->Scan();
  }
  ValidJSONPathIterator& itr = *iptr;
  ASSERT_TRUE(itr.End());
  itr.Rewind();
  ASSERT_TRUE(!itr.End());

  const std::vector<ValidJSONPathIterator::Token> gold = {"", "a", "b", "c",
                                                          "d"};
  std::vector<ValidJSONPathIterator::Token> tokens;
  for (; !itr.End(); ++itr) {
    tokens.push_back(*itr);
  }
  EXPECT_THAT(tokens, ElementsAreArray(gold));
}

TEST(JsonPathExtractorTest, SimpleValidPath) {
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iptr,
      ValidJSONPathIterator::Create("$.a.b", /*sql_standard_mode=*/true));
  ValidJSONPathIterator& itr = *(iptr.get());

  ASSERT_TRUE(!itr.End());

  const std::vector<ValidJSONPathIterator::Token> gold = {"", "a", "b"};
  std::vector<ValidJSONPathIterator::Token> tokens;
  for (; !itr.End(); ++itr) {
    tokens.push_back(*itr);
  }
  EXPECT_THAT(tokens, ElementsAreArray(gold));
}

TEST(JsonPathExtractorTest, BackAndForthIteration) {
  const char* const input = "$.a.b";
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iptr,
      ValidJSONPathIterator::Create(input, /*sql_standard_mode=*/true));
  ValidJSONPathIterator& itr = *(iptr.get());

  ++itr;
  EXPECT_EQ(*itr, "a");
  --itr;
  EXPECT_EQ(*itr, "");
  --itr;
  EXPECT_TRUE(itr.End());
  ++itr;
  EXPECT_EQ(*itr, "");
  ++itr;
  EXPECT_EQ(*itr, "a");
  ++itr;
  EXPECT_EQ(*itr, "b");
}

TEST(JsonPathExtractorTest, EscapedPathTokens) {
  std::string esc_text("$.a['\\'\\'\\s '].g[1]");
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iptr,
      ValidJSONPathIterator::Create(esc_text, /*sql_standard_mode=*/false));
  ValidJSONPathIterator& itr = *(iptr.get());
  const std::vector<ValidJSONPathIterator::Token> gold = {"", "a", "''\\s ",
                                                          "g", "1"};

  std::vector<ValidJSONPathIterator::Token> tokens;
  for (; !itr.End(); ++itr) {
    tokens.push_back(*itr);
  }

  EXPECT_THAT(tokens, ElementsAreArray(gold));
}

TEST(JsonPathExtractorTest, EscapedPathTokensStandard) {
  std::string esc_text("$.a.\"\\\"\\\"\\s \".g[1]");
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iptr,
      ValidJSONPathIterator::Create(esc_text, /*sql_standard_mode=*/true));
  ValidJSONPathIterator& itr = *(iptr.get());
  const std::vector<ValidJSONPathIterator::Token> gold = {"", "a", "\"\"\\s ",
                                                          "g", "1"};

  std::vector<ValidJSONPathIterator::Token> tokens;
  for (; !itr.End(); ++itr) {
    tokens.push_back(*itr);
  }

  EXPECT_THAT(tokens, ElementsAreArray(gold));
}

TEST(JsonPathExtractorTest, MixedPathTokens) {
  const char* const input_path =
      "$.a.b[423490].c['d::d'].e['abc\\\\\\'\\'     ']";
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iptr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  ValidJSONPathIterator& itr = *(iptr.get());
  const std::vector<ValidJSONPathIterator::Token> gold = {
      "", "a", "b", "423490", "c", "d::d", "e", "abc\\\\''     "};

  std::vector<ValidJSONPathIterator::Token> tokens;
  size_t n = gold.size();
  for (; !itr.End(); ++itr) {
    tokens.push_back(*itr);
  }

  EXPECT_THAT(tokens, ElementsAreArray(gold));

  tokens.clear();

  // Test along the decrement of the iterator.
  --itr;
  EXPECT_FALSE(itr.End());
  for (; !itr.End(); --itr) {
    tokens.push_back(*itr);
  }
  EXPECT_EQ(tokens.size(), n);

  for (size_t i = 0; i < tokens.size(); i++) {
    EXPECT_EQ(gold[(n - 1) - i], tokens[i]);
  }

  // Test along the increment of the iterator.
  tokens.clear();
  EXPECT_TRUE(itr.End());
  ++itr;
  EXPECT_FALSE(itr.End());
  for (; !itr.End(); ++itr) {
    tokens.push_back(*itr);
  }

  EXPECT_THAT(tokens, ElementsAreArray(gold));
}

TEST(RemoveBackSlashFollowedByChar, BasicTests) {
  std::string token = "'abc\\'\\'h'";
  std::string expected_token = "'abc''h'";
  RemoveBackSlashFollowedByChar(&token, '\'');
  EXPECT_EQ(token, expected_token);

  token = "";
  expected_token = "";
  RemoveBackSlashFollowedByChar(&token, '\'');
  EXPECT_EQ(token, expected_token);

  token = "\\'";
  expected_token = "'";
  RemoveBackSlashFollowedByChar(&token, '\'');
  EXPECT_EQ(token, expected_token);

  token = "\\'\\'\\\\'\\'\\'\\f ";
  expected_token = "''\\'''\\f ";
  RemoveBackSlashFollowedByChar(&token, '\'');
  EXPECT_EQ(token, expected_token);
}

TEST(IsValidJSONPathTest, BasicTests) {
  ZETASQL_EXPECT_OK(IsValidJSONPath("$", /*sql_standard_mode=*/true));
  ZETASQL_EXPECT_OK(IsValidJSONPath("$.a", /*sql_standard_mode=*/true));

  // Escaped a
  EXPECT_THAT(IsValidJSONPath("$['a']", /*sql_standard_mode=*/true),
              StatusIs(zetasql_base::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at:")));
  ZETASQL_EXPECT_OK(IsValidJSONPath("$['a']", /*sql_standard_mode=*/false));
  ZETASQL_EXPECT_OK(IsValidJSONPath("$.\"a\"", /*sql_standard_mode=*/true));

  // Escaped efgh
  EXPECT_THAT(IsValidJSONPath("$.a.b.c['efgh'].e", /*sql_standard_mode=*/true),
              StatusIs(zetasql_base::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at:")));
  ZETASQL_EXPECT_OK(IsValidJSONPath("$.a.b.c['efgh'].e", /*sql_standard_mode=*/false));
  ZETASQL_EXPECT_OK(IsValidJSONPath("$.a.b.c.\"efgh\".e", /*sql_standard_mode=*/true));

  // Escaped b.c.d
  EXPECT_THAT(IsValidJSONPath("$.a['b.c.d'].e", /*sql_standard_mode=*/true),
              StatusIs(zetasql_base::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at:")));
  ZETASQL_EXPECT_OK(IsValidJSONPath("$.a['b.c.d'].e", /*sql_standard_mode=*/false));
  ZETASQL_EXPECT_OK(IsValidJSONPath("$.a.\"b.c.d\".e", /*sql_standard_mode=*/true));
  ZETASQL_EXPECT_OK(IsValidJSONPath("$.\"b.c.d\".e", /*sql_standard_mode=*/true));

  EXPECT_THAT(
      IsValidJSONPath("$['a']['b']['c']['efgh']", /*sql_standard_mode=*/true),
      StatusIs(zetasql_base::StatusCode::kOutOfRange,
               HasSubstr("Invalid token in JSONPath at:")));
  ZETASQL_EXPECT_OK(IsValidJSONPath("$['a']['b']['c']['efgh']",
                            /*sql_standard_mode=*/false));

  ZETASQL_EXPECT_OK(IsValidJSONPath("$.a.b.c[0].e.f", /*sql_standard_mode=*/true));

  EXPECT_THAT(IsValidJSONPath("$['a']['b']['c'][0]['e']['f']",
                              /*sql_standard_mode=*/true),
              StatusIs(zetasql_base::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at:")));
  ZETASQL_EXPECT_OK(IsValidJSONPath("$['a']['b']['c'][0]['e']['f']",
                            /*sql_standard_mode=*/false));

  EXPECT_THAT(IsValidJSONPath("$['a']['b\\'\\c\\\\d          ef']",
                              /*sql_standard_mode=*/true),
              StatusIs(zetasql_base::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at:")));
  ZETASQL_EXPECT_OK(IsValidJSONPath("$['a']['b\\'\\c\\\\d          ef']",
                            /*sql_standard_mode=*/false));

  EXPECT_THAT(IsValidJSONPath("$['a;;;;;\\\\']['b\\'\\c\\\\d          ef']",
                              /*sql_standard_mode=*/true),
              StatusIs(zetasql_base::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at:")));
  ZETASQL_EXPECT_OK(IsValidJSONPath("$['a;;;;;\\\\']['b\\'\\c\\\\d          ef']",
                            /*sql_standard_mode=*/false));

  EXPECT_THAT(IsValidJSONPath("$.a['\\'\\'\\'\\'\\'\\\\f '].g[1]",
                              /*sql_standard_mode=*/true),
              StatusIs(zetasql_base::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at:")));
  ZETASQL_EXPECT_OK(IsValidJSONPath("$.a['\\'\\'\\'\\'\\'\\\\f '].g[1]",
                            /*sql_standard_mode=*/false));

  EXPECT_THAT(IsValidJSONPath("$.a.b.c[efgh]", /*sql_standard_mode=*/true),
              StatusIs(zetasql_base::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at:")));
  ZETASQL_EXPECT_OK(IsValidJSONPath("$.a.b.c[efgh]", /*sql_standard_mode=*/false));

  // unsupported @ in the path.
  EXPECT_THAT(
      IsValidJSONPath("$.a.;;;;;;;c[0];;;.@.f", /*sql_standard_mode=*/true),
      StatusIs(zetasql_base::StatusCode::kOutOfRange,
               HasSubstr("Unsupported operator in JSONPath: @")));
  EXPECT_THAT(
      IsValidJSONPath("$.a.;;;;;;;.c[0].@.f", /*sql_standard_mode=*/true),
      StatusIs(zetasql_base::StatusCode::kOutOfRange,
               HasSubstr("Unsupported operator in JSONPath: @")));
  EXPECT_THAT(IsValidJSONPath("$..", /*sql_standard_mode=*/true),
              StatusIs(zetasql_base::StatusCode::kOutOfRange,
                       HasSubstr("Unsupported operator in JSONPath: ..")));
  EXPECT_THAT(
      IsValidJSONPath("$.a.b.c[f.g.h.i].m.f", /*sql_standard_mode=*/false),
      StatusIs(zetasql_base::StatusCode::kOutOfRange,
               HasSubstr("Invalid token in JSONPath at: [f.g.h.i]")));
  EXPECT_THAT(IsValidJSONPath("$.a.b.c['f.g.h.i'].[acdm].f",
                              /*sql_standard_mode=*/false),
              StatusIs(zetasql_base::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at: .[acdm]")));
}

TEST(JSONPathExtractorTest, BasicParsing) {
  std::string input =
      "{ \"l00\" : { \"l01\" : \"a10\", \"l11\" : \"test\" }, \"l10\" : { "
      "\"l01\" : null }, \"l20\" : \"a5\" }";
  absl::string_view input_str(input);
  absl::string_view input_path("$");

  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string result;
  bool is_null;

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_EQ(result, Normalize(input));
  EXPECT_FALSE(is_null);
}

TEST(JSONPathExtractorTest, MatchingMultipleSuffixes) {
  std::string input =
      "{ \"a\" : { \"b\" : \"a10\", \"l11\" : \"test\" }, \"a\" : { "
      "\"c\" : null }, \"a\" : \"a5\", \"a\" : \"a6\" }";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.c");

  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string result;
  bool is_null;
  std::string gold = "null";

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_TRUE(parser.StoppedOnFirstMatch());
  EXPECT_EQ(result, gold);
  EXPECT_TRUE(is_null);
}

TEST(JSONPathExtractorTest, PartiallyMatchingSuffixes) {
  std::string input =
      "{ \"a\" : { \"b\" : \"a10\", \"l11\" : \"test\" }, \"a\" : { "
      "\"c\" : null }, \"a\" : \"a5\", \"a\" : \"a6\" }";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.c.d");

  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string result;
  bool is_null;
  std::string gold = "";

  // Parsing of JSON was successful however no match.
  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_FALSE(parser.StoppedOnFirstMatch());
  EXPECT_TRUE(is_null);
  EXPECT_EQ(result, gold);
}

TEST(JSONPathExtractorTest, MatchedEmptyStringValue) {
  std::string input =
      "{ \"a\" : { \"b\" : \"a10\", \"l11\" : \"test\" }, \"a\" : { "
      "\"c\" : {\"d\" : \"\" } }, \"a\" : \"a5\", \"a\" : \"a6\" }";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.c.d");

  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());

  // Parsing of JSON was successful and the value
  // itself is "" so we can use StoppedOnFirstMatch() to
  // distinguish between a matched value which is empty and
  // the case where there is no match. We can also rely on
  // the return value of \"\" however this is more elegant.
  std::string result;
  bool is_null;
  std::string gold = "\"\"";

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_TRUE(parser.StoppedOnFirstMatch());
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);
}

TEST(JSONPathExtractScalar, ValidateScalarResult) {
  std::string input =
      "{ \"a\" : { \"b\" : \"a10\", \"l11\" : \"tes\\\"t\" }, \"a\" : { "
      "\"c\" : {\"d\" : 1.9834 } , \"d\" : [ {\"a\" : \"a5\"}, {\"a\" : "
      "\"a6\"}] , \"quoted_null\" : \"null\" } , \"e\" : null , \"f\" : null}";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.c.d");

  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));

  JSONPathExtractScalar parser(input_str, path_itr.get());
  std::string scalar_result;
  bool is_null;

  EXPECT_TRUE(parser.Extract(&scalar_result, &is_null));
  EXPECT_TRUE(parser.StoppedOnFirstMatch());
  std::string gold = "1.9834";
  EXPECT_FALSE(is_null);
  EXPECT_EQ(scalar_result, gold);

  input_path = "$.a.l11";
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr1,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractScalar parser1(input_str, path_itr1.get());

  EXPECT_TRUE(parser1.Extract(&scalar_result, &is_null));
  gold = "tes\"t";
  EXPECT_FALSE(is_null);
  EXPECT_EQ(scalar_result, gold);

  input_path = "$.a.c";
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr2,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractScalar parser2(input_str, path_itr2.get());

  EXPECT_TRUE(parser2.Extract(&scalar_result, &is_null));
  EXPECT_TRUE(is_null);

  input_path = "$.a.d";
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr3,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractScalar parser3(input_str, path_itr3.get());

  EXPECT_TRUE(parser3.Extract(&scalar_result, &is_null));
  EXPECT_TRUE(is_null);

  input_path = "$.e";
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr4,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractScalar parser4(input_str, path_itr4.get());

  EXPECT_TRUE(parser4.Extract(&scalar_result, &is_null));
  EXPECT_TRUE(is_null);

  input_path = "$.a.c.d.e";
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr5,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractScalar parser5(input_str, path_itr5.get());

  EXPECT_TRUE(parser5.Extract(&scalar_result, &is_null));
  EXPECT_FALSE(parser5.StoppedOnFirstMatch());
  EXPECT_TRUE(is_null);

  input_path = "$.a.quoted_null";
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr6,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractScalar parser6(input_str, path_itr6.get());

  EXPECT_TRUE(parser6.Extract(&scalar_result, &is_null));
  EXPECT_FALSE(is_null);
  gold = "null";
  EXPECT_EQ(scalar_result, gold);

  input_path = "$.a.b.c";
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr7,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractScalar parser7(input_str, path_itr7.get());

  EXPECT_TRUE(parser7.Extract(&scalar_result, &is_null));
  EXPECT_TRUE(is_null);
  EXPECT_FALSE(parser7.StoppedOnFirstMatch());
}

TEST(JSONPathExtractorTest, ReturnJSONObject) {
  std::string input =
      "{ \"e\" : { \"b\" : \"a10\", \"l11\" : \"test\" }, \"a\" : { "
      "\"c\" : null, \"f\" : { \"g\" : \"h\", \"g\" : [ \"i\", { \"x\" : "
      "\"j\"} ] } }, "
      "\"a\" : \"a5\", \"a\" : \"a6\" }";

  absl::string_view input_str(input);
  absl::string_view input_path("$.a.f");

  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string result;
  bool is_null;
  std::string gold = "{ \"g\" : \"h\", \"g\" : [ \"i\", { \"x\" : \"j\" } ] }";

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_TRUE(parser.StoppedOnFirstMatch());
  EXPECT_EQ(result, Normalize(gold));
}

TEST(JSONPathExtractorTest, StopParserOnFirstMatch) {
  std::string input =
      "{ \"a\" : { \"b\" : { \"c\" : { \"d\" : \"l1\" } } } ,"
      " \"a\" : { \"b\" :  { \"c\" : { \"e\" : \"l2\" } } } ,"
      " \"a\" : { \"b\" : { \"c\" : { \"e\" : \"l3\"} }}}";

  std::string result;
  bool is_null;

  {
    absl::string_view input_str(input);
    absl::string_view input_path("$.a.b.c");

    ZETASQL_ASSERT_OK_AND_ASSIGN(
        const std::unique_ptr<ValidJSONPathIterator> path_itr,
        ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
    JSONPathExtractor parser(input_str, path_itr.get());

    std::string gold = "{ \"d\" : \"l1\" }";

    EXPECT_TRUE(parser.Extract(&result, &is_null));
    EXPECT_FALSE(is_null);
    EXPECT_TRUE(parser.StoppedOnFirstMatch());
    EXPECT_EQ(result, Normalize(gold));
  }

  {
    absl::string_view input_str(input);
    absl::string_view input_path("$.a.b.c");

    ZETASQL_ASSERT_OK_AND_ASSIGN(
        const std::unique_ptr<ValidJSONPathIterator> path_itr,
        ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
    JSONPathExtractor parser(input_str, path_itr.get());

    std::string gold = "{ \"d\" : \"l1\" }";
    EXPECT_TRUE(parser.Extract(&result, &is_null));
    EXPECT_FALSE(is_null);
    EXPECT_TRUE(parser.StoppedOnFirstMatch());
    EXPECT_EQ(result, Normalize(gold));
  }
}

TEST(JSONPathExtractorTest, BasicArrayAccess) {
  std::string input =
      "{ \"e\" : { \"b\" : \"a10\", \"l11\" : \"test\" }, \"a\" : { "
      "\"c\" : null, \"f\" : { \"g\" : \"h\", \"g\" : [ \"i\", \"j\" ] } }, "
      "\"a\" : \"a5\", \"a\" : \"a6\" }";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.f.g[1]");

  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string result;
  bool is_null;
  std::string gold = "\"j\"";

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);
}

TEST(JSONPathExtractorTest, ArrayAccessObjectMultipleSuffixes) {
  std::string input =
      "{ \"e\" : { \"b\" : \"a10\", \"l11\" : \"test\" },"
      " \"a\" : { \"f\" : null, "
      "\"f\" : { \"g\" : \"h\", "
      "\"g\" : [ \"i\", \"j\" ] } }, "
      "\"a\" : \"a5\", \"a\" : \"a6\" }";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.f.g[1]");

  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string gold = "\"j\"";
  std::string result;
  bool is_null;

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);
}

TEST(JSONPathExtractorTest, EscapedAccessTestStandard) {
  // There are two escapings happening as follows:
  // a. C++ Compiler
  // b. JSON Parser
  //
  // So '4k' (k > = 1) backslashes translate to 'k' backslashes at runtime
  // "\\\\" = "\" at runtime. So "\\\\\\\\s" === "\\s"
  std::string input =
      "{ \"e\" : { \"b\" : \"a10\", \"l11\" : \"test\" },"
      " \"a\" : { \"b\" : null, "
      "\"''\\\\\\\\s \" : { \"g\" : \"h\", "
      "\"g\" : [ \"i\", \"j\" ] } }, "
      "\"a\" : \"a5\", \"a\" : \"a6\" }";
  absl::string_view input_str(input);
  std::string input_path("$.a['\\'\\'\\\\s '].g[1]");
  absl::string_view esc_input_path(input_path);

  ZETASQL_ASSERT_OK_AND_ASSIGN(const std::unique_ptr<ValidJSONPathIterator> path_itr,
                       ValidJSONPathIterator::Create(
                           esc_input_path, /*sql_standard_mode=*/false));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string result;
  bool is_null;
  std::string gold = "\"j\"";

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);
}

TEST(JSONPathExtractorTest, EscapedAccessTest) {
  std::string input = R"({"a\"b": 1 })";
  absl::string_view input_str(input);
  std::string input_path(R"($."a\"b")");
  absl::string_view esc_input_path(input_path);

  LOG(INFO) << input;

  ZETASQL_ASSERT_OK_AND_ASSIGN(const std::unique_ptr<ValidJSONPathIterator> path_itr,
                       ValidJSONPathIterator::Create(
                           esc_input_path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string result;
  bool is_null;
  std::string gold = "1";

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);
}

TEST(JSONPathExtractorTest, NestedArrayAccess) {
  std::string input =
      "[0 , [ [],  [ [ 1, 4, 8, [2, 1, 0, {\"a\" : \"3\"}, 4 ], 11, 13] ] , "
      "[], \"a\" ], 2, [] ]";
  absl::string_view input_str(input);
  absl::string_view input_path("$[1][1][0][3][3]");
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string result;
  bool is_null;
  std::string gold = "{ \"a\" : \"3\" }";
  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_EQ(result, Normalize(gold));
  EXPECT_FALSE(is_null);
}

TEST(JSONPathExtractorTest, NegativeNestedArrayAccess) {
  std::string input =
      "[0 , [ [],  [ [ 1, 4, 8, [2, 1, 0, {\"a\" : \"3\"}, 4 ], 11, 13] ] , "
      "[], \"a\" ], 2, [] ]";
  absl::string_view input_str(input);
  absl::string_view input_path("$[1][1]['-0'][3][3]");
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string result;
  bool is_null;

  std::string gold = "{ \"a\" : \"3\" }";
  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, Normalize(gold));

  absl::string_view input_path1("$[1][1]['-5'][3][3]");
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr1,
      ValidJSONPathIterator::Create(input_path1, /*sql_standard_mode=*/false));
  JSONPathExtractor parser1(input_str, path_itr1.get());

  EXPECT_TRUE(parser1.Extract(&result, &is_null));
  EXPECT_TRUE(is_null);
  EXPECT_FALSE(parser1.StoppedOnFirstMatch());
  EXPECT_EQ(result, "");
}

TEST(JSONPathExtractorTest, MixedNestedArrayAccess) {
  std::string input =
      "{ \"a\" : [0 , [ [],  { \"b\" : [ 7, [ 1, 4, 8, [2, 1, 0, {\"a\" : { "
      "\"b\" : \"3\"}, \"c\" : \"d\" }, 4 ], 11, 13] ] }, "
      "[], \"a\" ], 2, [] ] }";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a[1][1].b[1][3][3].c");

  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());
  std::string result;
  bool is_null;
  std::string gold = "\"d\"";

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);
}

TEST(JSONPathExtractorTest, QuotedArrayIndex) {
  std::string input =
      "[0 , [ [],  [ [ 1, 4, 8, [2, 1, 0, {\"a\" : \"3\"}, 4 ], 11, 13] ] , "
      "[], \"a\" ], 2, [] ]";
  absl::string_view input_str(input);
  absl::string_view input_path("$['1'][1][0]['3']['3']");

  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string result;
  bool is_null;
  std::string gold = "{ \"a\" : \"3\" }";

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_EQ(result, Normalize(gold));
  EXPECT_FALSE(is_null);
}

TEST(JSONPathExtractorTest, TestReuseOfPathIterator) {
  std::string input =
      "[0 , [ [],  [ [ 1, 4, 8, [2, 1, 0, {\"a\" : \"3\"}, 4 ], 11, 13] ] , "
      "[], \"a\" ], 2, [] ]";
  std::string path = "$[1][1][0][3][3]";
  absl::string_view input_str(input);
  std::string gold = "{ \"a\" : \"3\" }";
  std::string result;
  bool is_null;

  // Default with local path_iterator object.
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_EQ(result, Normalize(gold));
  EXPECT_FALSE(is_null);

  for (size_t i = 0; i < 10; i++) {
    // Reusable token iterator.
    absl::string_view input_str(input);
    JSONPathExtractor parser(input_str, path_itr.get());

    EXPECT_TRUE(parser.Extract(&result, &is_null));
    EXPECT_EQ(result, Normalize(gold));
    EXPECT_FALSE(is_null);
  }
}

TEST(JSONPathArrayExtractorTest, BasicParsing) {
  std::string input =
      "[ {\"l00\" : { \"l01\" : \"a10\", \"l11\" : \"test\" }}, {\"l10\" : { "
      "\"l01\" : null }}, {\"l20\" : \"a5\"} ]";
  absl::string_view input_str(input);
  absl::string_view input_path("$");

  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  std::vector<std::string> gold(
      {Normalize("{\"l00\": { \"l01\" : \"a10\", \"l11\" : \"test\" }}"),
       Normalize("{\"l10\" : { \"l01\" : null }}"),
       Normalize("{\"l20\" : \"a5\"}")});
  bool is_null;

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_EQ(result, gold);
  EXPECT_FALSE(is_null);
}

TEST(JSONPathArrayExtractorTest, MatchingMultipleSuffixes) {
  std::string input =
      "{ \"a\" : { \"b\" : \"a10\", \"l11\" : \"test\" }, \"a\" : { "
      "\"c\" : null }, \"a\" : \"a5\", \"a\" : \"a6\" }";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.c");

  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  // Matching the leaf while it is not an array
  std::vector<std::string> gold({});

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_TRUE(parser.StoppedOnFirstMatch());
  EXPECT_EQ(result, gold);
  EXPECT_TRUE(is_null);
}

TEST(JSONPathArrayExtractorTest, MatchedEmptyArray) {
  std::string input =
      "{ \"a\" : { \"b\" : \"a10\", \"l11\" : \"test\" }, \"a\" : { "
      "\"c\" : {\"d\" : [] } }, \"a\" : \"a5\", \"a\" : \"a6\" }";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.c.d");

  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold({});

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_TRUE(parser.StoppedOnFirstMatch());
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);
}

TEST(JSONPathArrayExtractorTest, PartiallyMatchingSuffixes) {
  std::string input =
      "{ \"a\" : { \"b\" : \"a10\", \"l11\" : \"test\" }, \"a\" : { "
      "\"c\" : null }, \"a\" : \"a5\", \"a\" : \"a6\" }";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.c.d");

  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold({});

  // Parsing of JSON was successful however no match.
  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_FALSE(parser.StoppedOnFirstMatch());
  EXPECT_TRUE(is_null);
  EXPECT_EQ(result, gold);
}

TEST(JSONPathArrayExtractorTest, ReturnJSONObjectArray) {
  std::string input =
      "{ \"e\" : { \"b\" : \"a10\", \"l11\" : \"test\" }, \"a\" : { "
      "\"c\" : null, \"f\" : [ {\"g\" : \"h\"}, {\"g\" : [ \"i\", { \"x\" : "
      "\"j\"} ] } ] }, "
      "\"a\" : \"a5\", \"a\" : \"a6\" }";

  absl::string_view input_str(input);
  absl::string_view input_path("$.a.f");

  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold(
      {Normalize("{ \"g\" : \"h\"}"),
       Normalize("{\"g\" : [ \"i\", { \"x\" : \"j\" } ] }")});

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_TRUE(parser.StoppedOnFirstMatch());
  EXPECT_EQ(result, gold);
}

TEST(JSONPathArrayExtractorTest, StopParserOnFirstMatch) {
  std::string input =
      "{ \"a\" : { \"b\" : { \"c\" : { \"d\" : [\"l1\"] } } } ,"
      " \"a\" : { \"b\" :  { \"c\" : { \"e\" : \"l2\" } } } ,"
      " \"a\" : { \"b\" : { \"c\" : { \"d\" : \"l3\"} }}}";

  absl::string_view input_str(input);
  absl::string_view input_path("$.a.b.c.d");

  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold({"\"l1\""});

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_TRUE(parser.StoppedOnFirstMatch());
  EXPECT_EQ(result, gold);
}

TEST(JSONPathArrayExtractorTest, BasicArrayAccess) {
  std::string input =
      "{ \"e\" : { \"b\" : \"a10\", \"l11\" : \"test\" }, \"a\" : { "
      "\"c\" : null, \"f\" : { \"g\" : \"h\", \"g\" : [ [\"i\"], [\"j\", "
      "\"k\"] ] } }, \"a\" : \"a5\", \"a\" : \"a6\" }";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.f.g[1]");

  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold({"\"j\"", "\"k\""});

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);
}

TEST(JSONPathArrayExtractorTest, AccessObjectInArrayMultipleSuffixes) {
  std::string input =
      "{ \"e\" : { \"b\" : \"a10\", \"l11\" : \"test\" }, \"a\" : { \"f\" : "
      "null, \"f\" : { \"g\" : \"h\", \"g\" : [ [\"i\"], [\"j\", \"k\"] ] } }, "
      "\"a\" : \"a5\", \"a\" : \"a6\" }";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.f.g[1]");

  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold({"\"j\"", "\"k\""});

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);
}

TEST(JSONPathArrayExtractorTest, EscapedAccessTestNonSqlStandard) {
  // There are two escapings happening as follows:
  // a. C++ Compiler
  // b. JSON Parser
  //
  // So '4k' (k > = 1) backslashes translate to 'k' backslashes at runtime
  // "\\\\" = "\" at runtime. So "\\\\\\\\s" === "\\s"
  std::string input =
      "{ \"e\" : { \"b\" : \"a10\", \"l11\" : \"test\" },"
      " \"a\" : { \"b\" : null, "
      "\"''\\\\\\\\s \" : { \"g\" : \"h\", "
      "\"g\" : [ \"i\", [\"j\", \"k\"] ] } }, "
      "\"a\" : \"a5\", \"a\" : \"a6\" }";
  absl::string_view input_str(input);
  std::string input_path("$.a['\\'\\'\\\\s '].g[ 1]");
  absl::string_view esc_input_path(input_path);

  ZETASQL_ASSERT_OK_AND_ASSIGN(const std::unique_ptr<ValidJSONPathIterator> path_itr,
                       ValidJSONPathIterator::Create(
                           esc_input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold({"\"j\"", "\"k\""});

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);
}

TEST(JSONPathArrayExtractorTest,
     EscapedAccessTestNonSqlStandardInvalidJsonPath) {
  std::string input =
      "{ \"e\" : { \"b\" : \"a10\", \"l11\" : \"test\" },"
      " \"a\" : { \"b\" : null, "
      "\"''\\\\\\\\s \" : { \"g\" : \"h\", "
      "\"g\" : [ \"i\", [\"j\", \"k\"] ] } }, "
      "\"a\" : \"a5\", \"a\" : \"a6\" }";
  std::string input_path("$.a.\"\'\'\\\\s \".g[ 1]");
  absl::string_view esc_input_path(input_path);

  zetasql_base::Status status =
      ValidJSONPathIterator::Create(esc_input_path, /*sql_standard_mode=*/false)
          .status();
  EXPECT_THAT(
      status,
      StatusIs(zetasql_base::StatusCode::kOutOfRange,
               HasSubstr(R"(Invalid token in JSONPath at: ."''\\s ".g[ 1])")));
}

TEST(JSONPathArrayExtractorTest, NestedArrayAccess) {
  std::string input =
      "[0 , [ [],  [ [ 1, 4, 8, [2, 1, 0, [{\"a\" : \"3\"}, {\"a\" : \"4\"}], "
      "4 ], 11, 13] ] , [], \"a\" ], 2, [] ]";
  absl::string_view input_str(input);
  absl::string_view input_path("$[1][1][0][3][3]");
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold(
      {Normalize("{\"a\" : \"3\"}"), Normalize("{\"a\" : \"4\"}")});

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_EQ(result, gold);
  EXPECT_FALSE(is_null);
}

TEST(JSONPathArrayExtractorTest, NegativeNestedArrayAccess) {
  std::string input =
      "[0 , [ [],  [ [ 1, 4, 8, [2, 1, 0, [{\"a\" : \"3\"}, {\"a\" : \"4\"}], "
      "4 ], 11, 13] ] , [], \"a\" ], 2, [] ]";
  absl::string_view input_str(input);
  absl::string_view input_path("$[1][1]['-0'][3][3]");
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold(
      {Normalize("{\"a\" : \"3\"}"), Normalize("{\"a\" : \"4\"}")});

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);

  absl::string_view input_path1("$[1][1]['-5'][3][3]");
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr1,
      ValidJSONPathIterator::Create(input_path1, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser1(input_str, path_itr1.get());

  std::vector<std::string> gold1({});

  EXPECT_TRUE(parser1.ExtractArray(&result, &is_null));
  EXPECT_TRUE(is_null);
  EXPECT_FALSE(parser1.StoppedOnFirstMatch());
  EXPECT_EQ(result, gold1);
}

TEST(JSONPathArrayExtractorTest, MixedNestedArrayAccess) {
  std::string input =
      "{ \"a\" : [0 , [ [],  { \"b\" : [ 7, [ 1, 4, 8, [2, 1, 0, {\"a\" : { "
      "\"b\" : \"3\"}, \"c\" : [1,  2, 3 ] }, 4 ], 11, 13] ] }, "
      "[], \"a\" ], 2, [] ] }";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a[1][1].b[1][3][3].c");

  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());
  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold({"1", "2", "3"});

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);
}

TEST(JSONPathArrayExtractorTest, QuotedArrayIndex) {
  std::string input =
      "[0 , [ [],  [ [ 1, 4, 8, [2, 1, 0, [{\"a\" : \"3\"}, {\"a\" : \"4\"}], "
      "4 ], 11, 13] ] , [], \"a\" ], 2, [] ]";
  absl::string_view input_str(input);
  absl::string_view input_path("$['1'][1][0]['3']['3']");

  ZETASQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold(
      {Normalize("{\"a\" : \"3\"}"), Normalize("{\"a\" : \"4\"}")});

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_EQ(result, gold);
  EXPECT_FALSE(is_null);
}

TEST(ValidJSONPathIterator, BasicTest) {
  std::string path = "$[1][1][0][3][3]";
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iptr,
      ValidJSONPathIterator::Create(path, /*sql_standard_mode=*/true));
  ValidJSONPathIterator& itr = *(iptr.get());
  itr.Rewind();
  EXPECT_EQ(*itr, "");
  ++itr;
  EXPECT_EQ(*itr, "1");
  ++itr;
  EXPECT_EQ(*itr, "1");
  ++itr;
  EXPECT_EQ(*itr, "0");
  ++itr;
  EXPECT_EQ(*itr, "3");
  ++itr;
  EXPECT_EQ(*itr, "3");
  ++itr;
  EXPECT_TRUE(itr.End());

  // reverse.
  --itr;
  EXPECT_EQ(*itr, "3");
  --itr;
  EXPECT_EQ(*itr, "3");
  --itr;
  EXPECT_EQ(*itr, "0");
  --itr;
  EXPECT_EQ(*itr, "1");
  --itr;
  EXPECT_EQ(*itr, "1");
  --itr;
  EXPECT_EQ(*itr, "");
  --itr;
  EXPECT_TRUE(itr.End());

  ++itr;
  EXPECT_EQ(*itr, "");
  ++itr;
  EXPECT_EQ(*itr, "1");
}

TEST(ValidJSONPathIterator, DegenerateCases) {
  std::string path = "$";
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iptr,
      ValidJSONPathIterator::Create(path, /*sql_standard_mode=*/true));
  ValidJSONPathIterator& itr = *(iptr.get());

  EXPECT_FALSE(itr.End());
  EXPECT_EQ(*itr, "");

  path = "$";
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iptr1,
      ValidJSONPathIterator::Create(path, /*sql_standard_mode=*/true));
  ValidJSONPathIterator& itr1 = *(iptr1.get());

  EXPECT_FALSE(itr1.End());
  EXPECT_EQ(*itr1, "");
}

TEST(ValidJSONPathIterator, InvalidEmptyJSONPathCreation) {
  std::string path = "$.a.*.b.c";
  zetasql_base::Status status =
      ValidJSONPathIterator::Create(path, /*sql_standard_mode=*/true).status();
  EXPECT_THAT(status,
              StatusIs(zetasql_base::StatusCode::kOutOfRange,
                       HasSubstr("Unsupported operator in JSONPath: *")));

  path = "$.@";
  status =
      ValidJSONPathIterator::Create(path, /*sql_standard_mode=*/true).status();
  EXPECT_THAT(status,
              StatusIs(zetasql_base::StatusCode::kOutOfRange,
                       HasSubstr("Unsupported operator in JSONPath: @")));

  path = "$abc";
  status =
      ValidJSONPathIterator::Create(path, /*sql_standard_mode=*/true).status();
  EXPECT_THAT(status, StatusIs(zetasql_base::StatusCode::kOutOfRange,
                               HasSubstr("Invalid token in JSONPath at: abc")));

  path = "";
  status =
      ValidJSONPathIterator::Create(path, /*sql_standard_mode=*/true).status();
  EXPECT_THAT(status, StatusIs(zetasql_base::StatusCode::kOutOfRange,
                               HasSubstr("JSONPath must start with '$'")));
}

// Compliance Tests on JSON_EXTRACT
TEST(JSONPathExtractor, ComplianceJSONExtract) {
  const std::vector<FunctionTestCall> tests = GetFunctionTestsJsonExtract();
  for (const FunctionTestCall& test : tests) {
    if (test.params.params()[0].is_null() ||
        test.params.params()[1].is_null()) {
      continue;
    }
    const std::string json = test.params.param(0).string_value();
    const std::string json_path = test.params.param(1).string_value();

    EXPECT_TRUE(test.function_name == "json_extract" ||
                test.function_name == "json_extract_scalar");

    std::string value;
    zetasql_base::Status status;
    bool is_null = true;
    auto evaluator_status =
        ValidJSONPathIterator::Create(json_path, /*sql_standard_mode=*/false);
    if (evaluator_status.ok()) {
      std::string output;
      const std::unique_ptr<ValidJSONPathIterator>& path_itr =
          evaluator_status.ValueOrDie();
      if (test.function_name == "json_extract") {
        JSONPathExtractor parser(json, path_itr.get());
        parser.set_special_character_escaping(true);
        parser.Extract(&value, &is_null);
      } else {
        // json_extract_scalar
        JSONPathExtractScalar scalar_parser(json, path_itr.get());
        scalar_parser.set_special_character_escaping(true);
        scalar_parser.Extract(&value, &is_null);
      }
    } else {
      status = evaluator_status.status();
    }

    if (!status.ok() || !test.params.status().ok()) {
      EXPECT_EQ(test.params.status().code(), status.code()) << status;
    } else {
      EXPECT_EQ(is_null, test.params.result().is_null());
      if (!test.params.result().is_null()) {
        EXPECT_EQ(value, test.params.result().string_value());
      }
    }
  }
}

// Tests for JSON_QUERY and JSON_VALUE (Follows the SQL2016 standard)
TEST(JSONPathExtractor, ComplianceJSONExtractStandard) {
  const std::vector<FunctionTestCall> tests = GetFunctionTestsJson();
  for (const FunctionTestCall& test : tests) {
    if (test.params.params()[0].is_null() ||
        test.params.params()[1].is_null()) {
      continue;
    }
    const std::string json = test.params.param(0).string_value();
    const std::string json_path = test.params.param(1).string_value();

    EXPECT_TRUE(test.function_name == "json_query" ||
                test.function_name == "json_value");

    std::string value;
    zetasql_base::Status status;
    bool is_null = true;
    auto evaluator_status =
        ValidJSONPathIterator::Create(json_path, /*sql_standard_mode=*/true);
    if (evaluator_status.ok()) {
      std::string output;
      const std::unique_ptr<ValidJSONPathIterator>& path_itr =
          evaluator_status.ValueOrDie();
      if (test.function_name == "json_query") {
        JSONPathExtractor parser(json, path_itr.get());
        parser.set_special_character_escaping(true);
        parser.Extract(&value, &is_null);
      } else {
        // json_value
        JSONPathExtractScalar scalar_parser(json, path_itr.get());
        scalar_parser.set_special_character_escaping(true);
        scalar_parser.Extract(&value, &is_null);
      }
    } else {
      status = evaluator_status.status();
    }

    if (!status.ok() || !test.params.status().ok()) {
      EXPECT_EQ(test.params.status().code(), status.code()) << status;
    } else {
      EXPECT_EQ(is_null, test.params.result().is_null());
      if (!test.params.result().is_null()) {
        EXPECT_EQ(value, test.params.result().string_value());
      }
    }
  }
}

// Compliance Tests on JSON_EXTRACT_ARRAY
TEST(JSONPathExtractor, ComplianceJSONExtractArray) {
  const std::vector<FunctionTestCall> tests =
      GetFunctionTestsJsonExtractArray();
  for (const FunctionTestCall& test : tests) {
    if (test.params.params()[0].is_null() ||
        test.params.params()[1].is_null()) {
      continue;
    }
    const std::string json = test.params.param(0).string_value();
    const std::string json_path = test.params.param(1).string_value();
    const Value& expected_result = test.params.results().begin()->second.result;

    std::vector<std::string> output;
    Value result;
    zetasql_base::Status status;
    bool is_null = true;
    auto evaluator_status =
        ValidJSONPathIterator::Create(json_path, /*sql_standard_mode=*/false);
    if (evaluator_status.ok()) {
      const std::unique_ptr<ValidJSONPathIterator>& path_itr =
          evaluator_status.ValueOrDie();
      JSONPathArrayExtractor parser(json, path_itr.get());
      parser.set_special_character_escaping(true);
      parser.ExtractArray(&output, &is_null);
    } else {
      status = evaluator_status.status();
    }

    if (!status.ok() || !test.params.status().ok()) {
      EXPECT_EQ(test.params.status().code(), status.code()) << status;
    } else {
      result = values::StringArray(output);
      EXPECT_EQ(is_null, expected_result.is_null());
      if (!expected_result.is_null()) {
        EXPECT_EQ(result, expected_result);
      }
    }
  }
}

TEST(JsonPathEvaluatorTest, ExtractingArrayCloseToLimitSucceeds) {
  const int kNestingDepth = JSONPathExtractor::kMaxParsingDepth;
  const std::string nested_array_json(kNestingDepth, '[');
  std::string value;
  std::vector<std::string> array_value;
  zetasql_base::Status status;
  bool is_null = true;
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<JsonPathEvaluator> path_evaluator,
      JsonPathEvaluator::Create("$", /*sql_standard_mode=*/true));
  // Extracting should succeed, but the result is null since the arrays are not
  // closed.
  ZETASQL_EXPECT_OK(path_evaluator->Extract(nested_array_json, &value, &is_null));
  EXPECT_TRUE(is_null);
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      path_evaluator,
      JsonPathEvaluator::Create("$", /*sql_standard_mode=*/true));
  // Extracting should succeed, but the result is null since the arrays are not
  // closed.
  ZETASQL_EXPECT_OK(path_evaluator->ExtractScalar(nested_array_json, &value, &is_null));
  EXPECT_TRUE(is_null);
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      path_evaluator,
      JsonPathEvaluator::Create("$", /*sql_standard_mode=*/false));
  // Extracting should succeed, but the result is null since the arrays are not
  // closed.
  ZETASQL_EXPECT_OK(
      path_evaluator->ExtractArray(nested_array_json, &array_value, &is_null));
  EXPECT_TRUE(is_null);
}

TEST(JsonPathEvaluatorTest, DeeplyNestedArrayCausesFailure) {
  const int kNestingDepth = JSONPathExtractor::kMaxParsingDepth + 1;
  const std::string nested_array_json(kNestingDepth, '[');
  std::string json_path = "$";
  for (int i = 0; i < kNestingDepth; ++i) {
    absl::StrAppend(&json_path, "[0]");
  }
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<JsonPathEvaluator> path_evaluator,
      JsonPathEvaluator::Create(json_path, /*sql_standard_mode=*/true));
  std::string value;
  std::vector<std::string> array_value;
  bool is_null = true;
  EXPECT_THAT(path_evaluator->Extract(nested_array_json, &value, &is_null),
              StatusIs(zetasql_base::StatusCode::kOutOfRange,
                       "JSON parsing failed due to deeply nested array/struct. "
                       "Maximum nesting depth is 1000"));
  EXPECT_TRUE(is_null);
  EXPECT_THAT(
      path_evaluator->ExtractScalar(nested_array_json, &value, &is_null),
      StatusIs(zetasql_base::StatusCode::kOutOfRange,
               "JSON parsing failed due to deeply nested array/struct. "
               "Maximum nesting depth is 1000"));
  EXPECT_TRUE(is_null);
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      path_evaluator,
      JsonPathEvaluator::Create(json_path, /*sql_standard_mode=*/false));
  EXPECT_THAT(
      path_evaluator->ExtractArray(nested_array_json, &array_value, &is_null),
      StatusIs(zetasql_base::StatusCode::kOutOfRange,
               "JSON parsing failed due to deeply nested array/struct. "
               "Maximum nesting depth is 1000"));
  EXPECT_TRUE(is_null);
}

TEST(JsonPathEvaluatorTest, ExtractingObjectCloseToLimitSucceeds) {
  const int kNestingDepth = JSONPathExtractor::kMaxParsingDepth;
  std::string nested_object_json;
  for (int i = 0; i < kNestingDepth; ++i) {
    absl::StrAppend(&nested_object_json, "{\"x\":");
  }
  std::string value;
  std::vector<std::string> array_value;
  zetasql_base::Status status;
  bool is_null = true;
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<JsonPathEvaluator> path_evaluator,
      JsonPathEvaluator::Create("$", /*sql_standard_mode=*/true));
  // Extracting should succeed, but the result is null since the objects are not
  // closed.
  ZETASQL_EXPECT_OK(path_evaluator->Extract(nested_object_json, &value, &is_null));
  EXPECT_TRUE(is_null);
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      path_evaluator,
      JsonPathEvaluator::Create("$", /*sql_standard_mode=*/true));
  // Extracting should succeed, but the result is null since the objects are not
  // closed.
  ZETASQL_EXPECT_OK(
      path_evaluator->ExtractScalar(nested_object_json, &value, &is_null));
  EXPECT_TRUE(is_null);
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      path_evaluator,
      JsonPathEvaluator::Create("$", /*sql_standard_mode=*/false));
  // Extracting should succeed, but the result is null since the objects are not
  // closed.
  ZETASQL_EXPECT_OK(
      path_evaluator->ExtractArray(nested_object_json, &array_value, &is_null));
  EXPECT_TRUE(is_null);
}

TEST(JsonPathEvaluatorTest, DeeplyNestedObjectCausesFailure) {
  const int kNestingDepth = JSONPathExtractor::kMaxParsingDepth + 1;
  std::string nested_object_json;
  for (int i = 0; i < kNestingDepth; ++i) {
    absl::StrAppend(&nested_object_json, "{\"x\":");
  }
  std::string json_path = "$";
  for (int i = 0; i < kNestingDepth; ++i) {
    absl::StrAppend(&json_path, ".x");
  }
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<JsonPathEvaluator> path_evaluator,
      JsonPathEvaluator::Create(json_path, /*sql_standard_mode=*/true));

  std::string value;
  std::vector<std::string> array_value;
  bool is_null = true;
  EXPECT_THAT(path_evaluator->Extract(nested_object_json, &value, &is_null),
              StatusIs(zetasql_base::StatusCode::kOutOfRange,
                       "JSON parsing failed due to deeply nested array/struct. "
                       "Maximum nesting depth is 1000"));
  EXPECT_TRUE(is_null);
  EXPECT_THAT(
      path_evaluator->ExtractScalar(nested_object_json, &value, &is_null),
      StatusIs(zetasql_base::StatusCode::kOutOfRange,
               "JSON parsing failed due to deeply nested array/struct. "
               "Maximum nesting depth is 1000"));
  EXPECT_TRUE(is_null);
  ZETASQL_ASSERT_OK_AND_ASSIGN(
      path_evaluator,
      JsonPathEvaluator::Create(json_path, /*sql_standard_mode=*/false));
  EXPECT_THAT(
      path_evaluator->ExtractArray(nested_object_json, &array_value, &is_null),
      StatusIs(zetasql_base::StatusCode::kOutOfRange,
               "JSON parsing failed due to deeply nested array/struct. "
               "Maximum nesting depth is 1000"));
  EXPECT_TRUE(is_null);
}

}  // namespace

}  // namespace json_internal
}  // namespace functions
}  // namespace zetasql

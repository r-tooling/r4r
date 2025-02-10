#include "json.h"
#include <gtest/gtest.h>

TEST(JsonParserTest, NumberTypes) {
    auto int_val = JsonParser::parse("42");
    ASSERT_TRUE(std::holds_alternative<int>(int_val));
    EXPECT_EQ(std::get<int>(int_val), 42);

    auto neg_val = JsonParser::parse("-123");
    ASSERT_TRUE(std::holds_alternative<int>(neg_val));
    EXPECT_EQ(std::get<int>(neg_val), -123);

    auto double_val = JsonParser::parse("42.345");
    ASSERT_TRUE(std::holds_alternative<double>(double_val));
    EXPECT_DOUBLE_EQ(std::get<double>(double_val), 42.345);

    auto exp_val = JsonParser::parse("4.2e3");
    ASSERT_TRUE(std::holds_alternative<double>(exp_val));
    EXPECT_DOUBLE_EQ(std::get<double>(exp_val), 4200.0);

    auto int_exp_val = JsonParser::parse("123.0");
    ASSERT_TRUE(std::holds_alternative<double>(int_exp_val));
    EXPECT_DOUBLE_EQ(std::get<double>(int_exp_val), 123.0);

    auto large_val = JsonParser::parse("2147483648");
    ASSERT_TRUE(std::holds_alternative<double>(large_val));
    EXPECT_DOUBLE_EQ(std::get<double>(large_val), 2147483648.0);
}

TEST(JsonParserTest, ParseObjects) {
    std::string input{R"({
        "number": 42,
        "array": [1, true, "42"],
        "string": "\"with quotes\"",
        "nested": {
            "key": false
        }
    })"};

    auto result = std::get<JsonObject>(JsonParser::parse(input));

    EXPECT_EQ(std::get<int>(result["number"]), 42);
    EXPECT_EQ(std::get<std::string>(result["string"]), "\"with quotes\"");
    auto array = std::get<JsonArray>(result["array"]);
    EXPECT_EQ(array.size(), 3);
    EXPECT_EQ(std::get<int>(array[0]), 1);
    EXPECT_EQ(std::get<bool>(array[1]), true);
    EXPECT_EQ(std::get<std::string>(array[2]), "42");
    auto nested = std::get<JsonObject>(result["nested"]);
    EXPECT_EQ(std::get<bool>(nested["key"]), false);
}

TEST(JsonParserTest, MixedNumberTypes) {
    std::string input{R"({
        "int": 42,
        "double": 3.14,
        "exp": 1e3,
        "big": 9876543210
    })"};

    auto result = std::get<JsonObject>(JsonParser::parse(input));

    EXPECT_TRUE(std::holds_alternative<int>(result.at("int")));
    EXPECT_TRUE(std::holds_alternative<double>(result.at("double")));
    EXPECT_TRUE(std::holds_alternative<double>(result.at("exp")));
    EXPECT_TRUE(std::holds_alternative<double>(result.at("big")));
}

TEST(JsonParserTest, NumberEdgeCases) {
    auto zero_val = JsonParser::parse("0");
    ASSERT_TRUE(std::holds_alternative<int>(zero_val));
    EXPECT_EQ(std::get<int>(zero_val), 0);

    auto neg_zero_val = JsonParser::parse("-0");
    ASSERT_TRUE(std::holds_alternative<int>(neg_zero_val));
    EXPECT_EQ(std::get<int>(neg_zero_val), 0);

    auto lead_zero_val = JsonParser::parse("042");
    ASSERT_TRUE(std::holds_alternative<int>(lead_zero_val));
    EXPECT_EQ(std::get<int>(lead_zero_val), 42);
}

TEST(JsonParserTest, InvalidNumbers) {
    EXPECT_THROW(JsonParser::parse("12.3.4"), JsonParseError);

    EXPECT_THROW(JsonParser::parse("123abc"), JsonParseError);

    EXPECT_THROW(JsonParser::parse("1.2e3.4"), JsonParseError);
}

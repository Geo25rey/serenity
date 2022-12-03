/*
 * Copyright (c) 2022, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTest/TestCase.h>

#include <AK/StringBuilder.h>
#include <AK/Try.h>
#include <AK/UTF8String.h>
#include <AK/Utf8View.h>
#include <AK/Vector.h>

TEST_CASE(construct_empty)
{
    UTF8String empty;
    EXPECT(empty.is_empty());
    EXPECT_EQ(empty.byte_count(), 0u);

    auto empty2 = MUST(UTF8String::from_utf8(""sv));
    EXPECT(empty2.is_empty());
    EXPECT_EQ(empty, empty2);
    EXPECT_EQ(empty, ""sv);
}

TEST_CASE(short_strings)
{
    auto shorty = MUST(UTF8String::from_utf8("abcdefg"sv));
    EXPECT_EQ(shorty.is_short_string(), true);
    EXPECT_EQ(shorty.byte_count(), 7u);
    EXPECT_EQ(shorty.bytes_as_string_view(), "abcdefg"sv);
}

TEST_CASE(long_strings)
{
    auto shorty = MUST(UTF8String::from_utf8("abcdefgh"sv));
    EXPECT_EQ(shorty.is_short_string(), false);
    EXPECT_EQ(shorty.byte_count(), 8u);
    EXPECT_EQ(shorty.bytes_as_string_view(), "abcdefgh"sv);
}

TEST_CASE(substring)
{
    auto superstring = MUST(UTF8String::from_utf8("Hello I am a long string"sv));
    auto short_substring = MUST(superstring.substring(0, 5));
    EXPECT_EQ(short_substring, "Hello"sv);

    auto long_substring = MUST(superstring.substring(0, 10));
    EXPECT_EQ(long_substring, "Hello I am"sv);
}

TEST_CASE(code_points)
{
    auto string = MUST(UTF8String::from_utf8("🦬🪒"sv));

    Vector<u32> code_points;
    for (auto code_point : string.code_points())
        code_points.append(code_point);

    EXPECT_EQ(code_points[0], 0x1f9acu);
    EXPECT_EQ(code_points[1], 0x1fa92u);
}

TEST_CASE(string_builder)
{
    StringBuilder builder;
    builder.append_code_point(0x1f9acu);
    builder.append_code_point(0x1fa92u);

    auto utf8_string = MUST(builder.to_utf8_string());
    EXPECT_EQ(utf8_string, "🦬🪒"sv);
    EXPECT_EQ(utf8_string.byte_count(), 8u);
}

TEST_CASE(ak_format)
{
    auto foo = MUST(UTF8String::formatted("Hello {}", MUST(UTF8String::from_utf8("friends"sv))));
    EXPECT_EQ(foo, "Hello friends"sv);
}

TEST_CASE(replace)
{
    {
        auto haystack = MUST(UTF8String::from_utf8("Hello enemies"sv));
        auto result = MUST(haystack.replace("enemies"sv, "friends"sv, ReplaceMode::All));
        EXPECT_EQ(result, "Hello friends"sv);
    }

    {
        auto base_title = MUST(UTF8String::from_utf8("anon@courage:~"sv));
        auto result = MUST(base_title.replace("[*]"sv, "(*)"sv, ReplaceMode::FirstOnly));
        EXPECT_EQ(result, "anon@courage:~"sv);
    }
}

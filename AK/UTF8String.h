/*
 * Copyright (c) 2018-2022, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Format.h>
#include <AK/Forward.h>
#include <AK/RefCounted.h>
#include <AK/Span.h>
#include <AK/StringView.h>
#include <AK/Traits.h>
#include <AK/Types.h>

namespace AK {

class UTF8StringData;

// UTF8String is a strongly owned sequence of Unicode code points encoded as UTF-8.
// The data may or may not be heap-allocated, and may or may not be reference counted.
// There is no guarantee that the underlying bytes are null-terminated.
class UTF8String {
public:
    // NOTE: For short strings, we avoid heap allocations by storing them in the data pointer slot.
    static constexpr size_t MAX_SHORT_STRING_LENGTH = sizeof(UTF8StringData*) - 1;

    UTF8String(UTF8String const&);
    UTF8String(UTF8String&&);

    UTF8String& operator=(UTF8String&&);
    UTF8String& operator=(UTF8String const&);

    ~UTF8String();

    // Creates an empty (zero-length) UTF8String.
    UTF8String();

    // Creates a new UTF8String from a sequence of UTF-8 encoded code points.
    static ErrorOr<UTF8String> from_utf8(StringView);

    // Creates a substring with a deep copy of the specified data window.
    ErrorOr<UTF8String> substring(size_t start, size_t byte_count) const;

    // Creates a substring that strongly references the origin superstring instead of making a deep copy of the data.
    ErrorOr<UTF8String> substring_with_shared_superstring(size_t start, size_t byte_count) const;

    // Returns an iterable view over the Unicode code points.
    [[nodiscard]] Utf8View code_points() const;

    // Returns a pointer to the underlying UTF-8 encoded bytes.
    // NOTE: There is no guarantee about null-termination.
    [[nodiscard]] u8 const* bytes() const;

    // Returns the number of underlying UTF-8 encoded bytes.
    [[nodiscard]] size_t byte_count() const;

    // Returns true if the UTF8String is zero-length.
    [[nodiscard]] bool is_empty() const;

    // Returns a StringView covering the full length of the string. Note that iterating this will go byte-at-a-time, not code-point-at-a-time.
    [[nodiscard]] StringView bytes_as_string_view() const;

    ErrorOr<UTF8String> replace(StringView needle, StringView replacement, ReplaceMode replace_mode) const;

    [[nodiscard]] bool operator==(UTF8String const&) const;
    [[nodiscard]] bool operator!=(UTF8String const& other) const { return !(*this == other); }

    [[nodiscard]] bool operator==(StringView) const;
    [[nodiscard]] bool operator!=(StringView other) const { return !(*this == other); }

    [[nodiscard]] bool operator<(UTF8String const&) const;
    [[nodiscard]] bool operator<(char const*) const;
    [[nodiscard]] bool operator>=(UTF8String const& other) const { return !(*this < other); }
    [[nodiscard]] bool operator>=(char const* other) const { return !(*this < other); }

    [[nodiscard]] bool operator>(UTF8String const&) const;
    [[nodiscard]] bool operator>(char const*) const;
    [[nodiscard]] bool operator<=(UTF8String const& other) const { return !(*this > other); }
    [[nodiscard]] bool operator<=(char const* other) const { return !(*this > other); }

    [[nodiscard]] bool operator==(char const* cstring) const;
    [[nodiscard]] bool operator!=(char const* cstring) const { return !(*this == cstring); }

    [[nodiscard]] u32 hash() const;

    template<typename T>
    static ErrorOr<UTF8String> number(T value) requires IsArithmetic<T>
    {
        return formatted("{}", value);
    }

    static ErrorOr<UTF8String> vformatted(StringView fmtstr, TypeErasedFormatParams&);

    template<typename... Parameters>
    static ErrorOr<UTF8String> formatted(CheckedFormatString<Parameters...>&& fmtstr, Parameters const&... parameters)
    {
        VariadicFormatParams variadic_format_parameters { parameters... };
        return vformatted(fmtstr.view(), variadic_format_parameters);
    }

    [[nodiscard]] bool is_short_string() const { return reinterpret_cast<uintptr_t>(m_data) & 1; }

    // Creates a new AK::String with the same text.
    // NOTE: This will be removed once all code has been converted.
    String to_ak_string() const;

    // Creates a UTF8String from an AK::String. An error is returned if the input string is not valid UTF-8.
    // NOTE: This will be removed once all code has been converted.
    static ErrorOr<UTF8String> from_ak_string(String const&);

private:
    struct ShortString {
        size_t byte_count() const { return byte_count_and_type >> 1; }

        u8 byte_count_and_type { 0 };
        u8 bytes[MAX_SHORT_STRING_LENGTH] = { 0 };
    };

    explicit UTF8String(NonnullRefPtr<UTF8StringData>);
    explicit UTF8String(ShortString);

    union {
        ShortString m_short_string;
        UTF8StringData* m_data { nullptr };
    };
};

template<>
struct Traits<UTF8String> : public GenericTraits<UTF8String> {
    static unsigned hash(UTF8String const& s) { return s.hash(); }
};

template<>
struct Formatter<UTF8String> : Formatter<StringView> {
    ErrorOr<void> format(FormatBuilder&, UTF8String const&);
};

}

/*
 * Copyright (c) 2018-2022, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Checked.h>
#include <AK/Format.h>
#include <AK/Memory.h>
#include <AK/StringBuilder.h>
#include <AK/UTF8String.h>
#include <AK/Utf8View.h>
#include <stdlib.h>

namespace AK {

class UTF8StringData final : public RefCounted<UTF8StringData> {
public:
    static ErrorOr<NonnullRefPtr<UTF8StringData>> create_uninitialized(size_t, u8*& buffer);
    static ErrorOr<NonnullRefPtr<UTF8StringData>> create_substring(UTF8StringData const& superstring, size_t start, size_t byte_count);
    static ErrorOr<NonnullRefPtr<UTF8StringData>> from_utf8(char const* utf8_bytes, size_t);

    struct SubstringData {
        UTF8StringData const* superstring { nullptr };
        u32 start_offset { 0 };
    };

    void operator delete(void* ptr);

    ~UTF8StringData();

    SubstringData const& substring_data() const
    {
        return *reinterpret_cast<SubstringData const*>(m_bytes_or_substring_data);
    }

    // NOTE: There is no guarantee about null-termination.
    u8 const* bytes() const
    {
        if (m_substring) {
            auto const& data = substring_data();
            return data.superstring->bytes() + data.start_offset;
        }
        return &m_bytes_or_substring_data[0];
    }
    size_t byte_count() const { return m_byte_count; }

    StringView view() const { return { bytes(), byte_count() }; }

    bool operator==(UTF8StringData const& other) const
    {
        return view() == other.view();
    }

    unsigned hash() const
    {
        if (!m_has_hash)
            compute_hash();
        return m_hash;
    }

private:
    explicit UTF8StringData(size_t byte_count);
    UTF8StringData(UTF8StringData const& superstring, size_t start, size_t byte_count);

    void compute_hash() const;

    u32 m_byte_count { 0 };
    mutable unsigned m_hash { 0 };
    mutable bool m_has_hash { false };
    bool m_substring { false };

    u8 m_bytes_or_substring_data[0];
};

UTF8String::UTF8String(NonnullRefPtr<UTF8StringData> data)
    : m_data(&data.leak_ref())
{
}

UTF8String::UTF8String(ShortString short_string)
    : m_short_string(short_string)
{
}

UTF8String::UTF8String(UTF8String const& other)
    : m_data(other.m_data)
{
    if (!is_short_string())
        m_data->ref();
}

UTF8String::UTF8String(UTF8String&& other)
    : m_data(exchange(other.m_data, nullptr))
{
}

UTF8String& UTF8String::operator=(UTF8String&& other)
{
    m_data = exchange(other.m_data, nullptr);
    return *this;
}

UTF8String& UTF8String::operator=(UTF8String const& other)
{
    if (&other != this) {
        m_data = other.m_data;
        if (!is_short_string())
            m_data->ref();
    }
    return *this;
}

UTF8String::~UTF8String()
{
    if (!is_short_string() && m_data)
        m_data->unref();
}

UTF8String::UTF8String()
{
    m_short_string.byte_count_and_type = 0x01;
}

ErrorOr<UTF8String> UTF8String::from_utf8(StringView view)
{
    if (view.length() <= MAX_SHORT_STRING_LENGTH) {
        ShortString short_string;
        if (!view.is_empty())
            memcpy(short_string.bytes, view.characters_without_null_termination(), view.length());
        short_string.byte_count_and_type = (view.length() << 1) | 0x01;
        return UTF8String { short_string };
    }
    auto data = TRY(UTF8StringData::from_utf8(view.characters_without_null_termination(), view.length()));
    return UTF8String { data };
}

StringView UTF8String::bytes_as_string_view() const
{
    return StringView(bytes(), byte_count());
}

u8 const* UTF8String::bytes() const
{
    if (is_short_string())
        return m_short_string.bytes;
    return m_data->bytes();
}

size_t UTF8String::byte_count() const
{
    if (is_short_string())
        return m_short_string.byte_count();
    return m_data->byte_count();
}

bool UTF8String::is_empty() const
{
    return byte_count() == 0;
}

ErrorOr<UTF8String> UTF8String::vformatted(StringView fmtstr, TypeErasedFormatParams& params)
{
    StringBuilder builder;
    TRY(vformat(builder, fmtstr, params));
    return builder.to_utf8_string();
}

bool UTF8String::operator==(UTF8String const& other) const
{
    if (is_short_string())
        return m_data == other.m_data;
    return bytes_as_string_view() == other.bytes_as_string_view();
}

bool UTF8String::operator==(StringView other) const
{
    return bytes_as_string_view() == other;
}

bool UTF8String::operator<(UTF8String const& other) const
{
    return bytes_as_string_view() < other.bytes_as_string_view();
}

bool UTF8String::operator>(UTF8String const& other) const
{
    return bytes_as_string_view() > other.bytes_as_string_view();
}

ErrorOr<UTF8String> UTF8String::substring(size_t start, size_t byte_count) const
{
    if (!byte_count)
        return UTF8String {};
    return UTF8String::from_utf8(bytes_as_string_view().substring_view(start, byte_count));
}

ErrorOr<UTF8String> UTF8String::substring_with_shared_superstring(size_t start, size_t byte_count) const
{
    if (!byte_count)
        return UTF8String {};
    if (byte_count <= MAX_SHORT_STRING_LENGTH)
        return UTF8String::from_utf8(bytes_as_string_view().substring_view(start, byte_count));
    return UTF8String { TRY(UTF8StringData::create_substring(*m_data, start, byte_count)) };
}

bool UTF8String::operator==(char const* c_string) const
{
    return bytes_as_string_view() == c_string;
}

u32 UTF8String::hash() const
{
    if (is_short_string())
        return string_hash(reinterpret_cast<char const*>(bytes()), byte_count());
    return m_data->hash();
}

void UTF8StringData::operator delete(void* ptr)
{
    free(ptr);
}

UTF8StringData::UTF8StringData(size_t byte_count)
    : m_byte_count(byte_count)
{
}

UTF8StringData::UTF8StringData(UTF8StringData const& superstring, size_t start, size_t byte_count)
    : m_byte_count(byte_count)
    , m_substring(true)
{
    auto& data = const_cast<SubstringData&>(substring_data());
    data.start_offset = start;
    data.superstring = &superstring;
    superstring.ref();
}

UTF8StringData::~UTF8StringData()
{
    if (m_substring)
        substring_data().superstring->unref();
}

constexpr size_t allocation_size_for_string_data(size_t length)
{
    return sizeof(UTF8StringData) + (sizeof(char) * length) + sizeof(char);
}

ErrorOr<NonnullRefPtr<UTF8StringData>> UTF8StringData::create_uninitialized(size_t byte_count, u8*& buffer)
{
    VERIFY(byte_count);
    void* slot = malloc(allocation_size_for_string_data(byte_count));
    if (!slot) {
        return Error::from_errno(ENOMEM);
    }
    auto new_string_data = adopt_ref(*new (slot) UTF8StringData(byte_count));
    buffer = const_cast<u8*>(new_string_data->bytes());
    return new_string_data;
}

ErrorOr<NonnullRefPtr<UTF8StringData>> UTF8StringData::from_utf8(char const* utf8_data, size_t byte_count)
{
    // Strings of MAX_SHORT_STRING_LENGTH bytes or less should be handled by the UTF8String short string optimization.
    VERIFY(byte_count > UTF8String::MAX_SHORT_STRING_LENGTH);

    VERIFY(utf8_data);
    u8* buffer = nullptr;
    auto new_string_data = TRY(create_uninitialized(byte_count, buffer));
    memcpy(buffer, utf8_data, byte_count * sizeof(char));
    return new_string_data;
}

ErrorOr<NonnullRefPtr<UTF8StringData>> UTF8StringData::create_substring(UTF8StringData const& superstring, size_t start, size_t byte_count)
{
    // Strings of MAX_SHORT_STRING_LENGTH bytes or less should be handled by the UTF8String short string optimization.
    VERIFY(byte_count > UTF8String::MAX_SHORT_STRING_LENGTH);

    void* slot = malloc(sizeof(UTF8StringData) + sizeof(UTF8StringData::SubstringData));
    if (!slot) {
        return Error::from_errno(ENOMEM);
    }
    return adopt_ref(*new (slot) UTF8StringData(superstring, start, byte_count));
}

void UTF8StringData::compute_hash() const
{
    if (!byte_count())
        m_hash = 0;
    else
        m_hash = string_hash(reinterpret_cast<char const*>(bytes()), byte_count());
    m_has_hash = true;
}

Utf8View UTF8String::code_points() const
{
    return Utf8View(bytes_as_string_view());
}

ErrorOr<void> Formatter<UTF8String>::format(FormatBuilder& builder, UTF8String const& utf8_string)
{
    return Formatter<StringView>::format(builder, utf8_string.bytes_as_string_view());
}

String UTF8String::to_ak_string() const
{
    return String(bytes_as_string_view());
}

ErrorOr<UTF8String> UTF8String::from_ak_string(String const& ak_string)
{
    Utf8View view(ak_string);
    if (!view.validate())
        return Error::from_string_literal("UTF8String::from_ak_strong: Input was not valid UTF-8");
    return UTF8String::from_utf8(ak_string.view());
}

}

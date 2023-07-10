/*
 * Copyright (c) 2022, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma clang optimize off

#include <AK/BitStream.h>
#include <AK/MemoryStream.h>
#include <AK/Stream.h>
#include <LibCompress/Brotli.h>
#include <LibCore/File.h>
#include <LibGfx/Font/Font.h>
#include <LibGfx/Font/OpenType/Font.h>
#include <LibGfx/Font/OpenType/Glyf.h>
#include <LibGfx/Font/WOFF2/Font.h>

namespace WOFF2 {

static constexpr size_t BUFFER_SIZE = 8 * KiB;
static constexpr size_t WOFF2_HEADER_SIZE_IN_BYTES = 48;
static constexpr u32 WOFF2_SIGNATURE = 0x774F4632;
static constexpr u32 TTCF_SIGNAURE = 0x74746366;
static constexpr size_t SFNT_HEADER_SIZE = 12;
static constexpr size_t SFNT_TABLE_SIZE = 16;

[[maybe_unused]] static ErrorOr<u16> read_255_u_short(FixedMemoryStream& stream, ByteBuffer& buffer)
{
    auto next_byte = buffer.span().slice(0, 1);

    u8 code = 0;
    u16 final_value = 0;

    constexpr u8 one_more_byte_code_1 = 255;
    constexpr u8 one_more_byte_code_2 = 254;
    constexpr u8 word_code = 253;
    constexpr u8 lowest_u_code = 253;
    constexpr u16 lowest_u_code_multiplied_by_2 = lowest_u_code * 2;

    TRY(stream.read_until_filled(next_byte));

    code = next_byte[0];

    if (code == word_code) {
        auto two_next_bytes = buffer.span().slice(0, 2);
        TRY(stream.read_until_filled(two_next_bytes));
        final_value = (two_next_bytes[0] << 8);
        final_value |= two_next_bytes[1] & 0xff;
        return final_value;
    }

    if (code == one_more_byte_code_1) {
        TRY(stream.read_until_filled(next_byte));
        final_value = next_byte[0];
        final_value += lowest_u_code;
        return final_value;
    }

    if (code == one_more_byte_code_2) {
        TRY(stream.read_until_filled(next_byte));
        final_value = next_byte[0];
        final_value += lowest_u_code_multiplied_by_2;
        return final_value;
    }
    return code;
}

static ErrorOr<u32> read_uint_base_128(SeekableStream& stream, ByteBuffer& buffer)
{
    auto one_byte_buffer = buffer.span().slice(0, 1);
    u32 accumulator = 0;

    for (u8 i = 0; i < 5; ++i) {
        auto next_byte_buffer = TRY(stream.read_some(one_byte_buffer));
        if (next_byte_buffer.size() != 1)
            return Error::from_string_literal("Not enough data to read UIntBase128 type");

        u8 next_byte = next_byte_buffer[0];

        if (i == 0 && next_byte == 0x80)
            return Error::from_string_literal("UIntBase128 type contains a leading zero");

        if (accumulator & 0xFE000000)
            return Error::from_string_literal("UIntBase128 type exceeds the length of a u32");

        accumulator = (accumulator << 7) | (next_byte & 0x7F);

        if ((next_byte & 0x80) == 0)
            return accumulator;
    }

    return Error::from_string_literal("UIntBase128 type is larger than 5 bytes");
}

static u16 be_u16(u8 const* ptr)
{
    return (((u16)ptr[0]) << 8) | ((u16)ptr[1]);
}

static void be_u16(u8* ptr, u16 value)
{
    ptr[0] = (value >> 8) & 0xff;
    ptr[1] = value & 0xff;
}

static u32 be_u32(u8 const* ptr)
{
    return (((u32)ptr[0]) << 24) | (((u32)ptr[1]) << 16) | (((u32)ptr[2]) << 8) | ((u32)ptr[3]);
}

static void be_u32(u8* ptr, u32 value)
{
    ptr[0] = (value >> 24) & 0xff;
    ptr[1] = (value >> 16) & 0xff;
    ptr[2] = (value >> 8) & 0xff;
    ptr[3] = value & 0xff;
}

static i16 be_i16(u8 const* ptr)
{
    return (((i16)ptr[0]) << 8) | ((i16)ptr[1]);
}

static void be_i16(u8* ptr, i16 value)
{
    ptr[0] = (value >> 8) & 0xff;
    ptr[1] = value & 0xff;
}

static u16 pow_2_less_than_or_equal(u16 x)
{
    u16 result = 1;
    while (result < x)
        result <<= 1;
    return result;
}

enum class TransformationVersion {
    Version0,
    Version1,
    Version2,
    Version3,
};

struct TableDirectoryEntry {
    TransformationVersion transformation_version { TransformationVersion::Version0 };
    DeprecatedString tag;
    u32 original_length { 0 };
    Optional<u32> transform_length;

    u32 tag_to_u32() const
    {
        VERIFY(tag.length() == 4);
        return (static_cast<u8>(tag[0]) << 24)
            | (static_cast<u8>(tag[1]) << 16)
            | (static_cast<u8>(tag[2]) << 8)
            | static_cast<u8>(tag[3]);
    }

    bool has_transformation() const
    {
        return transform_length.has_value();
    }
};

// NOTE: Any tags less than 4 characters long are padded with spaces at the end.
static constexpr Array<StringView, 63> known_tag_names = {
    "cmap"sv,
    "head"sv,
    "hhea"sv,
    "hmtx"sv,
    "maxp"sv,
    "name"sv,
    "OS/2"sv,
    "post"sv,
    "cvt "sv,
    "fpgm"sv,
    "glyf"sv,
    "loca"sv,
    "prep"sv,
    "CFF "sv,
    "VORG"sv,
    "EBDT"sv,
    "EBLC"sv,
    "gasp"sv,
    "hdmx"sv,
    "kern"sv,
    "LTSH"sv,
    "PCLT"sv,
    "VDMX"sv,
    "vhea"sv,
    "vmtx"sv,
    "BASE"sv,
    "GDEF"sv,
    "GPOS"sv,
    "GSUB"sv,
    "EBSC"sv,
    "JSTF"sv,
    "MATH"sv,
    "CBDT"sv,
    "CBLC"sv,
    "COLR"sv,
    "CPAL"sv,
    "SVG "sv,
    "sbix"sv,
    "acnt"sv,
    "avar"sv,
    "bdat"sv,
    "bloc"sv,
    "bsln"sv,
    "cvar"sv,
    "fdsc"sv,
    "feat"sv,
    "fmtx"sv,
    "fvar"sv,
    "gvar"sv,
    "hsty"sv,
    "just"sv,
    "lcar"sv,
    "mort"sv,
    "morx"sv,
    "opbd"sv,
    "prop"sv,
    "trak"sv,
    "Zapf"sv,
    "Silf"sv,
    "Glat"sv,
    "Gloc"sv,
    "Feat"sv,
    "Sill"sv,
};

struct CoordinateTripletEncoding {
    u8 byte_count { 0 };
    u8 x_bits { 0 };
    u8 y_bits { 0 };
    Optional<u16> delta_x;
    Optional<u16> delta_y;
    Optional<bool> positive_x;
    Optional<bool> positive_y;
};

// https://www.w3.org/TR/WOFF2/#triplet_decoding
// 5.2. Decoding of variable-length X and Y coordinates
static CoordinateTripletEncoding const coordinate_triplet_encodings[128] = {
    { 2, 0, 8, {}, 0, {}, false },       // 0
    { 2, 0, 8, {}, 0, {}, true },        // 1
    { 2, 0, 8, {}, 256, {}, false },     // 2
    { 2, 0, 8, {}, 256, {}, true },      // 3
    { 2, 0, 8, {}, 512, {}, false },     // 4
    { 2, 0, 8, {}, 512, {}, true },      // 5
    { 2, 0, 8, {}, 768, {}, false },     // 6
    { 2, 0, 8, {}, 768, {}, true },      // 7
    { 2, 0, 8, {}, 1024, {}, false },    // 8
    { 2, 0, 8, {}, 1024, {}, true },     // 9
    { 2, 8, 0, 0, {}, false, {} },       // 10
    { 2, 8, 0, 0, {}, true, {} },        // 11
    { 2, 8, 0, 256, {}, false, {} },     // 12
    { 2, 8, 0, 256, {}, true, {} },      // 13
    { 2, 8, 0, 512, {}, false, {} },     // 14
    { 2, 8, 0, 512, {}, true, {} },      // 15
    { 2, 8, 0, 768, {}, false, {} },     // 16
    { 2, 8, 0, 768, {}, true, {} },      // 17
    { 2, 8, 0, 1024, {}, false, {} },    // 18
    { 2, 8, 0, 1024, {}, true, {} },     // 19
    { 2, 4, 4, 1, 1, false, false },     // 20
    { 2, 4, 4, 1, 1, true, false },      // 21
    { 2, 4, 4, 1, 1, false, true },      // 22
    { 2, 4, 4, 1, 1, true, true },       // 23
    { 2, 4, 4, 1, 17, false, false },    // 24
    { 2, 4, 4, 1, 17, true, false },     // 25
    { 2, 4, 4, 1, 17, false, true },     // 26
    { 2, 4, 4, 1, 17, true, true },      // 27
    { 2, 4, 4, 1, 33, false, false },    // 28
    { 2, 4, 4, 1, 33, true, false },     // 29
    { 2, 4, 4, 1, 33, false, true },     // 30
    { 2, 4, 4, 1, 33, true, true },      // 31
    { 2, 4, 4, 1, 49, false, false },    // 32
    { 2, 4, 4, 1, 49, true, false },     // 33
    { 2, 4, 4, 1, 49, false, true },     // 34
    { 2, 4, 4, 1, 49, true, true },      // 35
    { 2, 4, 4, 17, 1, false, false },    // 36
    { 2, 4, 4, 17, 1, true, false },     // 37
    { 2, 4, 4, 17, 1, false, true },     // 38
    { 2, 4, 4, 17, 1, true, true },      // 39
    { 2, 4, 4, 17, 17, false, false },   // 40
    { 2, 4, 4, 17, 17, true, false },    // 41
    { 2, 4, 4, 17, 17, false, true },    // 42
    { 2, 4, 4, 17, 17, true, true },     // 43
    { 2, 4, 4, 17, 33, false, false },   // 44
    { 2, 4, 4, 17, 33, true, false },    // 45
    { 2, 4, 4, 17, 33, false, true },    // 46
    { 2, 4, 4, 17, 33, true, true },     // 47
    { 2, 4, 4, 17, 49, false, false },   // 48
    { 2, 4, 4, 17, 49, true, false },    // 49
    { 2, 4, 4, 17, 49, false, true },    // 50
    { 2, 4, 4, 17, 49, true, true },     // 51
    { 2, 4, 4, 33, 1, false, false },    // 52
    { 2, 4, 4, 33, 1, true, false },     // 53
    { 2, 4, 4, 33, 1, false, true },     // 54
    { 2, 4, 4, 33, 1, true, true },      // 55
    { 2, 4, 4, 33, 17, false, false },   // 56
    { 2, 4, 4, 33, 17, true, false },    // 57
    { 2, 4, 4, 33, 17, false, true },    // 58
    { 2, 4, 4, 33, 17, true, true },     // 59
    { 2, 4, 4, 33, 33, false, false },   // 60
    { 2, 4, 4, 33, 33, true, false },    // 61
    { 2, 4, 4, 33, 33, false, true },    // 62
    { 2, 4, 4, 33, 33, true, true },     // 63
    { 2, 4, 4, 33, 49, false, false },   // 64
    { 2, 4, 4, 33, 49, true, false },    // 65
    { 2, 4, 4, 33, 49, false, true },    // 66
    { 2, 4, 4, 33, 49, true, true },     // 67
    { 2, 4, 4, 49, 1, false, false },    // 68
    { 2, 4, 4, 49, 1, true, false },     // 69
    { 2, 4, 4, 49, 1, false, true },     // 70
    { 2, 4, 4, 49, 1, true, true },      // 71
    { 2, 4, 4, 49, 17, false, false },   // 72
    { 2, 4, 4, 49, 17, true, false },    // 73
    { 2, 4, 4, 49, 17, false, true },    // 74
    { 2, 4, 4, 49, 17, true, true },     // 75
    { 2, 4, 4, 49, 33, false, false },   // 76
    { 2, 4, 4, 49, 33, true, false },    // 77
    { 2, 4, 4, 49, 33, false, true },    // 78
    { 2, 4, 4, 49, 33, true, true },     // 79
    { 2, 4, 4, 49, 49, false, false },   // 80
    { 2, 4, 4, 49, 49, true, false },    // 81
    { 2, 4, 4, 49, 49, false, true },    // 82
    { 2, 4, 4, 49, 49, true, true },     // 83
    { 3, 8, 8, 1, 1, false, false },     // 84
    { 3, 8, 8, 1, 1, true, false },      // 85
    { 3, 8, 8, 1, 1, false, true },      // 86
    { 3, 8, 8, 1, 1, true, true },       // 87
    { 3, 8, 8, 1, 257, false, false },   // 88
    { 3, 8, 8, 1, 257, true, false },    // 89
    { 3, 8, 8, 1, 257, false, true },    // 90
    { 3, 8, 8, 1, 257, true, true },     // 91
    { 3, 8, 8, 1, 513, false, false },   // 92
    { 3, 8, 8, 1, 513, true, false },    // 93
    { 3, 8, 8, 1, 513, false, true },    // 94
    { 3, 8, 8, 1, 513, true, true },     // 95
    { 3, 8, 8, 257, 1, false, false },   // 96
    { 3, 8, 8, 257, 1, true, false },    // 97
    { 3, 8, 8, 257, 1, false, true },    // 98
    { 3, 8, 8, 257, 1, true, true },     // 99
    { 3, 8, 8, 257, 257, false, false }, // 100
    { 3, 8, 8, 257, 257, true, false },  // 101
    { 3, 8, 8, 257, 257, false, true },  // 102
    { 3, 8, 8, 257, 257, true, true },   // 103
    { 3, 8, 8, 257, 513, false, false }, // 104
    { 3, 8, 8, 257, 513, true, false },  // 105
    { 3, 8, 8, 257, 513, false, true },  // 106
    { 3, 8, 8, 257, 513, true, true },   // 107
    { 3, 8, 8, 513, 1, false, false },   // 108
    { 3, 8, 8, 513, 1, true, false },    // 109
    { 3, 8, 8, 513, 1, false, true },    // 110
    { 3, 8, 8, 513, 1, true, true },     // 111
    { 3, 8, 8, 513, 257, false, false }, // 112
    { 3, 8, 8, 513, 257, true, false },  // 113
    { 3, 8, 8, 513, 257, false, true },  // 114
    { 3, 8, 8, 513, 257, true, true },   // 115
    { 3, 8, 8, 513, 513, false, false }, // 116
    { 3, 8, 8, 513, 513, true, false },  // 117
    { 3, 8, 8, 513, 513, false, true },  // 118
    { 3, 8, 8, 513, 513, true, true },   // 119
    { 4, 12, 12, 0, 0, false, false },   // 120
    { 4, 12, 12, 0, 0, true, false },    // 121
    { 4, 12, 12, 0, 0, false, true },    // 122
    { 4, 12, 12, 0, 0, true, true },     // 123
    { 5, 16, 16, 0, 0, false, false },   // 124
    { 5, 16, 16, 0, 0, true, false },    // 125
    { 5, 16, 16, 0, 0, false, true },    // 126
    { 5, 16, 16, 0, 0, true, true },     // 127
};

struct FontPoint {
    i16 x { 0 };
    i16 y { 0 };
    bool on_curve { false };
};

static ErrorOr<Vector<FontPoint>> retrieve_points_of_simple_glyph(FixedMemoryStream& flags_stream, FixedMemoryStream& glyph_stream, u16 number_of_points, ByteBuffer& read_buffer)
{
    Vector<FontPoint> points;
    TRY(points.try_ensure_capacity(number_of_points));

    i16 x = 0;
    i16 y = 0;

    for (u32 point = 0; point < number_of_points; ++point) {
        u8 flags = TRY(flags_stream.read_value<u8>());
        bool on_curve = (flags & 0x80) == 0;
        u8 coordinate_triplet_index = flags & 0x7F;

        dbgln("point {}: flags: 0x{:02x}, on_curve: {}, coordinate_triplet_index: {}", point, flags, on_curve, coordinate_triplet_index);

        auto& coordinate_triplet_encoding = coordinate_triplet_encodings[coordinate_triplet_index];

        // The byte_count in the array accounts for the flags, but we already read them in from a different stream.
        u8 byte_count_not_including_flags = coordinate_triplet_encoding.byte_count - 1;

        auto point_coordinates = read_buffer.span().slice(0, byte_count_not_including_flags);
        TRY(glyph_stream.read_until_filled(point_coordinates));
        dbgln("pcs: {}, bcnif: {}", point_coordinates.size(), byte_count_not_including_flags);

        int delta_x = 0;
        int delta_y = 0;

        dbgln("xbits: {}, ybits: {}", coordinate_triplet_encoding.x_bits, coordinate_triplet_encoding.y_bits);

        switch (coordinate_triplet_encoding.x_bits) {
        case 0:
            break;
        case 4:
            delta_x = static_cast<i16>(point_coordinates[0] >> 4);
            break;
        case 8:
            delta_x = static_cast<i16>(point_coordinates[0]);
            break;
        case 12:
            delta_x = (static_cast<i16>(point_coordinates[0]) << 4) | (static_cast<i16>(point_coordinates[1]) >> 4);
            break;
        case 16:
            delta_x = be_i16(point_coordinates.data());
            break;
        default:
            VERIFY_NOT_REACHED();
        }

        switch (coordinate_triplet_encoding.y_bits) {
        case 0:
            break;
        case 4:
            delta_y = static_cast<i16>(point_coordinates[0] & 0x0f);
            break;
        case 8:
            delta_y = byte_count_not_including_flags == 2 ? static_cast<i16>(point_coordinates[1]) : static_cast<i16>(point_coordinates[0]);
            break;
        case 12:
            delta_y = (static_cast<i16>(point_coordinates[1] & 0x0f) << 8) | static_cast<i16>(point_coordinates[2]);
            break;
        case 16:
            delta_y = be_i16(point_coordinates.offset(2));
            break;
        default:
            VERIFY_NOT_REACHED();
        }

        if (coordinate_triplet_encoding.delta_x.has_value()) {
            if (Checked<i16>::addition_would_overflow(delta_x, coordinate_triplet_encoding.delta_x.value()))
                return Error::from_string_literal("EOVERFLOW 3");

            delta_x += coordinate_triplet_encoding.delta_x.value();
        }

        if (coordinate_triplet_encoding.delta_y.has_value()) {
            if (Checked<i16>::addition_would_overflow(delta_y, coordinate_triplet_encoding.delta_y.value()))
                return Error::from_string_literal("EOVERFLOW 4");

            delta_y += coordinate_triplet_encoding.delta_y.value();
        }

        if (coordinate_triplet_encoding.positive_x.has_value() && !coordinate_triplet_encoding.positive_x.value())
            delta_x = -delta_x;

        if (coordinate_triplet_encoding.positive_y.has_value() && !coordinate_triplet_encoding.positive_y.value())
            delta_y = -delta_y;

        if (Checked<i16>::addition_would_overflow(x, delta_x))
            return Error::from_string_literal("EOVERFLOW 5");

        if (Checked<i16>::addition_would_overflow(y, delta_y))
            return Error::from_string_literal("EOVERFLOW 6");

        x += delta_x;
        y += delta_y;

        points.unchecked_append(FontPoint { .x = x, .y = y, .on_curve = on_curve });
    }

    return points;
}

static constexpr size_t TRANSFORMED_GLYF_TABLE_HEADER_SIZE_IN_BYTES = 36;

enum class LocaElementSize {
    TwoBytes,
    FourBytes,
};

struct GlyfAndLocaTableBuffers {
    ByteBuffer glyf_table;
    ByteBuffer loca_table;
};

enum SimpleGlyphFlags : u8 {
    OnCurve = 0x01,
    XShortVector = 0x02,
    YShortVector = 0x04,
    RepeatFlag = 0x08,
    XIsSameOrPositiveXShortVector = 0x10,
    YIsSameOrPositiveYShortVector = 0x20,
};

enum CompositeGlyphFlags : u16 {
    Arg1AndArg2AreWords = 0x0001,
    ArgsAreXYValues = 0x0002,
    RoundXYToGrid = 0x0004,
    WeHaveAScale = 0x0008,
    MoreComponents = 0x0020,
    WeHaveAnXAndYScale = 0x0040,
    WeHaveATwoByTwo = 0x0080,
    WeHaveInstructions = 0x0100,
    UseMyMetrics = 0x0200,
    OverlapCompound = 0x0400,
    ScaledComponentOffset = 0x0800,
    UnscaledComponentOffset = 0x1000,
};

static ErrorOr<GlyfAndLocaTableBuffers> create_glyf_and_loca_tables_from_transformed_glyf_table(FixedMemoryStream& table_stream, ByteBuffer& read_buffer)
{
    auto header_read_buffer = read_buffer.span().slice(0, TRANSFORMED_GLYF_TABLE_HEADER_SIZE_IN_BYTES);
    auto header_buffer = TRY(table_stream.read_some(header_read_buffer));
    dbgln("1");
    if (header_buffer.size() != TRANSFORMED_GLYF_TABLE_HEADER_SIZE_IN_BYTES)
        return Error::from_string_literal("Not enough data to read header of transformed glyf table");

    for (size_t i = 0; i < header_buffer.size(); ++i)
        dbgln("head byte {}: 0x{:02x}", i, header_buffer[i]);

    // Skip: reserved, optionFlags
    u16 num_glyphs = be_u16(header_buffer.offset(4));
    u16 index_format = be_u16(header_buffer.offset(6));
    auto loca_element_size = index_format == 0 ? LocaElementSize::TwoBytes : LocaElementSize::FourBytes;

    dbgln("num glyphs: {}, index format: {}", num_glyphs, index_format);

    u32 number_of_contours_stream_size = be_u32(header_buffer.offset(8));
    u32 number_of_points_stream_size = be_u32(header_buffer.offset(12));
    u32 flag_stream_size = be_u32(header_buffer.offset(16));
    u32 glyph_stream_size = be_u32(header_buffer.offset(20));
    u32 composite_stream_size = be_u32(header_buffer.offset(24));
    u32 bounding_box_stream_size = be_u32(header_buffer.offset(28));
    u32 instruction_stream_size = be_u32(header_buffer.offset(32));

    size_t table_size = TRY(table_stream.size());
    u64 total_size_of_streams = number_of_contours_stream_size;
    total_size_of_streams += number_of_points_stream_size;
    total_size_of_streams += flag_stream_size;
    total_size_of_streams += glyph_stream_size;
    total_size_of_streams += composite_stream_size;
    total_size_of_streams += bounding_box_stream_size;
    total_size_of_streams += instruction_stream_size;

    dbgln("table_size: {}, tsos: {}", table_size, total_size_of_streams);
    if (table_size < total_size_of_streams)
        return Error::from_string_literal("Not enough data to read in streams of transformed glyf table");

    auto all_tables_buffer = TRY(ByteBuffer::create_zeroed(total_size_of_streams));
    u64 all_tables_buffer_offset = 0;

    dbgln("nocss: {}", number_of_contours_stream_size);
    auto number_of_contours_stream_buffer = TRY(table_stream.read_some(all_tables_buffer.span().slice(all_tables_buffer_offset, number_of_contours_stream_size)));
    auto number_of_contours_stream = FixedMemoryStream(number_of_contours_stream_buffer);
    all_tables_buffer_offset += number_of_contours_stream_size;

    dbgln("nopss: {}", number_of_points_stream_size);
    auto number_of_points_stream_buffer = TRY(table_stream.read_some(all_tables_buffer.span().slice(all_tables_buffer_offset, number_of_points_stream_size)));
    auto number_of_points_stream = FixedMemoryStream(number_of_points_stream_buffer);
    all_tables_buffer_offset += number_of_points_stream_size;

    dbgln("fss: {}", flag_stream_size);
    auto flag_stream_buffer = all_tables_buffer.span().slice(all_tables_buffer_offset, flag_stream_size);
    TRY(table_stream.read_until_filled(flag_stream_buffer));
    auto flag_stream = FixedMemoryStream(flag_stream_buffer);
    all_tables_buffer_offset += flag_stream_size;

    dbgln("gss: {}", glyph_stream_size);
    auto glyph_stream_buffer = all_tables_buffer.span().slice(all_tables_buffer_offset, glyph_stream_size);
    TRY(table_stream.read_until_filled(glyph_stream_buffer));
    dbgln("gsbs: {}", glyph_stream_buffer.size());
    auto glyph_stream = FixedMemoryStream(glyph_stream_buffer);
    all_tables_buffer_offset += glyph_stream_size;

    auto composite_stream_buffer = all_tables_buffer.span().slice(all_tables_buffer_offset, composite_stream_size);
    TRY(table_stream.read_until_filled(composite_stream_buffer));
    auto composite_stream = FixedMemoryStream(composite_stream_buffer);
    all_tables_buffer_offset += composite_stream_size;

    size_t bounding_box_bitmap_length = ((num_glyphs + 31) >> 5) << 2;
    auto bounding_box_bitmap_stream_buffer = TRY(table_stream.read_some(all_tables_buffer.span().slice(all_tables_buffer_offset, bounding_box_bitmap_length)));
    auto bounding_box_bitmap_memory_stream = FixedMemoryStream(bounding_box_bitmap_stream_buffer);
    auto bounding_box_bitmap_bit_stream = BigEndianInputBitStream { MaybeOwned<Stream>(bounding_box_bitmap_memory_stream) };
    all_tables_buffer_offset += bounding_box_bitmap_length;

    if (bounding_box_stream_size < bounding_box_bitmap_length)
        return Error::from_string_literal("Not enough data to read bounding box stream of transformed glyf table");
    auto bounding_box_stream_buffer = TRY(table_stream.read_some(all_tables_buffer.span().slice(all_tables_buffer_offset, bounding_box_stream_size - bounding_box_bitmap_length)));
    auto bounding_box_stream = FixedMemoryStream(bounding_box_stream_buffer);
    all_tables_buffer_offset += bounding_box_stream_size - bounding_box_bitmap_length;

    auto instruction_buffer = all_tables_buffer.span().slice(all_tables_buffer_offset, instruction_stream_size);
    TRY(table_stream.read_until_filled(instruction_buffer));
    dbgln("iss: {}, ibs: {}", instruction_stream_size, instruction_buffer.size());
    auto instruction_stream = FixedMemoryStream(instruction_buffer);

    ByteBuffer reconstructed_glyf_table_buffer;
    Vector<u32> loca_indexes;

    auto append_u16 = [&](u16 value) -> ErrorOr<void> {
        auto end = reconstructed_glyf_table_buffer.size();
        TRY(reconstructed_glyf_table_buffer.try_resize(reconstructed_glyf_table_buffer.size() + sizeof(value)));
        auto* slot = reconstructed_glyf_table_buffer.offset_pointer(end);
        be_u16(slot, value);
        return {};
    };

    auto append_i16 = [&](i16 value) -> ErrorOr<void> {
        auto end = reconstructed_glyf_table_buffer.size();
        TRY(reconstructed_glyf_table_buffer.try_resize(reconstructed_glyf_table_buffer.size() + sizeof(value)));
        auto* slot = reconstructed_glyf_table_buffer.offset_pointer(end);
        be_i16(slot, value);
        return {};
    };

    auto append_bytes = [&](ReadonlyBytes bytes) -> ErrorOr<void> {
        TRY(reconstructed_glyf_table_buffer.try_append(bytes));
        return {};
    };

    auto transfer_bytes = [&](ByteBuffer& dst, FixedMemoryStream& src, size_t count) -> ErrorOr<void> {
        auto end = dst.size();
        TRY(dst.try_resize(dst.size() + count));
        auto* slot = dst.offset_pointer(end);
        TRY(src.read_until_filled(Bytes { slot, count }));
        return {};
    };

    for (size_t glyph_index = 0; glyph_index < num_glyphs; ++glyph_index) {
        dbgln("glyph_index: {}", glyph_index);

        size_t starting_glyf_table_size = reconstructed_glyf_table_buffer.size();

        bool has_bounding_box = TRY(bounding_box_bitmap_bit_stream.read_bit());

        auto number_of_contours = TRY(number_of_contours_stream.read_value<BigEndian<i16>>());
        dbgln("noc: {}", number_of_contours);

        if (number_of_contours == 0) {
            // Empty glyph

            // Reconstruction of an empty glyph (when nContour = 0) is a simple step
            // that involves incrementing the glyph record count and creating a new entry in the loca table
            // where loca[n] = loca[n-1].

            // If the bboxBitmap flag indicates that the bounding box values are explicitly encoded in the bboxStream
            // the decoder MUST reject WOFF2 file as invalid.
            if (has_bounding_box)
                return Error::from_string_literal("Empty glyphs cannot have an explicit bounding box");
        } else if (number_of_contours < 0) {
            // Decoding of Composite Glyphs

            [[maybe_unused]] i16 bounding_box_x_min = 0;
            [[maybe_unused]] i16 bounding_box_y_min = 0;
            [[maybe_unused]] i16 bounding_box_x_max = 0;
            [[maybe_unused]] i16 bounding_box_y_max = 0;

            if (has_bounding_box) {
                bounding_box_x_min = TRY(bounding_box_stream.read_value<BigEndian<i16>>());
                bounding_box_y_min = TRY(bounding_box_stream.read_value<BigEndian<i16>>());
                bounding_box_x_max = TRY(bounding_box_stream.read_value<BigEndian<i16>>());
                bounding_box_y_max = TRY(bounding_box_stream.read_value<BigEndian<i16>>());
            }

            TRY(append_i16(number_of_contours));
            TRY(append_i16(bounding_box_x_min));
            TRY(append_i16(bounding_box_y_min));
            TRY(append_i16(bounding_box_x_max));
            TRY(append_i16(bounding_box_y_max));

            bool have_instructions = false;
            u16 flags = to_underlying(OpenType::CompositeGlyfFlags::MoreComponents);
            while (flags & to_underlying(OpenType::CompositeGlyfFlags::MoreComponents)) {
                // 1a. Read a UInt16 from compositeStream. This is interpreted as a component flag word as in the TrueType spec.
                //     Based on the flag values, there are between 4 and 14 additional argument bytes,
                //     interpreted as glyph index, arg1, arg2, and optional scale or affine matrix.

                flags = TRY(composite_stream.read_value<BigEndian<u16>>());

                if (flags & to_underlying(OpenType::CompositeGlyfFlags::WeHaveInstructions)) {
                    have_instructions = true;
                }

                // 2a. Read the number of argument bytes as determined in step 1a from the composite stream,
                //     and store these in the reconstructed glyph.
                //     If the flag word read in step 1a has the FLAG_MORE_COMPONENTS bit (bit 5) set, go back to step 1a.

                size_t argument_byte_count = 2;

                if (flags & to_underlying(OpenType::CompositeGlyfFlags::Arg1AndArg2AreWords)) {
                    argument_byte_count += 4;
                } else {
                    argument_byte_count += 2;
                }

                if (flags & to_underlying(OpenType::CompositeGlyfFlags::WeHaveAScale)) {
                    argument_byte_count += 2;
                } else if (flags & to_underlying(OpenType::CompositeGlyfFlags::WeHaveAnXAndYScale)) {
                    argument_byte_count += 4;
                } else if (flags & to_underlying(OpenType::CompositeGlyfFlags::WeHaveATwoByTwo)) {
                    argument_byte_count += 8;
                }

                TRY(append_u16(flags));
                TRY(transfer_bytes(reconstructed_glyf_table_buffer, composite_stream, argument_byte_count));
            }

            if (have_instructions) {
                auto buffer = TRY(ByteBuffer::create_zeroed(2));
                auto number_of_instructions = TRY(read_255_u_short(glyph_stream, buffer));
                TRY(append_u16(number_of_instructions));

                if (number_of_instructions) {
                    auto instructions = TRY(ByteBuffer::create_zeroed(number_of_instructions));
                    TRY(instruction_stream.read_until_filled(instructions));
                    TRY(reconstructed_glyf_table_buffer.try_append(instructions));
                }
            }

            dbgln("Done with composite glyph");
        } else if (number_of_contours > 0) {
            // Decoding of Simple Glyphs

            // For a simple glyph (when nContour > 0), the process continues as follows:
            // Each of these is the number of points of that contour.
            // Convert this into the endPtsOfContours[] array by computing the cumulative sum, then subtracting one.

            Vector<size_t> end_points_of_contours;
            size_t number_of_points = 0;

            for (size_t contour_index = 0; contour_index < static_cast<size_t>(number_of_contours); ++contour_index) {
                size_t number_of_points_for_this_contour = TRY(read_255_u_short(number_of_points_stream, read_buffer));
                if (Checked<size_t>::addition_would_overflow(number_of_points, number_of_points_for_this_contour))
                    return Error::from_string_literal("EOVERFLOW 1");

                number_of_points += number_of_points_for_this_contour;
                if (number_of_points == 0)
                    return Error::from_string_literal("EOVERFLOW 2");

                end_points_of_contours.append(number_of_points - 1);
            }

            auto points = TRY(retrieve_points_of_simple_glyph(flag_stream, glyph_stream, number_of_points, read_buffer));

            for (size_t i = 0; i < points.size(); ++i)
                dbgln("point {}: on_curve: {}, x: {}, y: {}", i, points[i].on_curve, points[i].x, points[i].y);

            auto instruction_size = TRY(read_255_u_short(glyph_stream, read_buffer));
            auto instructions_buffer = TRY(ByteBuffer::create_zeroed(instruction_size));
            if (instruction_size != 0)
                TRY(instruction_stream.read_until_filled(instructions_buffer));

            i16 bounding_box_x_min = 0;
            i16 bounding_box_y_min = 0;
            i16 bounding_box_x_max = 0;
            i16 bounding_box_y_max = 0;

            if (has_bounding_box) {
                bounding_box_x_min = TRY(bounding_box_stream.read_value<BigEndian<i16>>());
                bounding_box_y_min = TRY(bounding_box_stream.read_value<BigEndian<i16>>());
                bounding_box_x_max = TRY(bounding_box_stream.read_value<BigEndian<i16>>());
                bounding_box_y_max = TRY(bounding_box_stream.read_value<BigEndian<i16>>());
            } else {
                for (size_t point_index = 0; point_index < points.size(); ++point_index) {
                    auto& point = points.at(point_index);

                    if (point_index == 0) {
                        bounding_box_x_min = bounding_box_x_max = point.x;
                        bounding_box_y_min = bounding_box_y_max = point.y;
                        continue;
                    }

                    bounding_box_x_min = min(bounding_box_x_min, point.x);
                    bounding_box_x_max = max(bounding_box_x_max, point.x);
                    bounding_box_y_min = min(bounding_box_y_min, point.y);
                    bounding_box_y_max = max(bounding_box_y_max, point.y);
                }
            }

            TRY(append_i16(number_of_contours));
            TRY(append_i16(bounding_box_x_min));
            TRY(append_i16(bounding_box_y_min));
            TRY(append_i16(bounding_box_x_max));
            TRY(append_i16(bounding_box_y_max));

            dbgln("offset after header: {}", reconstructed_glyf_table_buffer.size() - starting_glyf_table_size);

            for (auto end_point : end_points_of_contours)
                TRY(append_u16(end_point));

            dbgln("offset after endPointsOfContours: {}", reconstructed_glyf_table_buffer.size() - starting_glyf_table_size);

            TRY(append_u16(instruction_size));
            if (instruction_size != 0)
                TRY(append_bytes(instructions_buffer));

            dbgln("offset after insns: {}", reconstructed_glyf_table_buffer.size() - starting_glyf_table_size);

            Vector<FontPoint> relative_points;
            TRY(relative_points.try_ensure_capacity(points.size()));

            {
                i16 previous_point_x = 0;
                i16 previous_point_y = 0;
                for (auto& point : points) {
                    i16 x = point.x - previous_point_x;
                    i16 y = point.y - previous_point_y;
                    relative_points.unchecked_append({ x, y, point.on_curve });
                    previous_point_x = point.x;
                    previous_point_y = point.y;
                }
            }

            Optional<u8> last_flags;
            u8 repeat_count = 0;

            for (auto& point : relative_points) {
                u8 flags = 0;

                // FIXME: Implement flag deduplication (setting the REPEAT_FLAG for a string of same flags)

                if (point.on_curve)
                    flags |= SimpleGlyphFlags::OnCurve;

                if (point.x == 0) {
                    flags |= SimpleGlyphFlags::XIsSameOrPositiveXShortVector;
                } else if (point.x > -256 && point.x < 256) {
                    flags |= SimpleGlyphFlags::XShortVector;

                    if (point.x > 0)
                        flags |= SimpleGlyphFlags::XIsSameOrPositiveXShortVector;
                }

                if (point.y == 0) {
                    flags |= SimpleGlyphFlags::YIsSameOrPositiveYShortVector;
                } else if (point.y > -256 && point.y < 256) {
                    flags |= SimpleGlyphFlags::YShortVector;

                    if (point.y > 0)
                        flags |= SimpleGlyphFlags::YIsSameOrPositiveYShortVector;
                }

                if (last_flags.has_value() && flags == last_flags.value() && repeat_count != 0xff) {
                    // NOTE: Update the previous entry to say it's repeating.
                    reconstructed_glyf_table_buffer[reconstructed_glyf_table_buffer.size() - 1] |= SimpleGlyphFlags::RepeatFlag;
                    ++repeat_count;
                } else {
                    if (repeat_count != 0) {
                        TRY(reconstructed_glyf_table_buffer.try_append(repeat_count));
                        repeat_count = 0;
                    }
                    TRY(reconstructed_glyf_table_buffer.try_append(flags));
                }
                last_flags = flags;
            }
            if (repeat_count != 0) {
                TRY(reconstructed_glyf_table_buffer.try_append(repeat_count));
            }

            dbgln("offset after flags: {}", reconstructed_glyf_table_buffer.size() - starting_glyf_table_size);

            for (auto& point : relative_points) {
                dbgln("x delta {}", point.x);
                if (point.x == 0) {
                    // No need to write to the table.
                } else if (point.x > -256 && point.x < 256) {
                    dbgln(" -> short");
                    TRY(reconstructed_glyf_table_buffer.try_append(abs(point.x)));
                } else {
                    dbgln(" -> long");
                    TRY(append_i16(point.x));
                }
            }

            dbgln("offset after x values: {}", reconstructed_glyf_table_buffer.size() - starting_glyf_table_size);

            for (auto& point : relative_points) {
                dbgln("y delta {}", point.y);
                if (point.y == 0) {
                    // No need to write to the table.
                } else if (point.y > -256 && point.y < 256) {
                    dbgln(" -> short");
                    TRY(reconstructed_glyf_table_buffer.try_append(abs(point.y)));
                } else {
                    dbgln(" -> long");
                    TRY(append_i16(point.y));
                }
            }

            dbgln("offset after y values: {}", reconstructed_glyf_table_buffer.size() - starting_glyf_table_size);

            while (reconstructed_glyf_table_buffer.size() % 4 != 0) {
                TRY(reconstructed_glyf_table_buffer.try_append(0));
            }
        }

        dbgln("nli: {}", starting_glyf_table_size);
        loca_indexes.append(starting_glyf_table_size);
    }

    loca_indexes.append(reconstructed_glyf_table_buffer.size());

    size_t loca_element_size_in_bytes = loca_element_size == LocaElementSize::TwoBytes ? sizeof(u16) : sizeof(u32);
    size_t loca_table_buffer_size = loca_indexes.size() * loca_element_size_in_bytes;
    ByteBuffer loca_table_buffer = TRY(ByteBuffer::create_zeroed(loca_table_buffer_size));
    for (size_t loca_indexes_index = 0; loca_indexes_index < loca_indexes.size(); ++loca_indexes_index) {
        u32 loca_index = loca_indexes.at(loca_indexes_index);
        size_t loca_offset = loca_indexes_index * loca_element_size_in_bytes;

        if (loca_element_size == LocaElementSize::TwoBytes) {
            dbgln("2B loca_index: {:#08x} >> 1 == {:#08x}", loca_index, loca_index >> 1);
            be_u16(loca_table_buffer.offset_pointer(loca_offset), loca_index >> 1);
        } else {
            dbgln("4B loca_index: {:#08x}", loca_index);
            be_u32(loca_table_buffer.offset_pointer(loca_offset), loca_index);
        }
    }

    dbgln("gts: {}, lts: {}", reconstructed_glyf_table_buffer.size(), loca_table_buffer.size());
    //    for (size_t i = 0; i < reconstructed_glyf_table_buffer.size(); ++i)
    //        dbgln("rgtb[{}] = 0x{:02x}", i, reconstructed_glyf_table_buffer[i]);
    return GlyfAndLocaTableBuffers { .glyf_table = move(reconstructed_glyf_table_buffer), .loca_table = move(loca_table_buffer) };
}

ErrorOr<NonnullRefPtr<Font>> Font::try_load_from_file(StringView path)
{
    auto woff2_file_stream = TRY(Core::File::open(path, Core::File::OpenMode::Read));
    return try_load_from_externally_owned_memory(*woff2_file_stream);
}

ErrorOr<NonnullRefPtr<Font>> Font::try_load_from_externally_owned_memory(ReadonlyBytes bytes)
{
    FixedMemoryStream stream(bytes);
    return try_load_from_externally_owned_memory(stream);
}

ErrorOr<NonnullRefPtr<Font>> Font::try_load_from_externally_owned_memory(SeekableStream& stream)
{
    auto stream_size = TRY(stream.size());
    auto read_buffer = TRY(ByteBuffer::create_zeroed(BUFFER_SIZE));

    auto header_buffer = read_buffer.span().slice(0, WOFF2_HEADER_SIZE_IN_BYTES);
    auto header_bytes = TRY(stream.read_some(header_buffer));
    if (header_bytes.size() != WOFF2_HEADER_SIZE_IN_BYTES)
        return Error::from_string_literal("WOFF2 file too small");

    // The signature field in the WOFF2 header MUST contain the value of 0x774F4632 ('wOF2'), which distinguishes it from WOFF 1.0 files.
    // If the field does not contain this value, user agents MUST reject the file as invalid.
    u32 signature = be_u32(header_bytes.data());
    dbgln("woff2 signature: 0x{:08x}", signature);
    if (signature != WOFF2_SIGNATURE)
        return Error::from_string_literal("Invalid WOFF2 signature");

    // The interpretation of the WOFF2 Header is the same as the WOFF Header in [WOFF1], with the addition of one new totalCompressedSize field.
    // NOTE: See WOFF/Font.cpp for more comments about this.
    u32 flavor = be_u32(header_bytes.offset(4));      // The "sfnt version" of the input font.
    u32 length = be_u32(header_bytes.offset(8));      // Total size of the WOFF file.
    u16 num_tables = be_u16(header_bytes.offset(12)); // Number of entries in directory of font tables.
    // Skip: reserved
    u32 total_sfnt_size = be_u32(header_bytes.offset(16));       // Total size needed for the uncompressed font data, including the sfnt header, directory, and font tables (including padding).
    u32 total_compressed_size = be_u32(header_bytes.offset(20)); // Total length of the compressed data block.
    // Skip: major_version, minor_version
    u32 meta_offset = be_u32(header_bytes.offset(28)); // Offset to metadata block, from beginning of WOFF file.
    u32 meta_length = be_u32(header_bytes.offset(32)); // Length of compressed metadata block.
    // Skip: meta_orig_length
    u32 priv_offset = be_u32(header_bytes.offset(40)); // Offset to private data block, from beginning of WOFF file.
    u32 priv_length = be_u32(header_bytes.offset(44)); // Length of private data block.

    if (length > stream_size)
        return Error::from_string_literal("Invalid WOFF length");
    if (meta_length == 0 && meta_offset != 0)
        return Error::from_string_literal("Invalid WOFF meta block offset");
    if (priv_length == 0 && priv_offset != 0)
        return Error::from_string_literal("Invalid WOFF private block offset");
    if (flavor == TTCF_SIGNAURE)
        return Error::from_string_literal("Font collections not yet supported");

    // NOTE: "The "totalSfntSize" value in the WOFF2 Header is intended to be used for reference purposes only. It may represent the size of the uncompressed input font file,
    //        but if the transformed 'glyf' and 'loca' tables are present, the uncompressed size of the reconstructed tables and the total decompressed font size may differ
    //        substantially from the original total size specified in the WOFF2 Header."
    //        We use it as an initial size of the font buffer and extend it as necessary.
    auto font_buffer = TRY(ByteBuffer::create_zeroed(total_sfnt_size));

    // ISO-IEC 14496-22:2019 4.5.1 Offset table
    constexpr size_t OFFSET_TABLE_SIZE_IN_BYTES = 12;
    TRY(font_buffer.try_ensure_capacity(OFFSET_TABLE_SIZE_IN_BYTES));
    u16 search_range = pow_2_less_than_or_equal(num_tables);
    be_u32(font_buffer.data() + 0, flavor);
    be_u16(font_buffer.data() + 4, num_tables);
    be_u16(font_buffer.data() + 6, search_range * 16);
    be_u16(font_buffer.data() + 8, log2(search_range));
    be_u16(font_buffer.data() + 10, num_tables * 16 - search_range * 16);

    Vector<TableDirectoryEntry> table_entries;
    TRY(table_entries.try_ensure_capacity(num_tables));

    auto one_byte_buffer = read_buffer.span().slice(0, 1);
    u64 total_length_of_all_tables = 0;

    for (size_t table_entry_index = 0; table_entry_index < num_tables; ++table_entry_index) {
        TableDirectoryEntry table_directory_entry;
        auto flags_byte_buffer = TRY(stream.read_some(one_byte_buffer));
        if (flags_byte_buffer.size() != 1)
            return Error::from_string_literal("Not enough data to read flags entry of table directory entry");

        u8 flags_byte = flags_byte_buffer[0];

        switch ((flags_byte & 0xC0) >> 6) {
        case 0:
            table_directory_entry.transformation_version = TransformationVersion::Version0;
            break;
        case 1:
            table_directory_entry.transformation_version = TransformationVersion::Version1;
            break;
        case 2:
            table_directory_entry.transformation_version = TransformationVersion::Version2;
            break;
        case 3:
            table_directory_entry.transformation_version = TransformationVersion::Version3;
            break;
        default:
            VERIFY_NOT_REACHED();
        }

        u8 tag_number = flags_byte & 0x3F;

        if (tag_number != 0x3F) {
            table_directory_entry.tag = known_tag_names[tag_number];
        } else {
            auto four_byte_buffer = read_buffer.span().slice(0, 4);
            auto tag_name_buffer = TRY(stream.read_some(four_byte_buffer));
            if (tag_name_buffer.size() != 4)
                return Error::from_string_literal("Not enough data to read tag name entry of table directory entry");

            table_directory_entry.tag = tag_name_buffer;
        }

        VERIFY(table_directory_entry.tag.length() == 4);
        table_directory_entry.original_length = TRY(read_uint_base_128(stream, read_buffer));

        bool needs_to_read_transform_length = false;
        if (table_directory_entry.tag.is_one_of("glyf"sv, "loca"sv))
            needs_to_read_transform_length = table_directory_entry.transformation_version == TransformationVersion::Version0;
        else
            needs_to_read_transform_length = table_directory_entry.transformation_version != TransformationVersion::Version0;

        if (needs_to_read_transform_length) {
            dbgln("table {} has transform", table_directory_entry.tag);
            u32 transform_length = TRY(read_uint_base_128(stream, read_buffer));
            table_directory_entry.transform_length = transform_length;
            total_length_of_all_tables += transform_length;
        } else {
            total_length_of_all_tables += table_directory_entry.original_length;
        }

        table_entries.unchecked_append(move(table_directory_entry));
    }

    // FIXME: Read in collection header and entries.

    auto glyf_table = table_entries.find_if([](TableDirectoryEntry const& entry) {
        return entry.tag == "glyf"sv;
    });

    auto loca_table = table_entries.find_if([](TableDirectoryEntry const& entry) {
        return entry.tag == "loca"sv;
    });

    // "In other words, both glyf and loca tables must either be present in their transformed format or with null transform applied to both tables."
    if (glyf_table.is_end() != loca_table.is_end())
        return Error::from_string_literal("Must have both 'loca' and 'glyf' tables if one of them is present");

    if (!glyf_table.is_end() && !loca_table.is_end()) {
        if (glyf_table->transformation_version != loca_table->transformation_version)
            return Error::from_string_literal("The 'loca' and 'glyf' tables must have the same transformation version");
    }

    if (!loca_table.is_end()) {
        if (loca_table->has_transformation() && loca_table->transform_length.value() != 0)
            return Error::from_string_literal("Transformed 'loca' table must have a transform length of 0");
    }

    auto compressed_bytes_read_buffer = TRY(ByteBuffer::create_zeroed(total_compressed_size));
    auto compressed_bytes = TRY(stream.read_some(compressed_bytes_read_buffer));
    if (compressed_bytes.size() != total_compressed_size)
        return Error::from_string_literal("Not enough data to read in the reported size of the compressed data");

    auto compressed_stream = FixedMemoryStream(compressed_bytes);
    auto brotli_stream = Compress::BrotliDecompressionStream { compressed_stream };
    auto decompressed_table_data = TRY(brotli_stream.read_until_eof());
    if (decompressed_table_data.size() != total_length_of_all_tables)
        return Error::from_string_literal("Size of the decompressed data is not equal to the total of the reported lengths of each table");

    auto decompressed_data_stream = FixedMemoryStream(decompressed_table_data.bytes());
    size_t font_buffer_offset = SFNT_HEADER_SIZE + num_tables * SFNT_TABLE_SIZE;
    Optional<GlyfAndLocaTableBuffers> glyf_and_loca_buffer;
    for (size_t table_entry_index = 0; table_entry_index < num_tables; ++table_entry_index) {
        auto& table_entry = table_entries.at(table_entry_index);
        dbgln("GO table {}"sv, table_entry.tag);
        u32 length_to_read = table_entry.has_transformation() ? table_entry.transform_length.value() : table_entry.original_length;

        auto table_buffer = TRY(ByteBuffer::create_zeroed(length_to_read));
        auto table_bytes = TRY(decompressed_data_stream.read_some(table_buffer));
        if (table_bytes.size() != length_to_read)
            return Error::from_string_literal("Not enough data to read decompressed table");

        size_t table_directory_offset = SFNT_HEADER_SIZE + table_entry_index * SFNT_TABLE_SIZE;

        if (table_entry.has_transformation()) {
            if (table_entry.tag == "glyf"sv) {
                auto table_stream = FixedMemoryStream(table_bytes);
                glyf_and_loca_buffer = TRY(create_glyf_and_loca_tables_from_transformed_glyf_table(table_stream, read_buffer));

                constexpr u32 GLYF_TAG = 0x676C7966;

                if (font_buffer.size() < (font_buffer_offset + glyf_and_loca_buffer->glyf_table.size()))
                    TRY(font_buffer.try_resize(font_buffer_offset + glyf_and_loca_buffer->glyf_table.size()));

                // ISO-IEC 14496-22:2019 4.5.2 Table directory
                be_u32(font_buffer.data() + table_directory_offset, GLYF_TAG);
                // FIXME: WOFF2 does not give us the original checksum.
                be_u32(font_buffer.data() + table_directory_offset + 4, 0);
                be_u32(font_buffer.data() + table_directory_offset + 8, font_buffer_offset);
                be_u32(font_buffer.data() + table_directory_offset + 12, glyf_and_loca_buffer->glyf_table.size());

                font_buffer.overwrite(font_buffer_offset, glyf_and_loca_buffer->glyf_table.data(), glyf_and_loca_buffer->glyf_table.size());
                font_buffer_offset += glyf_and_loca_buffer->glyf_table.size();
            } else if (table_entry.tag == "loca"sv) {
                // FIXME: Handle loca table coming before glyf table in input?
                VERIFY(glyf_and_loca_buffer.has_value());
                if (font_buffer.size() < (font_buffer_offset + glyf_and_loca_buffer->loca_table.size()))
                    TRY(font_buffer.try_resize(font_buffer_offset + glyf_and_loca_buffer->loca_table.size()));
                constexpr u32 LOCA_TAG = 0x6C6F6361;
                be_u32(font_buffer.data() + table_directory_offset, LOCA_TAG);
                // FIXME: WOFF2 does not give us the original checksum.
                be_u32(font_buffer.data() + table_directory_offset + 4, 0);
                be_u32(font_buffer.data() + table_directory_offset + 8, font_buffer_offset);
                be_u32(font_buffer.data() + table_directory_offset + 12, glyf_and_loca_buffer->loca_table.size());

                font_buffer.overwrite(font_buffer_offset, glyf_and_loca_buffer->loca_table.data(), glyf_and_loca_buffer->loca_table.size());
                font_buffer_offset += glyf_and_loca_buffer->loca_table.size();
            } else if (table_entry.tag == "hmtx"sv) {
                return Error::from_string_literal("Decoding transformed hmtx table not yet supported");
            } else {
                return Error::from_string_literal("Unknown transformation");
            }
        } else {
            // ISO-IEC 14496-22:2019 4.5.2 Table directory
            be_u32(font_buffer.data() + table_directory_offset, table_entry.tag_to_u32());
            // FIXME: WOFF2 does not give us the original checksum.
            be_u32(font_buffer.data() + table_directory_offset + 4, 0);
            be_u32(font_buffer.data() + table_directory_offset + 8, font_buffer_offset);
            be_u32(font_buffer.data() + table_directory_offset + 12, length_to_read);

            if (font_buffer.size() < (font_buffer_offset + length_to_read))
                TRY(font_buffer.try_resize(font_buffer_offset + length_to_read));
            font_buffer.overwrite(font_buffer_offset, table_buffer.data(), length_to_read);

            font_buffer_offset += length_to_read;
        }
    }

    //    for (size_t i = 0; i < font_buffer.size(); ++i) {
    //        dbgln("font_buffer[{}] = 0x{:02x} ('{}')", i, font_buffer[i], (char)font_buffer[i]);
    //    }

    //    auto remaining = TRY(stream.read_until_eof());
    //    for (size_t i = 0; i < remaining.size(); ++i) {
    //        dbgln("{}: 0x{:02x}", i, remaining[i]);
    //    }

    auto outfile = MUST(Core::File::open("/tmp/font.ttf"sv, Core::File::OpenMode::Write | Core::File::OpenMode::Truncate));
    MUST(outfile->write_until_depleted(font_buffer.bytes()));

    auto input_font = TRY(OpenType::Font::try_load_from_externally_owned_memory(font_buffer.bytes()));
    return adopt_ref(*new Font(input_font, move(font_buffer)));
}

}

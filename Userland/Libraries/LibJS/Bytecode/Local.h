/*
 * Copyright (c) 2023, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Format.h>

namespace JS::Bytecode {

class Local {
public:
    constexpr explicit Local(u32 index)
        : m_index(index)
    {
    }

    constexpr bool operator==(Local other) const { return m_index == other.index(); }

    constexpr u32 index() const { return m_index; }

private:
    u32 m_index;
};

}

template<>
struct AK::Formatter<JS::Bytecode::Local> : AK::Formatter<FormatString> {
    ErrorOr<void> format(FormatBuilder& builder, JS::Bytecode::Local const& value)
    {
        return AK::Formatter<FormatString>::format(builder, "%{}"sv, value.index());
    }
};

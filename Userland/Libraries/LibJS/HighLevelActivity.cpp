/*
 * Copyright (c) 2023, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Format.h>
#include <LibJS/HighLevelActivity.h>

namespace JS {

static StringView s_current_high_level_activity;

HighLevelActivityScope::HighLevelActivityScope(StringView description)
{
    m_previous = set_high_level_activity(description);
}

HighLevelActivityScope::~HighLevelActivityScope()
{
    (void) set_high_level_activity(m_previous);
}

NEVER_INLINE StringView get_high_level_activity()
{
    return s_current_high_level_activity;
}

NEVER_INLINE StringView set_high_level_activity(StringView description)
{
    auto previous = s_current_high_level_activity;
    s_current_high_level_activity = description;
    return previous;
}

}

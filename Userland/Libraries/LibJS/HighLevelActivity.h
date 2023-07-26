/*
 * Copyright (c) 2023, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Noncopyable.h>
#include <AK/StringView.h>

namespace JS {

[[nodiscard]] StringView get_high_level_activity();
[[nodiscard]] StringView set_high_level_activity(StringView);

class HighLevelActivityScope {
    AK_MAKE_NONCOPYABLE(HighLevelActivityScope);
    AK_MAKE_NONMOVABLE(HighLevelActivityScope);

public:
    HighLevelActivityScope(StringView description);
    ~HighLevelActivityScope();

private:
    StringView m_previous;
};

}

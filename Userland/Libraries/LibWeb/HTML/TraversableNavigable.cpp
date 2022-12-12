/*
 * Copyright (c) 2022, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/HTML/SessionHistoryEntry.h>
#include <LibWeb/HTML/TraversableNavigable.h>

namespace Web::HTML {

TraversableNavigable::TraversableNavigable() = default;

TraversableNavigable::~TraversableNavigable() = default;

void TraversableNavigable::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    for (auto& entry : m_session_history_entries)
        visitor.visit(entry);
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#top-level-traversable
bool TraversableNavigable::is_top_level_traversable() const
{
    return parent() == nullptr;
}

int TraversableNavigable::current_session_history_step() const
{
    return m_current_session_history_step;
}

Vector<JS::NonnullGCPtr<SessionHistoryEntry>> const& TraversableNavigable::session_history_entries() const
{
    return m_session_history_entries;
}

bool TraversableNavigable::running_nested_apply_history_step() const
{
    return m_running_nested_apply_history_step;
}

HTML::VisibilityState TraversableNavigable::system_visibility_state() const
{
    return m_system_visibility_state;
}

}

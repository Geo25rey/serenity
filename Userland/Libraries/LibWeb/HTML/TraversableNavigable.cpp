/*
 * Copyright (c) 2022, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/DocumentState.h>
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

// https://html.spec.whatwg.org/multipage/document-sequences.html#destroy-a-top-level-traversable
void TraversableNavigable::destroy_top_level_traversable()
{
    VERIFY(is_top_level_traversable());

    // 1. Let browsingContext be traversable's active browsing context.
    auto browsing_context = active_browsing_context();

    // 2. For each historyEntry in traversable's session history entries:
    for (auto& history_entry : m_session_history_entries) {
        // 1. Let document be historyEntry's document.
        auto document = history_entry->document_state->document;

        // 2. If document is not null, then destroy document.
        if (document)
            document->destroy();
    }

    // 3. Remove browsingContext.
    browsing_context->remove();

    // FIXME: 4. Remove traversable from the user interface (e.g., close or hide its tab in a tabbed browser).

    // FIXME: 5. Remove traversable from the user agent's top-level traversable set.
}

}

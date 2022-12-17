/*
 * Copyright (c) 2022, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Vector.h>
#include <LibWeb/HTML/Navigable.h>
#include <LibWeb/HTML/VisibilityState.h>

namespace Web::HTML {

// https://html.spec.whatwg.org/multipage/document-sequences.html#traversable-navigable
class TraversableNavigable final : public Navigable {
    JS_CELL(TraversableNavigable, Navigable);

public:
    virtual ~TraversableNavigable() override;

    bool is_top_level_traversable() const;

    int current_session_history_step() const;
    Vector<JS::NonnullGCPtr<SessionHistoryEntry>> const& session_history_entries() const;
    bool running_nested_apply_history_step() const;
    HTML::VisibilityState system_visibility_state() const;

    static JS::NonnullGCPtr<TraversableNavigable> create_a_new_top_level_traversable(Page&, JS::GCPtr<BrowsingContext> opener, DeprecatedString target_name);
    static JS::NonnullGCPtr<TraversableNavigable> create_a_fresh_top_level_traversable(Page&, AK::URL const& initial_navigation_url);

    void close_top_level_traversable();
    void destroy_top_level_traversable();

private:
    TraversableNavigable();

    virtual void visit_edges(Cell::Visitor&) override;

    // https://html.spec.whatwg.org/multipage/document-sequences.html#tn-current-session-history-step
    int m_current_session_history_step { 0 };

    // https://html.spec.whatwg.org/multipage/document-sequences.html#tn-session-history-entries
    Vector<JS::NonnullGCPtr<SessionHistoryEntry>> m_session_history_entries;

    // FIXME: https://html.spec.whatwg.org/multipage/document-sequences.html#tn-session-history-traversal-queue

    // https://html.spec.whatwg.org/multipage/document-sequences.html#tn-running-nested-apply-history-step
    bool m_running_nested_apply_history_step { false };

    // https://html.spec.whatwg.org/multipage/document-sequences.html#system-visibility-state
    HTML::VisibilityState m_system_visibility_state { HTML::VisibilityState::Visible };
};

}

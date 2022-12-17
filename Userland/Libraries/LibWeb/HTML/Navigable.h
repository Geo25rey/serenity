/*
 * Copyright (c) 2022, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/DeprecatedString.h>
#include <LibJS/Heap/Cell.h>
#include <LibWeb/Forward.h>

namespace Web::HTML {

// https://html.spec.whatwg.org/multipage/document-sequences.html#navigable
class Navigable : public JS::Cell {
    JS_CELL(Navigable, JS::Cell);

public:
    virtual ~Navigable() override;

    void initialize_navigable(JS::NonnullGCPtr<DocumentState>, JS::GCPtr<Navigable> parent_navigable = nullptr);

    DeprecatedString id() const;
    JS::GCPtr<Navigable> parent() const;

    bool is_closing() const;
    void set_closing(bool);

    bool is_delaying_load_events() const;
    void set_delaying_load_events(bool);

    JS::GCPtr<SessionHistoryEntry> active_session_history_entry() const;
    JS::GCPtr<SessionHistoryEntry> current_session_history_entry() const;

    JS::GCPtr<DOM::Document> active_document();
    JS::GCPtr<BrowsingContext> active_browsing_context();
    JS::GCPtr<HTML::WindowProxy> active_window_proxy();
    JS::GCPtr<HTML::Window> active_window();

    DeprecatedString target_name() const;

    JS::GCPtr<NavigableContainer> container() const;
    void set_container(JS::GCPtr<NavigableContainer>);

    JS::GCPtr<DOM::Document> container_document() const;

    JS::GCPtr<TraversableNavigable> traversable_navigable();
    JS::GCPtr<TraversableNavigable> top_level_traversable();

protected:
    Navigable();

    virtual void visit_edges(Cell::Visitor&) override;

private:
    // https://html.spec.whatwg.org/multipage/document-sequences.html#nav-id
    DeprecatedString m_id;

    // https://html.spec.whatwg.org/multipage/document-sequences.html#nav-parent
    JS::GCPtr<Navigable> m_parent;

    // https://html.spec.whatwg.org/multipage/document-sequences.html#nav-current-history-entry
    JS::GCPtr<SessionHistoryEntry> m_current_session_history_entry;

    // https://html.spec.whatwg.org/multipage/document-sequences.html#nav-active-history-entry
    JS::GCPtr<SessionHistoryEntry> m_active_session_history_entry;

    // https://html.spec.whatwg.org/multipage/document-sequences.html#is-closing
    bool m_closing { false };

    // https://html.spec.whatwg.org/multipage/document-sequences.html#delaying-load-events-mode
    bool m_delaying_load_events { false };

    // Implied link between navigable and its container.
    JS::GCPtr<NavigableContainer> m_container;
};

}

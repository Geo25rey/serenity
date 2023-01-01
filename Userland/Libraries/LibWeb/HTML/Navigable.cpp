/*
 * Copyright (c) 2022, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/BrowsingContext.h>
#include <LibWeb/HTML/DocumentState.h>
#include <LibWeb/HTML/Navigable.h>
#include <LibWeb/HTML/SessionHistoryEntry.h>
#include <LibWeb/HTML/TraversableNavigable.h>

namespace Web::HTML {

Navigable::Navigable() = default;

Navigable::~Navigable() = default;

void Navigable::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_parent);
    visitor.visit(m_current_session_history_entry);
    visitor.visit(m_active_session_history_entry);
    visitor.visit(m_container);
}

DeprecatedString Navigable::id() const
{
    return m_id;
}

JS::GCPtr<Navigable> Navigable::parent() const
{
    return m_parent;
}

bool Navigable::is_closing() const
{
    return m_closing;
}

void Navigable::set_closing(bool value)
{
    m_closing = value;
}

bool Navigable::is_delaying_load_events() const
{
    return m_delaying_load_events;
}

void Navigable::set_delaying_load_events(bool value)
{
    m_delaying_load_events = value;
}

JS::GCPtr<SessionHistoryEntry> Navigable::active_session_history_entry() const
{
    return m_active_session_history_entry;
}

JS::GCPtr<SessionHistoryEntry> Navigable::current_session_history_entry() const
{
    return m_current_session_history_entry;
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#nav-document
JS::GCPtr<DOM::Document> Navigable::active_document()
{
    // A navigable's active document is its active session history entry's document.
    return m_active_session_history_entry->document_state->document;
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#nav-bc
JS::GCPtr<BrowsingContext> Navigable::active_browsing_context()
{
    // A navigable's active browsing context is its active document's browsing context.
    // If this navigable is a traversable navigable, then its active browsing context will be a top-level browsing context.
    if (auto document = active_document())
        return document->browsing_context();
    return nullptr;
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#nav-wp
JS::GCPtr<HTML::WindowProxy> Navigable::active_window_proxy()
{
    // A navigable's active WindowProxy is its active browsing context's associated WindowProxy.
    if (auto browsing_context = active_browsing_context())
        return browsing_context->window_proxy();
    return nullptr;
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#nav-window
JS::GCPtr<HTML::Window> Navigable::active_window()
{
    // A navigable's active window is its active WindowProxy's [[Window]].
    if (auto window_proxy = active_window_proxy())
        return window_proxy->window();
    return nullptr;
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#nav-target
DeprecatedString Navigable::target_name() const
{
    // FIXME: A navigable's target name is its active session history entry's document state's navigable target name.
    dbgln("FIXME: Implement Navigable::target_name()");
    return "";
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#nav-container
JS::GCPtr<NavigableContainer> Navigable::container() const
{
    // The container of a navigable navigable is the navigable container whose nested navigable is navigable, or null if there is no such element.
    return m_container;
}

void Navigable::set_container(JS::GCPtr<NavigableContainer> container)
{
    m_container = container;
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#nav-traversable
JS::GCPtr<TraversableNavigable> Navigable::traversable_navigable()
{
    // 1. Let navigable be inputNavigable.
    auto navigable = this;

    // 2. While navigable is not a traversable navigable, set navigable to navigable's parent.
    while (navigable && !is<TraversableNavigable>(*navigable))
        navigable = navigable->parent();

    // 3. Return navigable.
    return static_cast<TraversableNavigable*>(navigable);
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#nav-top
JS::GCPtr<TraversableNavigable> Navigable::top_level_traversable()
{
    // 1. Let navigable be inputNavigable.
    auto navigable = this;

    // 2. While navigable's parent is not null, set navigable to navigable's parent.
    while (navigable->parent())
        navigable = navigable->parent();

    // 3. Return navigable.
    return verify_cast<TraversableNavigable>(navigable);
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#initialize-the-navigable
void Navigable::initialize_navigable(JS::NonnullGCPtr<DocumentState> document_state, JS::GCPtr<Navigable> parent)
{
    // 1. Let entry be a new session history entry, with
    JS::NonnullGCPtr<SessionHistoryEntry> entry = *heap().allocate_without_realm<SessionHistoryEntry>();

    // URL: document's URL
    entry->url = document_state->document->url();

    // document state: documentState
    entry->document_state = document_state;

    // 2. Set navigable's current session history entry to entry.
    m_current_session_history_entry = entry;

    // 3. Set navigable's active session history entry to entry.
    m_active_session_history_entry = entry;

    // 4. Set navigable's parent to parent.
    m_parent = parent;
}

}

/*
 * Copyright (c) 2022, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/MainThreadVM.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/BrowsingContextGroup.h>
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

static OrderedHashTable<TraversableNavigable*>& user_agent_top_level_traversable_set()
{
    static OrderedHashTable<TraversableNavigable*> set;
    return set;
}

struct BrowsingContextAndDocument {
    JS::NonnullGCPtr<HTML::BrowsingContext> browsing_context;
    JS::NonnullGCPtr<DOM::Document> document;
};

// https://html.spec.whatwg.org/multipage/document-sequences.html#creating-a-new-top-level-browsing-context
static BrowsingContextAndDocument create_a_new_top_level_browsing_context_and_document(Page& page)
{
    // 1. Let group and document be the result of creating a new browsing context group and document.
    auto [group, document] = BrowsingContextGroup::create_a_new_browsing_context_group_and_document(page);

    // 2. Return group's browsing context set[0] and document.
    return { **group->browsing_context_set().begin(), document };
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#creating-a-new-top-level-traversable
JS::NonnullGCPtr<TraversableNavigable> TraversableNavigable::create_a_new_top_level_traversable(Page& page, JS::GCPtr<HTML::BrowsingContext> opener, DeprecatedString target_name)
{
    auto& vm = Bindings::main_thread_vm();

    // 1. Let document be null.
    JS::GCPtr<DOM::Document> document = nullptr;

    // 2. If opener is null, then set document to the second return value of creating a new top-level browsing context and document.
    if (!opener) {
        document = create_a_new_top_level_browsing_context_and_document(page).document;
    }

    // 3. Otherwise, set document to the second return value of creating a new auxiliary browsing context and document given opener.
    else {
        document = BrowsingContext::create_a_new_auxiliary_browsing_context_and_document(page, *opener).document;
    }

    // 4. Let documentState be a new document state, with
    auto document_state = vm.heap().allocate_without_realm<DocumentState>();

    // document: document
    document_state->document = document;

    // navigable target name: targetName
    document_state->navigable_target_name = target_name;

    // 5. Let traversable be a new traversable navigable.
    auto traversable = vm.heap().allocate_without_realm<TraversableNavigable>();

    // 6. Initialize the navigable traversable given documentState.
    traversable->initialize_navigable(document_state);

    // 7. Let initialHistoryEntry be traversable's active session history entry.
    auto initial_history_entry = traversable->active_session_history_entry();
    VERIFY(initial_history_entry);

    // FIXME: 8. Set initialHistoryEntry's step to 0.

    // 9. Append initialHistoryEntry to traversable's session history entries.
    traversable->m_session_history_entries.append(*initial_history_entry);

    // FIXME: 10. If opener is non-null, then legacy-clone a traversable storage shed given opener's top-level traversable and traversable. [STORAGE]

    // 11. Append traversable to the user agent's top-level traversable set.
    user_agent_top_level_traversable_set().set(traversable);

    // 12. Return traversable.
    return traversable;
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#create-a-fresh-top-level-traversable
JS::NonnullGCPtr<TraversableNavigable> TraversableNavigable::create_a_fresh_top_level_traversable(Page& page, AK::URL const& initial_navigation_url)
{
    // 1. Let traversable be the result of creating a new top-level traversable given null and the empty string.
    auto traversable = create_a_new_top_level_traversable(page, nullptr, "");

    // FIXME: 2. Navigate traversable to initialNavigationURL using traversable's active document, with documentResource set to initialNavigationPostResource.
    (void)initial_navigation_url;

    // 3. Return traversable.
    return traversable;
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

// https://html.spec.whatwg.org/multipage/document-sequences.html#close-a-top-level-traversable
void TraversableNavigable::close_top_level_traversable()
{
    VERIFY(is_top_level_traversable());

    // 1. Let toUnload be traversable's active document's inclusive descendant navigables.
    auto to_unload = active_document()->inclusive_descendant_navigables();

    // FIXME: 2. If the result of checking if unloading is user-canceled for toUnload is true, then return.

    // 3. Unload the active documents of each of toUnload.
    for (auto navigable : to_unload) {
        navigable->active_document()->unload();
    }

    // 4. Destroy traversable.
    destroy_top_level_traversable();
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

    // 5. Remove traversable from the user agent's top-level traversable set.
    user_agent_top_level_traversable_set().remove(this);
}

}

/*
 * Copyright (c) 2021-2022, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/RefPtr.h>
#include <LibJS/Heap/Handle.h>
#include <LibJS/SafeFunction.h>
#include <LibWeb/Forward.h>

namespace Web::HTML {

class Task {
public:
    // https://html.spec.whatwg.org/multipage/webappapis.html#generic-task-sources
    enum class Source {
        Unspecified,
        DOMManipulation,
        UserInteraction,
        Networking,
        HistoryTraversal,
        IdleTask,
        PostedMessage,
        Microtask,
        TimerTask,
        JavaScriptEngine,

        // https://html.spec.whatwg.org/multipage/webappapis.html#navigation-and-traversal-task-source
        NavigationAndTraversal,
    };

    static NonnullOwnPtr<Task> create(Source source, DOM::Document* document, JS::SafeFunction<void()> steps)
    {
        return adopt_own(*new Task(source, document, move(steps)));
    }
    ~Task();

    Source source() const { return m_source; }
    void execute();

    DOM::Document* document();
    DOM::Document const* document() const;

    bool is_runnable() const;

private:
    Task(Source, DOM::Document*, JS::SafeFunction<void()> steps);

    Source m_source { Source::Unspecified };
    JS::SafeFunction<void()> m_steps;
    JS::Handle<DOM::Document> m_document;
};

}

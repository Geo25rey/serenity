/*
 * Copyright (c) 2021, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2022, Linus Groh <linusg@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Heap/Heap.h>
#include <LibJS/Heap/MarkedVector.h>

namespace JS {

MarkedVectorBase::MarkedVectorBase(Heap& heap)
    : m_heap(heap)
{
    m_heap.did_create_marked_vector({}, *this);
}

MarkedVectorBase::~MarkedVectorBase()
{
    m_heap.did_destroy_marked_vector({}, *this);
}

}

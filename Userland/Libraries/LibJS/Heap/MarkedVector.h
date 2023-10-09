/*
 * Copyright (c) 2021, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2022, Linus Groh <linusg@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/IntrusiveList.h>
#include <AK/Vector.h>
#include <LibJS/Forward.h>
#include <LibJS/Heap/Cell.h>
#include <LibJS/Heap/HeapRoot.h>

namespace JS {

class MarkedVectorBase {
    AK_MAKE_NONCOPYABLE(MarkedVectorBase);
    AK_MAKE_NONMOVABLE(MarkedVectorBase);

public:
    virtual void gather_roots(HashMap<Cell*, JS::HeapRoot>&) const = 0;

    explicit MarkedVectorBase(Heap&);
    ~MarkedVectorBase();

    Heap& m_heap;
    IntrusiveListNode<MarkedVectorBase> m_list_node;

    using List = IntrusiveList<&MarkedVectorBase::m_list_node>;
};

template<typename T>
class MarkedVectorImpl final
    : public MarkedVectorBase
    , public Vector<T> {

public:
    explicit MarkedVectorImpl(Heap& heap)
        : MarkedVectorBase(heap)
    {
    }

    virtual ~MarkedVectorImpl() = default;

    virtual void gather_roots(HashMap<Cell*, JS::HeapRoot>& roots) const override
    {
        for (auto& value : *this) {
            if constexpr (IsSame<Value, T>) {
                if (value.is_cell())
                    roots.set(&const_cast<T&>(value).as_cell(), HeapRoot { .type = HeapRoot::Type::MarkedVector });
            } else {
                roots.set(value, HeapRoot { .type = HeapRoot::Type::MarkedVector });
            }
        }
    }
};

template<typename T>
class MarkedVector final {
    AK_MAKE_NONCOPYABLE(MarkedVector);

public:
    ~MarkedVector() = default;

    MarkedVector(MarkedVector&&) = default;
    MarkedVector& operator=(MarkedVector&&) = default;

    explicit MarkedVector(Heap& heap)
        : m_impl(adopt_own(*new MarkedVectorImpl<T>(heap)))
    {
    }

    MarkedVector clone() const
    {
        MarkedVector clone { m_impl->m_heap };
        clone.extend(*this);
        return clone;
    }

    [[nodiscard]] T const* data() const { return m_impl->data(); }
    [[nodiscard]] T* data() { return m_impl->data(); }

    void clear() { m_impl->clear(); }

    [[nodiscard]] size_t size() const { return m_impl->size(); }
    [[nodiscard]] bool is_empty() const { return size() == 0; }

    void ensure_capacity(size_t capacity) { m_impl->ensure_capacity(capacity); }

    void append(T const& value) { m_impl->append(value); }
    void append(T&& value) { m_impl->append(move(value)); }

    template<class... Args>
    void empend(Args&&... args)
    {
        m_impl->empend(forward<Args>(args)...);
    }

    void unchecked_append(T const& value) { m_impl->unchecked_append(value); }
    void unchecked_append(T&& value) { m_impl->unchecked_append(move(value)); }

    void prepend(T const& value) { m_impl->prepend(value); }
    void prepend(T&& value) { m_impl->prepend(move(value)); }

    void insert(size_t index, T const& value) { m_impl->insert(index, value); }
    void insert(size_t index, T&& value) { m_impl->insert(index, move(value)); }

    [[nodiscard]] T const& first() const { return m_impl->first(); }
    [[nodiscard]] T& first() { return m_impl->first(); }

    operator Span<T>() { return m_impl->span(); }
    operator ReadonlySpan<T>() const { return m_impl->span(); }

    [[nodiscard]] auto begin() { return m_impl->begin(); }
    [[nodiscard]] auto begin() const { return m_impl->begin(); }
    [[nodiscard]] auto end() { return m_impl->end(); }
    [[nodiscard]] auto end() const { return m_impl->end(); }

    [[nodiscard]] T& operator[](size_t index) { return m_impl->operator[](index); }
    [[nodiscard]] T const& operator[](size_t index) const { return m_impl->operator[](index); }

    [[nodiscard]] bool contains_slow(T const& value) const { return m_impl->contains_slow(value); }

    void extend(MarkedVector<T> const& other) { m_impl->extend(*other.m_impl); }
    void extend(MarkedVector<T>&& other) { m_impl->extend(move(*other.m_impl)); }

    void extend(Vector<T> const& other) { m_impl->extend(other); }
    void extend(Vector<T>&& other) { m_impl->extend(move(other)); }

    void resize(size_t size) { m_impl->resize(size); }

    template<typename TUnaryPredicate>
    bool remove_first_matching(TUnaryPredicate const& predicate)
    {
        return m_impl->remove_first_matching(predicate);
    }

private:
    NonnullOwnPtr<MarkedVectorImpl<T>> m_impl;
};

}

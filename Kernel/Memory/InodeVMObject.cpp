/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Kernel/FileSystem/Inode.h>
#include <Kernel/Memory/InodeVMObject.h>
#include <Kernel/InterruptDisabler.h>
#include <Kernel/Memory/MemoryManager.h>

namespace Kernel::Memory {

InodeVMObject::InodeVMObject(Inode& inode, FixedArray<RefPtr<PhysicalPage>>&& new_physical_pages, Bitmap dirty_pages)
    : VMObject(move(new_physical_pages))
    , m_inode(inode)
    , m_dirty_pages(move(dirty_pages))
{
}

InodeVMObject::InodeVMObject(InodeVMObject const& other, FixedArray<RefPtr<PhysicalPage>>&& new_physical_pages, Bitmap dirty_pages)
    : VMObject(move(new_physical_pages))
    , m_inode(other.m_inode)
    , m_dirty_pages(move(dirty_pages))
{
    for (size_t i = 0; i < page_count(); ++i)
        m_dirty_pages.set(i, other.m_dirty_pages.get(i));
}

InodeVMObject::~InodeVMObject() = default;

size_t InodeVMObject::amount_clean() const
{
    size_t count = 0;
    VERIFY(page_count() == m_dirty_pages.size());
    for (size_t i = 0; i < page_count(); ++i) {
        if (!m_dirty_pages.get(i) && m_physical_pages[i])
            ++count;
    }
    return count * PAGE_SIZE;
}

size_t InodeVMObject::amount_dirty() const
{
    size_t count = 0;
    for (size_t i = 0; i < m_dirty_pages.size(); ++i) {
        if (m_dirty_pages.get(i))
            ++count;
    }
    return count * PAGE_SIZE;
}

int InodeVMObject::release_all_clean_pages()
{
    SpinlockLocker locker(m_lock);

    int count = 0;
    for (size_t i = 0; i < page_count(); ++i) {
        if (!m_dirty_pages.get(i) && m_physical_pages[i]) {
            m_physical_pages[i] = nullptr;
            ++count;
        }
    }
    if (count) {
        for_each_region([](auto& region) {
            region.remap();
        });
    }
    return count;
}

int InodeVMObject::try_release_clean_pages(int page_amount)
{
    SpinlockLocker locker(m_lock);

    int count = 0;
    for (size_t i = 0; i < page_count() && count < page_amount; ++i) {
        if (!m_dirty_pages.get(i) && m_physical_pages[i]) {
            m_physical_pages[i] = nullptr;
            ++count;
        }
    }
    if (count) {
        for_each_region([](auto& region) {
            region.remap();
        });
    }
    return count;
}

u32 InodeVMObject::writable_mappings() const
{
    u32 count = 0;
    const_cast<InodeVMObject&>(*this).for_each_region([&](auto& region) {
        if (region.is_writable())
            ++count;
    });
    return count;
}

Result<void, PageFaultResponse> InodeVMObject::handle_page_fault(size_t page_index)
{
    VERIFY_INTERRUPTS_ENABLED();

    if (auto* current_thread = Thread::current()) {
        current_thread->did_inode_fault();
    }

    u8 page_buffer[PAGE_SIZE];
    auto buffer = UserOrKernelBuffer::for_kernel_buffer(page_buffer);
    auto result = inode().read_bytes(page_index * PAGE_SIZE, PAGE_SIZE, buffer, nullptr);

    if (result.is_error()) {
        dmesgln("InodeVMObject::handle_page_fault: Read error: {}", result.error());
        return PageFaultResponse::ShouldCrash;
    }

    auto nread = result.value();
    // Note: If we received 0, it means we are at the end of file or after it,
    // which means we should return bus error.
    if (nread == 0) {
        return PageFaultResponse::BusError;
    }

    if (nread < PAGE_SIZE) {
        // If we read less than a page, zero out the rest to avoid leaking uninitialized data.
        memset(page_buffer + nread, 0, PAGE_SIZE - nread);
    }

    // Allocate a new physical page, and copy the read inode contents into it.
    auto new_physical_page_or_error = MM.allocate_physical_page(MemoryManager::ShouldZeroFill::No);
    if (new_physical_page_or_error.is_error()) {
        dmesgln("MM: handle_inode_fault was unable to allocate a physical page");
        return PageFaultResponse::OutOfMemory;
    }
    auto new_physical_page = new_physical_page_or_error.release_value();
    {
        InterruptDisabler disabler;
        u8* dest_ptr = MM.quickmap_page(*new_physical_page);
        memcpy(dest_ptr, page_buffer, PAGE_SIZE);
        MM.unquickmap_page();
    }

    {
        // NOTE: The VMObject lock is required when manipulating the VMObject's physical page slot.
        SpinlockLocker locker(m_lock);

        if (!physical_pages()[page_index].is_null()) {
            // Someone else faulted in this page while we were reading from the inode.
            // No harm done (other than some duplicate work), remap the page here and return.
            dbgln_if(PAGE_FAULT_DEBUG, "InodeVMObject::handle_page_fault: Page faulted in by someone else, remapping.");
            return {};
        }
        physical_pages()[page_index] = move(new_physical_page);
    }

    return {};
}

}

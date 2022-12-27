/*
 * Copyright (c) 2018-2022, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Memory.h>
#include <AK/StringView.h>
#include <Kernel/Arch/PageDirectory.h>
#include <Kernel/Arch/PageFault.h>
#include <Kernel/Debug.h>
#include <Kernel/FileSystem/Inode.h>
#include <Kernel/InterruptDisabler.h>
#include <Kernel/Memory/AnonymousVMObject.h>
#include <Kernel/Memory/MemoryManager.h>
#include <Kernel/Memory/PageDirectory.h>
#include <Kernel/Memory/Region.h>
#include <Kernel/Memory/SharedInodeVMObject.h>
#include <Kernel/Panic.h>
#include <Kernel/Process.h>
#include <Kernel/Scheduler.h>
#include <Kernel/Thread.h>

namespace Kernel::Memory {

Region::Region()
    : m_range(VirtualRange({}, 0))
{
}

Region::Region(NonnullLockRefPtr<VMObject> vmobject, size_t offset_in_vmobject, OwnPtr<KString> name, Region::Access access, Cacheable cacheable, bool shared)
    : m_range(VirtualRange({}, 0))
    , m_offset_in_vmobject(offset_in_vmobject)
    , m_vmobject(move(vmobject))
    , m_name(move(name))
    , m_access(access | ((access & 0x7) << 4))
    , m_shared(shared)
    , m_cacheable(cacheable == Cacheable::Yes)
{
    m_vmobject->add_region(*this);
}

Region::Region(VirtualRange const& range, NonnullLockRefPtr<VMObject> vmobject, size_t offset_in_vmobject, OwnPtr<KString> name, Region::Access access, Cacheable cacheable, bool shared)
    : m_range(range)
    , m_offset_in_vmobject(offset_in_vmobject)
    , m_vmobject(move(vmobject))
    , m_name(move(name))
    , m_access(access | ((access & 0x7) << 4))
    , m_shared(shared)
    , m_cacheable(cacheable == Cacheable::Yes)
{
    VERIFY(m_range.base().is_page_aligned());
    VERIFY(m_range.size());
    VERIFY((m_range.size() % PAGE_SIZE) == 0);

    m_vmobject->add_region(*this);
}

Region::~Region()
{
    if (is_writable() && vmobject().is_shared_inode()) {
        // FIXME: This is very aggressive. Find a way to do less work!
        (void)static_cast<SharedInodeVMObject&>(vmobject()).sync();
    }

    m_vmobject->remove_region(*this);

    if (m_page_directory) {
        SpinlockLocker pd_locker(m_page_directory->get_lock());
        if (!is_readable() && !is_writable() && !is_executable()) {
            // If the region is "PROT_NONE", we didn't map it in the first place.
        } else {
            unmap_with_locks_held(ShouldFlushTLB::Yes, pd_locker);
            VERIFY(!m_page_directory);
        }
    }

    if (is_kernel())
        MM.unregister_kernel_region(*this);
}

ErrorOr<NonnullOwnPtr<Region>> Region::create_unbacked()
{
    return adopt_nonnull_own_or_enomem(new (nothrow) Region);
}

ErrorOr<NonnullOwnPtr<Region>> Region::create_unplaced(NonnullLockRefPtr<VMObject> vmobject, size_t offset_in_vmobject, OwnPtr<KString> name, Region::Access access, Cacheable cacheable, bool shared)
{
    return adopt_nonnull_own_or_enomem(new (nothrow) Region(move(vmobject), offset_in_vmobject, move(name), access, cacheable, shared));
}

ErrorOr<NonnullOwnPtr<Region>> Region::try_clone()
{
    VERIFY(Process::has_current());

    if (m_shared) {
        VERIFY(!m_stack);
        if (vmobject().is_inode())
            VERIFY(vmobject().is_shared_inode());

        // Create a new region backed by the same VMObject.

        OwnPtr<KString> region_name;
        if (m_name)
            region_name = TRY(m_name->try_clone());

        auto region = TRY(Region::try_create_user_accessible(
            m_range, vmobject(), m_offset_in_vmobject, move(region_name), access(), m_cacheable ? Cacheable::Yes : Cacheable::No, m_shared));
        region->set_mmap(m_mmap, m_mmapped_from_readable, m_mmapped_from_writable);
        region->set_shared(m_shared);
        region->set_syscall_region(is_syscall_region());
        return region;
    }

    if (vmobject().is_inode())
        VERIFY(vmobject().is_private_inode());

    auto vmobject_clone = TRY(vmobject().try_clone());

    // Set up a COW region. The parent (this) region becomes COW as well!
    if (is_writable())
        remap();

    OwnPtr<KString> clone_region_name;
    if (m_name)
        clone_region_name = TRY(m_name->try_clone());

    auto clone_region = TRY(Region::try_create_user_accessible(
        m_range, move(vmobject_clone), m_offset_in_vmobject, move(clone_region_name), access(), m_cacheable ? Cacheable::Yes : Cacheable::No, m_shared));

    if (m_stack) {
        VERIFY(vmobject().is_anonymous());
        clone_region->set_stack(true);
    }
    clone_region->set_syscall_region(is_syscall_region());
    clone_region->set_mmap(m_mmap, m_mmapped_from_readable, m_mmapped_from_writable);
    return clone_region;
}

void Region::set_vmobject(NonnullLockRefPtr<VMObject>&& obj)
{
    if (m_vmobject.ptr() == obj.ptr())
        return;
    m_vmobject->remove_region(*this);
    m_vmobject = move(obj);
    m_vmobject->add_region(*this);
}

size_t Region::cow_pages() const
{
    if (!vmobject().is_anonymous())
        return 0;
    return static_cast<AnonymousVMObject const&>(vmobject()).cow_pages();
}

size_t Region::amount_dirty() const
{
    if (!vmobject().is_inode())
        return amount_resident();
    return static_cast<InodeVMObject const&>(vmobject()).amount_dirty();
}

size_t Region::amount_resident() const
{
    size_t bytes = 0;
    for (size_t i = 0; i < page_count(); ++i) {
        auto page = physical_page(i);
        if (page && !page->is_shared_zero_page() && !page->is_lazy_committed_page())
            bytes += PAGE_SIZE;
    }
    return bytes;
}

size_t Region::amount_shared() const
{
    size_t bytes = 0;
    for (size_t i = 0; i < page_count(); ++i) {
        auto page = physical_page(i);
        if (page && page->ref_count() > 1 && !page->is_shared_zero_page() && !page->is_lazy_committed_page())
            bytes += PAGE_SIZE;
    }
    return bytes;
}

ErrorOr<NonnullOwnPtr<Region>> Region::try_create_user_accessible(VirtualRange const& range, NonnullLockRefPtr<VMObject> vmobject, size_t offset_in_vmobject, OwnPtr<KString> name, Region::Access access, Cacheable cacheable, bool shared)
{
    return adopt_nonnull_own_or_enomem(new (nothrow) Region(range, move(vmobject), offset_in_vmobject, move(name), access, cacheable, shared));
}

bool Region::should_cow(size_t page_index) const
{
    if (!vmobject().is_anonymous())
        return false;
    return static_cast<AnonymousVMObject const&>(vmobject()).should_cow(first_page_index() + page_index, m_shared);
}

ErrorOr<void> Region::set_should_cow(size_t page_index, bool cow)
{
    VERIFY(!m_shared);
    if (vmobject().is_anonymous())
        TRY(static_cast<AnonymousVMObject&>(vmobject()).set_should_cow(first_page_index() + page_index, cow));
    return {};
}

bool Region::map_individual_page_impl(size_t page_index, RefPtr<PhysicalPage> page)
{
    VERIFY(m_page_directory->get_lock().is_locked_by_current_processor());

    auto page_vaddr = vaddr_from_page_index(page_index);

    bool user_allowed = page_vaddr.get() >= USER_RANGE_BASE && is_user_address(page_vaddr);
    if (is_mmap() && !user_allowed) {
        PANIC("About to map mmap'ed page at a kernel address");
    }

    auto* pte = MM.ensure_pte(*m_page_directory, page_vaddr);
    if (!pte)
        return false;

    if (!page || (!is_readable() && !is_writable())) {
        pte->clear();
        return true;
    }

    pte->set_cache_disabled(!m_cacheable);
    pte->set_physical_page_base(page->paddr().get());
    pte->set_present(true);
    if (page->is_shared_zero_page() || page->is_lazy_committed_page() || should_cow(page_index))
        pte->set_writable(false);
    else
        pte->set_writable(is_writable());
    if (Processor::current().has_nx())
        pte->set_execute_disabled(!is_executable());
    if (Processor::current().has_pat())
        pte->set_pat(is_write_combine());
    pte->set_user_allowed(user_allowed);

    return true;
}

bool Region::map_individual_page_impl(size_t page_index)
{
    RefPtr<PhysicalPage> page;
    {
        SpinlockLocker vmobject_locker(vmobject().m_lock);
        page = physical_page(page_index);
    }

    return map_individual_page_impl(page_index, page);
}

void Region::unmap(ShouldFlushTLB should_flush_tlb)
{
    if (!m_page_directory)
        return;
    SpinlockLocker pd_locker(m_page_directory->get_lock());
    unmap_with_locks_held(should_flush_tlb, pd_locker);
}

void Region::unmap_with_locks_held(ShouldFlushTLB should_flush_tlb, SpinlockLocker<RecursiveSpinlock>&)
{
    if (!m_page_directory)
        return;
    size_t count = page_count();
    for (size_t i = 0; i < count; ++i) {
        auto vaddr = vaddr_from_page_index(i);
        MM.release_pte(*m_page_directory, vaddr, i == count - 1 ? MemoryManager::IsLastPTERelease::Yes : MemoryManager::IsLastPTERelease::No);
    }
    if (should_flush_tlb == ShouldFlushTLB::Yes)
        MemoryManager::flush_tlb(m_page_directory, vaddr(), page_count());
    m_page_directory = nullptr;
}

void Region::set_page_directory(PageDirectory& page_directory)
{
    VERIFY(!m_page_directory || m_page_directory == &page_directory);
    m_page_directory = page_directory;
}

ErrorOr<void> Region::map(PageDirectory& page_directory, ShouldFlushTLB should_flush_tlb)
{
    SpinlockLocker page_lock(page_directory.get_lock());

    // FIXME: Find a better place for this sanity check(?)
    if (is_user() && !is_shared()) {
        VERIFY(!vmobject().is_shared_inode());
    }

    set_page_directory(page_directory);
    size_t page_index = 0;
    while (page_index < page_count()) {
        if (!map_individual_page_impl(page_index))
            break;
        ++page_index;
    }
    if (page_index > 0) {
        if (should_flush_tlb == ShouldFlushTLB::Yes)
            MemoryManager::flush_tlb(m_page_directory, vaddr(), page_index);
        if (page_index == page_count())
            return {};
    }
    return ENOMEM;
}

void Region::remap()
{
    VERIFY(m_page_directory);
    auto result = map(*m_page_directory);
    if (result.is_error())
        TODO();
}

ErrorOr<void> Region::set_write_combine(bool enable)
{
    if (enable && !Processor::current().has_pat()) {
        dbgln("PAT is not supported, implement MTRR fallback if available");
        return Error::from_errno(ENOTSUP);
    }

    m_write_combine = enable;
    remap();
    return {};
}

void Region::clear_to_zero()
{
    VERIFY(vmobject().is_anonymous());
    SpinlockLocker locker(vmobject().m_lock);
    for (auto i = 0u; i < page_count(); ++i) {
        auto& page = physical_page_slot(i);
        VERIFY(page);
        if (page->is_shared_zero_page())
            continue;
        page = MM.shared_zero_page();
    }
}

Result<void, PageFaultResponse> Region::validate_access(PageFault const& fault)
{
    auto page_index_in_region = page_index_from_address(fault.vaddr());

    if (fault.type() == PageFault::Type::PageNotPresent) {
        if (fault.is_read() && !is_readable()) {
            dbgln("NP(non-readable) fault at {}", fault.vaddr());
            return PageFaultResponse::ShouldCrash;
        }
        if (fault.is_write() && !is_writable()) {
            dbgln("NP(non-writable) write fault at {}", fault.vaddr());
            return PageFaultResponse::ShouldCrash;
        }

        if (vmobject().is_inode()) {
            dbgln_if(PAGE_FAULT_DEBUG, "NP(inode) fault at {}", fault.vaddr());
            return {};
        }

        // NOTE: This is the lazy-committed path.
        return {};
    }

    VERIFY(fault.type() == PageFault::Type::ProtectionViolation);

    if (fault.is_write() && is_writable() && should_cow(page_index_in_region)) {
        dbgln_if(PAGE_FAULT_DEBUG, "PV(cow) fault at {}", fault.vaddr());
        return {};
    }

    dbgln("PV(error) fault at {}", fault.vaddr());
    return PageFaultResponse::ShouldCrash;
}

RefPtr<PhysicalPage> Region::physical_page(size_t index) const
{
    SpinlockLocker vmobject_locker(vmobject().m_lock);
    VERIFY(index < page_count());
    return vmobject().physical_pages()[first_page_index() + index];
}

RefPtr<PhysicalPage>& Region::physical_page_slot(size_t index)
{
    VERIFY(vmobject().m_lock.is_locked_by_current_processor());
    VERIFY(index < page_count());
    return vmobject().physical_pages()[first_page_index() + index];
}

}

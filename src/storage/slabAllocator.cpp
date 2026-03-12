#include "slabAllocator.h"

#include <new>
#include <algorithm>


SlabAllocator::SlabAllocator(uint64_t allocatedPageSize, std::vector<uint64_t> slotSizes)
{
	if(allocatedPageSize > 0)
		m_allocatedPageSize = allocatedPageSize;

	if (!slotSizes.empty())
	{
		std::vector<uint64_t> validSizes;
		for (uint64_t size : slotSizes)
		{
			if (size >= sizeof(Slot) && (size & (size - 1)) == 0)
				validSizes.push_back(size);
		}
		if(!validSizes.empty())
			m_slotSizes = validSizes;
	}

	std::sort(m_slotSizes.begin(), m_slotSizes.end());

	for (size_t i = 0; i < m_slotSizes.size(); i++)
		m_slabs.push_back({ m_slotSizes[i], nullptr });
}

SlabAllocator::~SlabAllocator() 
{
	for (Byte* page : m_pages) {
		delete[] page;
	}
}



// Implementation of getThreadLocalInstance moved to header for inlining


void SlabAllocator::grow(Slab& slab)
{
	uint64_t slotsPerSlab = m_allocatedPageSize / slab.size;
	Byte* page = new Byte[m_allocatedPageSize];
	m_pages.push_back(page);
	for (auto i = 0; i < slotsPerSlab; i++)
	{
		Slot* slot = reinterpret_cast<Slot*>(page + i * slab.size);
		slot->next = slab.freeList;
		slab.freeList = slot;
	}
}


SlabAllocator::Slot* SlabAllocator::getSlot(Slab& slab)
{
	
	if (slab.freeList == nullptr)
	{
		grow(slab);
	}

	Slot* slot = nullptr;

	if (slab.freeList)
	{
		slot = slab.freeList;
		slab.freeList = slab.freeList->next;
	}
	else
	{
		throw std::bad_alloc();
	}
	
	return slot;
}


void SlabAllocator::returnSlot(Slab& slab, Slot* slot)
{
	slot->next = slab.freeList;
	slab.freeList = slot;
}


void* SlabAllocator::allocate(size_t& size)
{
	if (size > 0x7FFFFFFF) [[unlikely]]		// 2^31 - 1 to prevent overflow in capacity field of heap allocation of CompactString
		std::abort();


	auto iter = std::lower_bound(m_slabs.begin(), m_slabs.end(), size, [](const Slab& slab, size_t val) {
		return slab.size < val;
	});

	if (iter != m_slabs.end())
	{
		Slot* slot = getSlot(*iter);
		size = iter->size;
		m_totalAllocatedBytes += size;
		return reinterpret_cast<void*>(slot);
	}

	m_totalAllocatedBytes += size;
	return ::operator new(size);
}


void SlabAllocator::deallocate(void* ptr, size_t size)
{
	if (ptr == nullptr)
		return;

	auto iter = std::lower_bound(m_slabs.begin(), m_slabs.end(), size, [](const Slab& slab, size_t val) {
		return slab.size < val;
		});

	if (iter != m_slabs.end() && iter->size == size)
	{
		Slot* slot = reinterpret_cast<Slot*>(ptr);
		m_totalAllocatedBytes -= size;
		returnSlot(*iter, slot);
		return;
	}

	m_totalAllocatedBytes -= size;
	::operator delete(ptr);
}
#pragma once


#include <vector>
#include <cstdint>

using Byte = unsigned char;

inline constexpr uint64_t kDefaultAllocatedPageSize = 1024 * 1024; // 1 MB
inline constexpr uint64_t kDefaultSlotSizes[7] = { 32, 64, 128, 256, 512, 1024, 2048 };

class SlabAllocator {

public:

	struct Slot {
		Slot* next = nullptr;
	};

	struct Slab {
		size_t size = 0;
		Slot* freeList = nullptr;
	};

	SlabAllocator(uint64_t allocatedPageSize = 0, std::vector<uint64_t> slotSizes = {});

	~SlabAllocator();

	void* allocate(size_t& size);

	void deallocate(void* ptr, size_t size);

	inline uint64_t getTotalAllocatedBytes() const { return m_totalAllocatedBytes; }

	inline static SlabAllocator* getThreadLocalInstance() {
		static thread_local SlabAllocator instance;
		return &instance;
	}
	
	SlabAllocator(const SlabAllocator& SlabAllocator) = delete;
	SlabAllocator& operator=(const SlabAllocator& SlabAllocator) = delete;
	SlabAllocator(SlabAllocator&& SlabAllocator) = delete;
	SlabAllocator& operator=(SlabAllocator&& SlabAllocator) = delete;

private:

	void grow(Slab& slab);

	Slot* getSlot(Slab& slab);

	void returnSlot(Slab& slab, Slot* slot);

	std::vector<uint64_t> m_slotSizes {std::begin(kDefaultSlotSizes), std::end(kDefaultSlotSizes)};
	uint64_t m_allocatedPageSize = kDefaultAllocatedPageSize;

	uint64_t m_totalAllocatedBytes = 0;

	std::vector<Slab> m_slabs;
	std::vector<Byte*> m_pages;
};
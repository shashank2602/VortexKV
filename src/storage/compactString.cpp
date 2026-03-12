#include "compactString.h"

#include <cstring>





void CompactString::modify(const char* data, uint32_t size)
{
	bool newIsSSO = size <= SSO_CAPACITY;
	bool currentIsSSO = isSSO();

	if (newIsSSO == currentIsSSO)
	{
		// Same storage type: SSO→SSO or Heap→Heap
		if (newIsSSO)
		{
			memcpy(str.sso.data, data, size);
			str.sso.tag_len = static_cast<uint8_t>(size) | SSO_FLAG_MASK;
		}
		else if (size <= str.heap.capacity)
		{
			memcpy(str.heap.data, data, size);
			str.heap.size = size;
		}
		else
		{
			destroy();
			SetHeap(data, size);
		}
	}
	else
	{
		// Different storage type: need to switch
		destroy();
		if (newIsSSO)
		{
			memcpy(str.sso.data, data, size);
			str.sso.tag_len = static_cast<uint8_t>(size) | SSO_FLAG_MASK;
		}
		else
			SetHeap(data, size);
	}
}




void CompactString::SetHeap(const char* data, uint32_t size)
{
	size_t allocatedSize = size;
	str.heap.data = static_cast<char*>(SlabAllocator::getThreadLocalInstance()->allocate(allocatedSize));
	memcpy(str.heap.data, data, size);
	str.heap.size = size;
	str.heap.capacity = static_cast<uint32_t>(allocatedSize);
}
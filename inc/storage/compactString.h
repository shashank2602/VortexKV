#pragma once

#include "slabAllocator.h"

#include <stdint.h>
#include <cstring>
#include <string>
#include <string_view>
#include <array>


constexpr uint32_t SSO_CAPACITY = 15;
constexpr uint8_t SSO_FLAG_MASK = 0x80;


#if defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE __attribute__((always_inline)) inline
#endif

// Binary safe compact string implementation with SSO for strings up to 15 bytes. Not null-terminated.
class CompactString {

public:

	FORCE_INLINE CompactString() noexcept
	{
		str.sso.tag_len = SSO_FLAG_MASK;
	}

	FORCE_INLINE CompactString(const char* data, uint32_t size)
	{
		if (size <= SSO_CAPACITY)
		{
			memcpy(str.sso.data, data, size);
			str.sso.tag_len = static_cast<uint8_t>(size) | SSO_FLAG_MASK;
		}
		else
		{
			SetHeap(data, size);
		}
	}

	FORCE_INLINE CompactString(const std::string& stdStr) : CompactString(stdStr.data(), static_cast<uint32_t>(stdStr.length()))
	{

	}

	FORCE_INLINE CompactString(CompactString&& compactString) noexcept
	{
		str = compactString.str;
		compactString.str.sso.tag_len = SSO_FLAG_MASK;
	}

	FORCE_INLINE ~CompactString()
	{
		destroy();
	}

	FORCE_INLINE CompactString& operator=(CompactString&& compactString) noexcept
	{
		if (this != &compactString)
		{
			destroy();
			str = compactString.str;
			compactString.str.sso.tag_len = SSO_FLAG_MASK;
		}
		return *this;
	}
	
	FORCE_INLINE bool operator==(const CompactString& compactString) const
	{
		uint32_t len = length();
		if (len != compactString.length())
			return false;
		return std::memcmp(data(), compactString.data(), len) == 0;
	}

	FORCE_INLINE bool operator==(const std::string_view& strView) const
	{
		uint32_t len = length();
		if (len != strView.length())
			return false;
		return std::memcmp(data(), strView.data(), len) == 0;
	}

	FORCE_INLINE void swap(CompactString& compactString)
	{
		auto temp = str;
		str = compactString.str;
		compactString.str = temp;
	}

	FORCE_INLINE void destroy()
	{
		if (!isSSO() && str.heap.data)
			SlabAllocator::getThreadLocalInstance()->deallocate(str.heap.data, str.heap.capacity);
	}


	FORCE_INLINE uint32_t length() const
	{
		if (isSSO())
			return str.sso.tag_len ^ SSO_FLAG_MASK;
		return str.heap.size;
	}

	FORCE_INLINE uint32_t capacity() const
	{
		if (isSSO())
			return SSO_CAPACITY;
		return str.heap.capacity;
	}

	FORCE_INLINE uint32_t heapSize() const
	{
		if (isSSO())
			return 0;
		return str.heap.capacity;
	}

	FORCE_INLINE const char* data() const
	{
		return isSSO() ? str.sso.data : str.heap.data;
	}

	void modify(const char* data, uint32_t size);

	CompactString(const CompactString& compactString) = delete;
	CompactString& operator=(const CompactString& compactString) = delete;

private:

	inline bool isSSO() const {
		return (reinterpret_cast<const uint8_t*>(&str)[15] & SSO_FLAG_MASK) != 0;
	}


	void SetHeap(const char* data, uint32_t size);

	// 16 bytes
	union alignas(16) {
		struct {
			char* data;	
			uint32_t size;
			uint32_t capacity; // MSB of the last byte used for tag, capacity must be less that 2^31 to allow tag bit
		} heap;

		struct {
			char data[SSO_CAPACITY];
			uint8_t tag_len;	//	MSB is flag to indicate SSO, lower 7 bits are length
		} sso;
		// Assumes little-endian architecture (most modern systems)
		
	} str;


};
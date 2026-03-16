#pragma once


#include <linearBuffer.h>
#include <config.h>

#include <functional>
#include <unordered_map>
#include <random>
#include <charconv>
#include <format>
#include <span>
#include <thread>


enum class ParseStatus : std::uint8_t {
	Success,
	IncompleteData,
	Error
};


struct ParseResult {
	ParseStatus status;
	int dataRead;
	int intValue;
	std::string_view data;
};


template<typename T, uint8_t InlineCapacity = 4>
class InlineVector {

public:

	InlineVector() = default;

	~InlineVector() {
		clear();
		if (m_heap)
			::operator delete(m_heap);
	}

	InlineVector(const InlineVector& other)
	{
		this->m_size = other.m_size;
		this->m_heapCap = other.m_heapCap;
		if (other.m_heap) 
		{
			this->m_heap = static_cast<T*>(::operator new(sizeof(T) * m_heapCap));
			for (uint32_t i = 0; i < m_size; ++i)
				new(&this->m_heap[i]) T(other.m_heap[i]);
		} 
		else 
		{
			for (uint32_t i = 0; i < m_size; ++i)
				new(this->inlinePtr() + i) T(other.inlinePtr()[i]);
		}
	}

	InlineVector& operator=(const InlineVector& other)
	{
		if (this == &other)
			return *this;
		clear();
		if (m_heap) 
		{
			::operator delete(m_heap);
			m_heap = nullptr;
			m_heapCap = 0;
		}
		this->m_size = other.m_size;
		this->m_heapCap = other.m_heapCap;
		if (other.m_heap) 
		{
			this->m_heap = static_cast<T*>(::operator new(sizeof(T) * m_heapCap));
			for (uint32_t i = 0; i < m_size; ++i)
				new(&this->m_heap[i]) T(other.m_heap[i]);
		} 
		else 
		{
			for (uint32_t i = 0; i < m_size; ++i)
				new(this->inlinePtr() + i) T(other.inlinePtr()[i]);
		}
		return *this;
	}


	InlineVector(InlineVector&& other) noexcept
	{
		if (this == &other)
			return;
		this->m_size = other.m_size;
		this->m_heapCap = other.m_heapCap;
		this->m_heap = other.m_heap;
		if (!other.m_heap) 
		{
			for (uint32_t i = 0; i < m_size; ++i) 
			{
				new(inlinePtr() + i) T(std::move(other.inlinePtr()[i]));
				if constexpr (!std::is_trivially_destructible_v<T>)
					other.inlinePtr()[i].~T();
			}
		}
		other.m_size = 0;
		other.m_heapCap = 0;
		other.m_heap = nullptr;
	}

	InlineVector& operator=(InlineVector&& other) noexcept
	{
		if (this == &other)
			return *this;

		clear();
		if (m_heap) {
			::operator delete(m_heap);
			m_heap = nullptr;
			m_heapCap = 0;
		}

		this->m_size = other.m_size;
		this->m_heapCap = other.m_heapCap;
		this->m_heap = other.m_heap;
		if (!other.m_heap)
		{
			for (uint32_t i = 0; i < m_size; ++i) 
			{
				new(inlinePtr() + i) T(std::move(other.inlinePtr()[i]));
				if constexpr (!std::is_trivially_destructible_v<T>)
					other.inlinePtr()[i].~T();
			}
		}
		other.m_size = 0;
		other.m_heapCap = 0;
		other.m_heap = nullptr;
	}


	void push_back(T val) 
	{
		if (!m_heap && m_size < InlineCapacity) 
		{
			new(inlinePtr() + m_size) T(std::move(val));
			++m_size;
		} 
		else 
		{
			pushHeap(std::move(val));
		}
	}

	T& operator[](size_t i) { return data()[i]; }
	const T& operator[](size_t i) const { return data()[i]; }

	T* data() { return m_heap ? m_heap : inlinePtr(); }
	const T* data() const { return m_heap ? m_heap : inlinePtr(); }

	uint32_t size()  const { return m_size; }
	bool empty() const { return m_size == 0; }

	void clear() 
	{
		if constexpr (!std::is_trivially_destructible_v<T>) 
		{
			T* d = data();
			for (uint32_t i = 0; i < m_size; ++i)
				d[i].~T();
		}
		m_size = 0;
	}

	operator std::span<const T>() const { return { data(), m_size }; }
	operator std::span<T>() { return { data(), m_size }; }


private:

	T* inlinePtr() {
		return std::launder(reinterpret_cast<T*>(m_storage));
	}
	const T* inlinePtr() const {
		return std::launder(reinterpret_cast<const T*>(m_storage));
	}

	void pushHeap(T val) {
		if (m_heap == nullptr) 
		{
			uint32_t newCap = InlineCapacity * 2;
			m_heap = static_cast<T*>(::operator new(sizeof(T) * newCap));
			T* src = inlinePtr();
			for (uint32_t i = 0; i < m_size; ++i) 
			{
				new(&m_heap[i]) T(std::move(src[i]));
				if constexpr (!std::is_trivially_destructible_v<T>)
					src[i].~T();
			}
			m_heapCap = newCap;
		} else if (m_size >= m_heapCap) 
		{
			uint32_t newCap = m_heapCap * 2;
			T* newHeap = static_cast<T*>(::operator new(sizeof(T) * newCap));
			for (uint32_t i = 0; i < m_size; ++i) 
			{
				new(&newHeap[i]) T(std::move(m_heap[i]));
				if constexpr (!std::is_trivially_destructible_v<T>)
					m_heap[i].~T();
			}
			::operator delete(m_heap);
			m_heap    = newHeap;
			m_heapCap = newCap;
		}
		new(&m_heap[m_size]) T(std::move(val));
		++m_size;
	}

	uint32_t m_size    = 0;
	uint32_t m_heapCap = 0;
	T* m_heap    = nullptr;										// null  = inline mode
	alignas(alignof(T)) unsigned char m_storage[sizeof(T) * InlineCapacity];	// inline storage
};


struct CommandRequest {
	std::string_view type;
	InlineVector<std::string_view, 4> arguments;	// 4 inline slots covers 99%+ of commands

	inline void reset() {
		type = std::string_view();
		arguments.clear();
	}
};


class InlineResponseBuffer {

private:
	constexpr static int kInlineCapacity = 128;

	char m_inlineBuffer[kInlineCapacity] = {0};
	std::vector<char> m_heapBuffer;
	uint64_t m_size = 0;

public:

	InlineResponseBuffer() = default;

	InlineResponseBuffer(const char* data, uint64_t size);

	~InlineResponseBuffer() = default;

	InlineResponseBuffer(InlineResponseBuffer&& other) noexcept;

	InlineResponseBuffer& operator=(InlineResponseBuffer&& other) noexcept;

	void assign(const char* data, uint64_t size);

	const char* data() const { return m_size <= kInlineCapacity ? m_inlineBuffer : m_heapBuffer.data(); }

	uint64_t size() const { return m_size; }

	InlineResponseBuffer(const InlineResponseBuffer& other) = delete;
	InlineResponseBuffer& operator=(const InlineResponseBuffer& other) = delete;
};



class RandomGenerator {

public:

	RandomGenerator();
	RandomGenerator(int64_t min, int64_t max);

	int64_t getRandomInteger(int64_t min, int64_t max);
	
private:
		
	std::mt19937 m_gen;
	std::uniform_int_distribution<int64_t> m_dist;
	int64_t m_min = 0, m_max= 100;
};


inline void transformToUpper(const char* data, size_t size, char* output)
{
	for (size_t i = 0; i < size; ++i)
		output[i] = static_cast<char>(data[i] >= 'a' && data[i] <= 'z' ? data[i] - 32 : data[i]);
}

inline uint64_t transformToUpperAndPackInInteger(const char* data, size_t size)
{
	uint64_t outputKey = 0;
	size = std::min(size, static_cast<size_t>(8));
	for (size_t i = 0; i < size; ++i)
	{
		char c = static_cast<char>(data[i] >= 'a' && data[i] <= 'z' ? data[i] - 32 : data[i]);
		outputKey |= static_cast<uint64_t>(c) << (i * 8);
	}
	return outputKey;
}


inline bool strict_integer_parse(std::string_view str, long long& value)
{
	auto result = std::from_chars(str.data(), str.data() + str.size(), value);
	return result.ec == std::errc() && result.ptr == str.data() + str.size();
}

void PinThreadToCore(std::jthread& thread, int coreId);


// Cross-platform prefetch support
#if defined(_MSC_VER)
#include <xmmintrin.h>
#define PREFETCH_READ(addr, locality) _mm_prefetch((const char*)(addr), locality)
#define PREFETCH_L1 _MM_HINT_T0   // All cache levels
#define PREFETCH_L2 _MM_HINT_T1   // L2 and L3
#define PREFETCH_L3 _MM_HINT_T2   // L3 only
#define PREFETCH_NTA _MM_HINT_NTA // Non-temporal (streaming)
#elif defined(__GNUC__) || defined(__clang__)
#define PREFETCH_READ(addr, locality) __builtin_prefetch((const void*)(addr), 0, locality)
#define PREFETCH_L1 3  // High temporal locality (all cache levels)
#define PREFETCH_L2 2  // Moderate temporal locality (L2/L3)
#define PREFETCH_L3 1  // Low temporal locality (L3)
#define PREFETCH_NTA 0 // No temporal locality
#else
#define PREFETCH_READ(addr, locality) ((void)0)
#define PREFETCH_L1 0
#define PREFETCH_L2 0
#define PREFETCH_L3 0
#define PREFETCH_NTA 0
#endif


// Cross-platform thread affinity headers
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
// Linux and other Unix-like systems
#include <pthread.h>
#include <sched.h>
#endif
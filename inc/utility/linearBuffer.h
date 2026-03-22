#pragma once


#include <string_view>
#include <vector>


class LinearBuffer {

public:


	LinearBuffer(size_t initialSize = linearBufferDefaultSize);

	void append(const char* data, size_t size);

	void seekWrite(size_t size);

	void seekRead(size_t size);

	void compaction();

	char* reserveContiguous(size_t size);

	inline std::string_view getView() const {
		return std::string_view(m_buffer.data() + m_tail, this->size());
	}


	inline void reset() {
		m_head = 0;
		m_tail = 0;
	}
	

	inline bool empty() const {
		return m_head == m_tail;
	}

	inline size_t capacity() const {
		return m_buffer.size();
	}

	inline size_t size() const {
		return m_head - m_tail;
	}

	inline char* writePtr() {
		return m_buffer.data() + m_head;
	}

	inline char* readPtr() {
		return m_buffer.data() + m_tail;
	}

	inline size_t remainingWriteSize() const {
		return capacity() - m_head;
	}

private:

	std::vector<char> m_buffer;
	size_t m_head;
	size_t m_tail;

	static const std::size_t linearBufferDefaultSize = 64 * 1024;
	static const std::size_t minBufferSpaceRequired = 8 * 1024;
};
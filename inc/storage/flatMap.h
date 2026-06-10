#pragma once

#include "utility.h"
#include "rapidhash.h"

#include <variant>
#include <string>
#include <optional>
#include <vector>
#include <iostream>
#include <concepts>


constexpr uint64_t INITIAL_MAP_SIZE = 256;
constexpr double RESIZE_LOAD_FACTOR = 0.80;
constexpr uint64_t DEFAULT_RESIZE_STEP_COUNT = 32;
constexpr uint32_t PREFETCH_DISTANCE = 2; // Prefetch 2 slots ahead in probe sequence



template <typename T>
concept StringLike =

	requires(T t, std::string_view sv) {
		{ t.data() } -> std::convertible_to<const char*>;
		{ t.length() } -> std::convertible_to<std::size_t>;
		{ t == sv }  -> std::convertible_to<bool>;
};


template <typename KeyType, typename ValueType>
class FlatMap {

	static_assert(StringLike<KeyType>, "Key must be a string type with data(), length() and comparable to std::string_view");
		
public:

	using String = KeyType;
	//using List = std::vector<String>;
	using Value = ValueType;

	struct Metadata {
		uint8_t psl = 0;	// psl = 0 means empty slot, psl = 1 means first probe, etc.
		uint8_t tag = 0;	// bits 0 - 6 are hash tag, bit 7 is visited flag

		Metadata() = default;
		Metadata(uint8_t _psl, uint8_t _tag) : psl(_psl), tag(_tag & 0x7F) {}

		inline bool getVisited() const { return tag & 0x80; }
		inline void setVisited() { tag |= 0x80; }
		inline void resetVisited() { tag &= 0x7F; }
		inline uint8_t getTag() const { return tag & 0x7F; }
	};

	struct Table {

		std::vector<String> keys;
		std::vector<Value> values;
		std::vector<Metadata> metadata;
		std::vector<uint32_t> hashes;
		uint32_t size = 0;
		
		void resize(uint32_t size) {
			this->size = size;
			keys.resize(size);
			values.resize(size);
			metadata.resize(size);
			hashes.resize(size);
		}

		void freeMemory() {
			this->size = 0;
			std::vector<String>().swap(keys);
			std::vector<Value>().swap(values);
			std::vector<Metadata>().swap(metadata);
			std::vector<uint32_t>().swap(hashes);
		}

		uint64_t getMemoryUsage() const {
			uint64_t total = 0;
			total += sizeof(String) * keys.capacity();
			total += sizeof(Value) * values.capacity();
			total += sizeof(Metadata) * metadata.capacity();
			total += sizeof(uint32_t) * hashes.capacity();
			return total;
		}
	};


private:

	Table m_table, m_tableOld;
	uint32_t m_objectCount = 0;
	uint32_t m_resizeThreshold = 0; // Cached threshold to avoid division
	bool m_isResizing = false;
	bool m_allowGrowth = true;
	bool m_evictDuringMigration = false;
	uint32_t m_migrateIndex = 0;
	uint32_t m_sieveHand = 0;
	uint64_t m_hashSeed = 0;
	uint32_t m_currentResizeStep = 0;

	RandomGenerator m_randomGen;



public:
	FlatMap(): m_currentResizeStep(DEFAULT_RESIZE_STEP_COUNT)
	{
		static_assert((INITIAL_MAP_SIZE & (INITIAL_MAP_SIZE - 1)) == 0, "Initial map size must be a power of two");
		m_table.resize(INITIAL_MAP_SIZE);
		updateResizeThreshold();
	}


	~FlatMap() {}


	bool insert(String key, Value value, std::optional<uint32_t> storedHash = std::nullopt) {
		if (m_isResizing)
		{
			resizeStep();
			remove(std::string_view(key.data(), key.length()), m_tableOld);
		}

		bool newEntryInserted = rawInsert(std::move(key), std::move(value), storedHash);
		m_objectCount += newEntryInserted;
		return newEntryInserted;
	}


	Value* find(std::string_view key, std::optional<uint32_t> storedHash = std::nullopt, bool markVisited = false) {
		uint32_t hash = storedHash.has_value() ? storedHash.value() : calculateHash(key.data(), key.size());
		if(auto entry = find(key, hash, m_table, markVisited); entry != nullptr)
			return entry;

		if (m_isResizing && ((hash & (m_tableOld.size - 1)) >= m_migrateIndex))
			return find(key, hash, m_tableOld, markVisited);
		return nullptr;
	}



	bool remove(std::string_view key, std::optional<uint32_t> storedHash = std::nullopt) {
		bool isRemoved = remove(key, m_table, storedHash) || remove(key, m_tableOld, storedHash);
		m_objectCount -= isRemoved;
		resizeStep();
		return isRemoved;
	}


	std::pair<std::string_view, Value*> getRandomEntry(uint32_t probeLength = 10) {
		size_t tableSize = m_table.keys.size();
		if (tableSize == 0)
			return { std::string_view(), nullptr };
		size_t startIndex = m_randomGen.getRandomInteger(0, tableSize - 1);

		for (uint32_t i = 0; i < probeLength; i++)
		{
			size_t index = (startIndex + i) % tableSize;
			if (m_table.metadata[index].psl != 0)
			{
				return { std::string_view(m_table.keys[index].data(), m_table.keys[index].length()), &m_table.values[index] };
			}
		}
		return { std::string_view(), nullptr };
	}


	bool evictOneEntry(bool mustEvict = false, uint32_t maxProbes = 64)
	{
		Table& targetTable = m_table;
		uint32_t tableSize = targetTable.size;
		if (tableSize == 0) return {};

		uint32_t mask = (uint32_t)targetTable.keys.size() - 1;
		m_sieveHand = m_sieveHand & mask;
		uint32_t probes = 0;

		std::string_view candidateKey;

		while (probes < maxProbes) 
		{
			Metadata& meta = targetTable.metadata[m_sieveHand];

			if (meta.psl != 0) 
			{
				auto& key = targetTable.keys[m_sieveHand];
				candidateKey = { key.data(), key.length() };
				if (meta.getVisited()) 
				{
					meta.resetVisited();
				}
				else 
				{
					remove(candidateKey, targetTable);
					return true;
				}
			}
			m_sieveHand = (m_sieveHand + 1) & mask;
			probes++;
		}

		if (mustEvict && !candidateKey.empty())
		{
			remove(candidateKey, targetTable);
			return true;
		}	
		
		return false;
	}

	// Does NOT include memory heap memory used by keys and values on their own
	inline uint64_t getTableMemoryUsage() const {
		
		return m_table.getMemoryUsage() + (m_isResizing ? m_tableOld.getMemoryUsage() : 0);
	}


	inline void setAllowGrowth(bool allowGrowth) {
		m_allowGrowth = allowGrowth;
	}


	inline size_t getInternalTableSize() const {
		return m_table.size + (m_isResizing ? m_tableOld.size : 0);
	}

	uint64_t calculateHash(const char* data, size_t dataSize) const{
		return rapidhash_withSeed(data, dataSize, m_hashSeed);
	}

	inline void setHashSeed(uint64_t seed) {
		m_hashSeed = seed;
	}

	inline void setResizeStepCount(uint32_t stepCount = DEFAULT_RESIZE_STEP_COUNT) {
		m_currentResizeStep = stepCount;
	}

	inline void setEvictDuringMigration(bool evict) {
		m_evictDuringMigration = evict;
	}

	inline uint64_t size() const { return m_objectCount; }

	void clear() {
		m_table.freeMemory();
		m_tableOld.freeMemory();
		m_objectCount = 0;
		m_resizeThreshold = 0;
		m_isResizing = false;
		m_migrateIndex = 0;
	}

	inline void prefetchForKey(std::string_view key) {
		uint32_t hash = calculateHash(key.data(), key.size());
		uint32_t mask = (uint32_t)m_table.keys.size() - 1;
		uint32_t keyPos = hash & mask;
		PREFETCH_READ(&m_table.metadata[keyPos], PREFETCH_L1);
		PREFETCH_READ(&m_table.keys[keyPos], PREFETCH_L2);
	}


private:

	bool rawInsert(String key, Value value, std::optional<uint32_t> storedHash = std::nullopt) {
		uint32_t hash;
		if (storedHash.has_value())
			hash = storedHash.value();
		else
			hash = calculateHash(key.data(), key.length());

		uint32_t mask = (uint32_t)m_table.keys.size() - 1;
		uint32_t keyPos = hash & mask;
		Metadata newEntryMetadata (1, static_cast<uint8_t>(hash & 0xFF));
		const uint8_t newTag = newEntryMetadata.getTag();

		PREFETCH_READ(&m_table.metadata[keyPos], PREFETCH_L1);
		PREFETCH_READ(&m_table.keys[keyPos], PREFETCH_L1);

		for (uint32_t i = 0; i < m_table.size; i++)
		{
			// Prefetch ahead in the probe sequence
			// Use L2 prefetch to avoid polluting L1 cache with speculative data
			/*if (i + 4 < m_table.size) {
				uint32_t prefetchPos = (keyPos + 4) & mask;
				PREFETCH_READ(&m_table.metadata[prefetchPos], PREFETCH_L1);
				PREFETCH_READ(&m_table.keys[prefetchPos], PREFETCH_L1);
			}*/

			if (m_table.metadata[keyPos].psl == 0)
			{
				m_table.keys[keyPos] = std::move(key);
				m_table.values[keyPos] = std::move(value);
				m_table.metadata[keyPos] = newEntryMetadata;
				m_table.hashes[keyPos] = hash;
				
				// Use cached threshold instead of division
				if (!m_isResizing && m_allowGrowth && m_objectCount >= m_resizeThreshold)
					startResize();
				return true;
			}

			// Optimized comparison: check PSL and tag first (cheaper), then full key
			if (m_table.metadata[keyPos].psl == newEntryMetadata.psl && 
			    m_table.metadata[keyPos].getTag() == newTag && 
			    m_table.keys[keyPos] == key)
			{
					m_table.values[keyPos] = std::move(value);
					return false;
			}

			if (newEntryMetadata.psl > m_table.metadata[keyPos].psl)
			{
				//std::swap(key, m_table.keys[keyPos]);
				key.swap(m_table.keys[keyPos]);
				std::swap(value, m_table.values[keyPos]);
				std::swap(newEntryMetadata, m_table.metadata[keyPos]);
				std::swap(hash, m_table.hashes[keyPos]);
			}
				

			keyPos = (keyPos + 1) & mask;
			newEntryMetadata.psl++;
		}
		return false;
	}

	Value* find(std::string_view key, uint32_t keyHash, Table& table, bool markVisited = false) {
		auto keyIndex = findKeyIndex(key, keyHash, table);

		if (keyIndex.has_value() == false)
			return nullptr;

		if (markVisited)
			table.metadata[keyIndex.value()].setVisited();
		return &table.values[keyIndex.value()];
	}

	bool remove(std::string_view key, Table& table, std::optional<uint32_t> storedHash = std::nullopt) {

		uint32_t hash = storedHash.has_value() ? storedHash.value() : calculateHash(key.data(), key.size());
		auto keyIndex = findKeyIndex(key, hash, table);

		if (keyIndex.has_value() == false)
			return false;

		uint32_t deleteIndex = keyIndex.value();
		uint32_t mask = (uint32_t)table.keys.size() - 1;
		uint32_t nextIndex = (deleteIndex + 1) & mask;
		auto&& keys = table.keys;
		auto&& values = table.values;
		auto&& metadata = table.metadata;
		auto&& hashes = table.hashes;

		for (uint32_t i = 0; i < table.size; i++)
		{

			if (metadata[nextIndex].psl == 0 || metadata[nextIndex].psl == 1)
				break;

			keys[deleteIndex] = std::move(keys[nextIndex]);
			values[deleteIndex] = std::move(values[nextIndex]);
			metadata[deleteIndex] = metadata[nextIndex];
			metadata[deleteIndex].psl--;
			hashes[deleteIndex] = hashes[nextIndex];

			deleteIndex = nextIndex;
			nextIndex = (nextIndex + 1) & mask;
		}

		keys[deleteIndex] = String();
		values[deleteIndex] = Value();
		metadata[deleteIndex] = Metadata();

		return true;
	}

	std::optional<uint32_t> findKeyIndex(std::string_view key, uint32_t keyHash, const Table& table) const {
		if (table.keys.empty())
			return std::nullopt;

		uint32_t hash = keyHash;
		uint32_t mask = (uint32_t)table.keys.size() - 1;
		uint32_t keyPos = hash & mask;

		// Initial prefetch for the first position
		// Use L2 to minimize L1 cache pollution for potentially unsuccessful searches
		PREFETCH_READ(&table.metadata[keyPos], PREFETCH_L1);
		PREFETCH_READ(&table.keys[keyPos], PREFETCH_L1);

		Metadata newEntryMetadata(1, static_cast<uint8_t>(hash & 0xFF));
		const uint8_t newTag = newEntryMetadata.getTag();
		auto&& metadata = table.metadata;

		for (uint32_t i = 0; i < table.size; i++)
		{
			//// Prefetch ahead in the probe sequence
			//if (i + 4 < table.size) {
			//	uint32_t prefetchPos = (keyPos + 4) & mask;
			//	PREFETCH_READ(&metadata[prefetchPos], PREFETCH_L1);
			//	PREFETCH_READ(&table.keys[prefetchPos], PREFETCH_L1);
			//}

			if (metadata[keyPos].psl == 0 || metadata[keyPos].psl < newEntryMetadata.psl)
				return std::nullopt;

			// Optimized comparison: PSL and tag checks are cheaper than full key comparison
			if (metadata[keyPos].psl == newEntryMetadata.psl && 
			    metadata[keyPos].getTag() == newTag && 
			    table.keys[keyPos] == key)
			{
				return keyPos;
			}
					
			
			keyPos = (keyPos + 1) & mask;
			newEntryMetadata.psl++;
		}

		return std::nullopt;
	}

	double loadFactor() const { 
		return static_cast<double>(m_objectCount) / static_cast<double>(m_table.size);
	}

	void updateResizeThreshold() {
		m_resizeThreshold = static_cast<uint32_t>(m_table.size * RESIZE_LOAD_FACTOR);
	}

	void startResize() {
		size_t newSize = m_table.size * 2;
		m_tableOld = std::move(m_table);
		m_table.resize(newSize);
		m_migrateIndex = 0;
		m_isResizing = true;
		m_sieveHand = 0;
		updateResizeThreshold();
	}

	void resizeStep() {
		uint32_t steps = m_currentResizeStep;
		while (m_migrateIndex < m_tableOld.size && (steps-- > 0 || m_tableOld.metadata[m_migrateIndex].psl != 0))
		{
			if (m_tableOld.metadata[m_migrateIndex].psl != 0)
			{
				m_tableOld.metadata[m_migrateIndex].psl = 0;
				if (!m_evictDuringMigration || m_tableOld.metadata[m_migrateIndex].getVisited())
				{
					rawInsert(std::move(m_tableOld.keys[m_migrateIndex]), std::move(m_tableOld.values[m_migrateIndex]), m_tableOld.hashes[m_migrateIndex]);
				}
				else
				{
					m_objectCount--;
				}
			}
			m_migrateIndex++;
		}

		if (m_migrateIndex >= m_tableOld.size)
		{
			m_isResizing = false;
			m_tableOld.freeMemory();
		}
	}

};
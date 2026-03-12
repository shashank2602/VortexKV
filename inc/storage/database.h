#pragma once

#include "compactString.h"
#include "flatMap.h"
#include "databaseErrors.h"

#include <charconv>
#include <span>


constexpr uint64_t DefaultMemoryUsageThreshold = 1024ULL * 1024 * 1024;
constexpr int64_t  NO_EXPIRY = 0;


struct DBResult {
	
private:
	std::string_view m_strView;
	char m_intBuffer[22];
	int8_t m_bufferLen = -1;

public:
	DatabaseError error;

public:
	
	DBResult(DatabaseError err) : error(err) {}

	DBResult(DatabaseError err, std::string_view val) : error(err), m_strView(val) {}

	DBResult(DatabaseError err, long long intValue) : error(err)
	{
		auto [ptr, ec] = std::to_chars(m_intBuffer, m_intBuffer + sizeof(m_intBuffer), intValue);
		m_bufferLen = static_cast<int8_t>(ptr - m_intBuffer);
	}

	std::string_view str() const {
		if (m_bufferLen != -1)
			return std::string_view(m_intBuffer, m_bufferLen);
		return m_strView;
	}
};


class Database {

public:

	using DBValue = std::variant<CompactString, long long>;

	struct CachedEntry {
		DBValue value;
		int64_t expiry = NO_EXPIRY;

		CachedEntry() = default;
		CachedEntry(DBValue v, int64_t exp = NO_EXPIRY) : value(std::move(v)), expiry(exp) {}

		inline bool hasExpiry() const { return expiry != NO_EXPIRY; }
		inline bool isExpired(int64_t now) const { return expiry != NO_EXPIRY && expiry <= now; }
	};

	using StorageMap = FlatMap<CompactString, CachedEntry>;


private:

	StorageMap m_storage;

	uint64_t m_memoryUsage = 0;
	uint64_t m_memoryUsageThreshold = DefaultMemoryUsageThreshold;

	int64_t m_currentTimeInMillis = 0;

public:

	Database(uint64_t memoryUsageThreshold = DefaultMemoryUsageThreshold);

	DBResult SET(std::string_view key, std::string_view value, std::optional<std::string_view> ttl = std::nullopt);

	DBResult GET(std::string_view key);

	DBResult DEL(std::span<const std::string_view> keys);

	DBResult EXISTS(std::span<const std::string_view> keys);

	DBResult INCR(std::string_view key);

	DBResult DECR(std::string_view key);

	DBResult INCRBY(std::string_view key, std::string_view delta);

	DBResult DECRBY(std::string_view key, std::string_view delta);

	DBResult EXPIRE(std::string_view key, std::string_view ttl);

	DBResult PERSIST(std::string_view key);

	DBResult TTL(std::string_view key);

	void runMaintenance();

	void updateCurrentTime(int64_t time);

	inline void prefetchForKey(std::string_view key)
	{
		m_storage.prefetchForKey(key);
	}

private:

	DBResult doAddition(std::string_view key, int64_t delta);

	std::optional<int64_t> getIntegerValue(std::string_view key);

	static std::optional<int64_t> addSafe(int64_t a, int64_t b);

	uint64_t calculateMemoryUsage();
};

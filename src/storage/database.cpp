#include "database.h"
#include "slabAllocator.h"

#include <chrono>


Database::Database(uint64_t memoryUsageThreshold) : m_memoryUsageThreshold(memoryUsageThreshold)
{
	RandomGenerator randomGen;
	uint64_t randomSeed = randomGen.getRandomInteger(1, std::numeric_limits<long long>::max());
	m_storage.setHashSeed(randomSeed);
}


DBResult Database::SET(std::string_view key, std::string_view value, std::optional<std::string_view> ttl)
{
	if (m_memoryUsage > m_memoryUsageThreshold)
	{
		int evictionIterations = 10;
		while (evictionIterations-- > 0)
		{
			m_storage.evictOneEntry(true);
		}

		m_storage.setResizeStepCount(128);
		m_storage.setEvictDuringMigration(true);

		if (calculateMemoryUsage() > m_memoryUsageThreshold)
			return DBResult(DatabaseError::OUT_OF_MEMORY);
	}
	else
	{
		m_storage.setResizeStepCount();
		m_storage.setEvictDuringMigration(false);
	}

	//uint32_t hash = m_storage.calculateHash(key.data(), key.size());

	int64_t expiry = NO_EXPIRY;
	if (ttl.has_value())
	{
		auto ttlInt = getIntegerValue(ttl.value());
		if (ttlInt.has_value() == false || ttlInt.value() <= 0)
			return DBResult(DatabaseError::NOT_AN_INTEGER_OR_OUT_OF_RANGE);
		expiry = m_currentTimeInMillis + ttlInt.value();
	}

	DBValue val;
	auto intValue = getIntegerValue(value);
	if (intValue.has_value())
		val = intValue.value();
	else
		val = CompactString(value.data(), value.size());

	m_storage.insert(CompactString(key.data(), key.size()), CachedEntry(std::move(val), expiry));
	return DBResult(DatabaseError::SUCCESS);
}


DBResult Database::GET(std::string_view key)
{
	uint32_t hash = m_storage.calculateHash(key.data(), key.size());

	auto entryPtr = m_storage.find(key, hash, true);
	if (entryPtr == nullptr)
		return DBResult(DatabaseError::KEY_NOT_FOUND);

	if (entryPtr->isExpired(m_currentTimeInMillis))
	{
		m_storage.remove(key, hash);
		return DBResult(DatabaseError::KEY_NOT_FOUND);
	}

	if (auto val = std::get_if<long long>(&entryPtr->value))
		return DBResult(DatabaseError::SUCCESS, *val);

	if (auto val = std::get_if<CompactString>(&entryPtr->value))
		return DBResult(DatabaseError::SUCCESS, std::string_view(val->data(), val->length()));

	return DBResult(DatabaseError::WRONG_TYPE);
}


DBResult Database::DEL(std::string_view key)
{
	int64_t deletedCount = 0;

	uint32_t hash = m_storage.calculateHash(key.data(), key.size());
	auto entryPtr = m_storage.find(key, hash);
	bool keyExpired = entryPtr && entryPtr->isExpired(m_currentTimeInMillis);

	if (entryPtr && m_storage.remove(key, hash) && !keyExpired)
		deletedCount++;

	return DBResult(DatabaseError::SUCCESS, deletedCount);
}


DBResult Database::EXISTS(std::string_view key)
{
	int64_t existsCount = 0;

	uint32_t hash = m_storage.calculateHash(key.data(), key.size());
	auto entryPtr = m_storage.find(key, hash);

	if (entryPtr == nullptr)
		return DBResult(DatabaseError::SUCCESS, existsCount);

	if (entryPtr->isExpired(m_currentTimeInMillis))
	{
		m_storage.remove(key, hash);
		return DBResult(DatabaseError::SUCCESS, existsCount);
	}

	existsCount++;

	return DBResult(DatabaseError::SUCCESS, existsCount);
}


DBResult Database::INCR(std::string_view key)
{
	return doAddition(key, 1);
}


DBResult Database::DECR(std::string_view key)
{
	return doAddition(key, -1);
}


DBResult Database::INCRBY(std::string_view key, std::string_view delta)
{
	auto intValue = getIntegerValue(delta);
	if (intValue.has_value())
		return doAddition(key, intValue.value());
	return DBResult(DatabaseError::NOT_AN_INTEGER_OR_OUT_OF_RANGE);
}


DBResult Database::DECRBY(std::string_view key, std::string_view delta)
{
	auto intValue = getIntegerValue(delta);
	if (intValue.has_value())
		return doAddition(key, -intValue.value());
	return DBResult(DatabaseError::NOT_AN_INTEGER_OR_OUT_OF_RANGE);
}


DBResult Database::EXPIRE(std::string_view key, std::string_view ttl)
{
	auto ttlInt = getIntegerValue(ttl);
	if (ttlInt.has_value() == false || ttlInt.value() <= 0)
		return DBResult(DatabaseError::NOT_AN_INTEGER_OR_OUT_OF_RANGE);

	uint32_t hash = m_storage.calculateHash(key.data(), key.size());

	auto entryPtr = m_storage.find(key, hash);
	if (entryPtr == nullptr)
		return DBResult(DatabaseError::KEY_NOT_FOUND, 0);

	if (entryPtr->isExpired(m_currentTimeInMillis))
	{
		m_storage.remove(key, hash);
		return DBResult(DatabaseError::KEY_NOT_FOUND, 0);
	}

	entryPtr->expiry = m_currentTimeInMillis + ttlInt.value();
	return DBResult(DatabaseError::SUCCESS, 1);
}


DBResult Database::PERSIST(std::string_view key)
{
	uint32_t hash = m_storage.calculateHash(key.data(), key.size());

	auto entryPtr = m_storage.find(key, hash);
	if (entryPtr == nullptr)
		return DBResult(DatabaseError::KEY_NOT_FOUND, 0);

	if (entryPtr->isExpired(m_currentTimeInMillis))
	{
		m_storage.remove(key, hash);
		return DBResult(DatabaseError::KEY_NOT_FOUND, 0);
	}

	if (!entryPtr->hasExpiry())
		return DBResult(DatabaseError::NO_EXPIRATION_SET, 0);

	entryPtr->expiry = NO_EXPIRY;
	return DBResult(DatabaseError::SUCCESS, 1);
}


DBResult Database::TTL(std::string_view key)
{
	uint32_t hash = m_storage.calculateHash(key.data(), key.size());

	auto entryPtr = m_storage.find(key, hash);
	if (entryPtr == nullptr)
		return DBResult(DatabaseError::KEY_NOT_FOUND, -2);

	if (!entryPtr->hasExpiry())
		return DBResult(DatabaseError::NO_EXPIRATION_SET, -1);

	int64_t now = m_currentTimeInMillis;
	if (entryPtr->expiry <= now)
	{
		m_storage.remove(key, hash);
		return DBResult(DatabaseError::KEY_NOT_FOUND, -2);
	}

	return DBResult(DatabaseError::SUCCESS, entryPtr->expiry - now);
}


DBResult Database::doAddition(std::string_view key, int64_t delta)
{
	uint32_t hash = m_storage.calculateHash(key.data(), key.size());

	auto entryPtr = m_storage.find(key, hash);

	if (entryPtr == nullptr || entryPtr->isExpired(m_currentTimeInMillis))
	{
		// New key or lazily-expired key: create fresh entry with value = delta, no expiry
		m_storage.insert(CompactString(key.data(), key.size()), CachedEntry(static_cast<long long>(delta)), hash);
		return { DatabaseError::SUCCESS, delta };
	}

	if (auto val = std::get_if<long long>(&entryPtr->value))
	{
		auto newValue = addSafe(*val, delta);
		if (newValue.has_value())
		{
			*val = newValue.value();
			return DBResult(DatabaseError::SUCCESS, newValue.value());
		}
		return DBResult(DatabaseError::VALUE_OVERFLOW);
	}

	if (auto val = std::get_if<CompactString>(&entryPtr->value))
	{
		auto intValue = getIntegerValue(std::string_view(val->data(), val->length()));
		if (intValue.has_value())
		{
			auto newValue = addSafe(intValue.value(), delta);
			if (newValue.has_value())
			{
				entryPtr->value = newValue.value();
				return DBResult(DatabaseError::SUCCESS, newValue.value());
			}
			return DBResult(DatabaseError::VALUE_OVERFLOW);
		}
	}

	return DBResult(DatabaseError::NOT_AN_INTEGER_OR_OUT_OF_RANGE);
}


std::optional<int64_t> Database::getIntegerValue(std::string_view str)
{
	if(str.empty())
		return std::nullopt;
	if(str[0] == '+')
		return std::nullopt;
	if(str.size() > 1 && str[0] == '0')
		return std::nullopt;
	if(str.size() > 1 && str[0] == '-' && str[1] == '0')
		return std::nullopt;

	long long value = 0;
	bool parseIntResult = strict_integer_parse(str, value);
	if (parseIntResult)
		return value;
	return std::nullopt;
}


void Database::runMaintenance()
{
	const int MAX_CLEANUP_ITERATIONS = 10;
	const int CLEANUP_BATCH_SIZE = 20;
	const int THRESHOLD = CLEANUP_BATCH_SIZE / 4;

	int64_t now = m_currentTimeInMillis;

	for (int i = 0; i < MAX_CLEANUP_ITERATIONS; i++)
	{
		int expiredFound = 0;
		for (int j = 0; j < CLEANUP_BATCH_SIZE; j++)
		{
			auto [keyView, entryPtr] = m_storage.getRandomEntry(10);
			if (entryPtr == nullptr)
				continue;

			if (entryPtr->isExpired(now))
			{
				m_storage.remove(keyView);
				expiredFound++;
			}
		}
		if (expiredFound < THRESHOLD)
			break;
	}

	calculateMemoryUsage();
	bool hasMemoryHeadroom = m_memoryUsage < (m_memoryUsageThreshold * 0.9);
	m_storage.setAllowGrowth(hasMemoryHeadroom);

	updateCurrentTime(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()
	).count());
}


void Database::updateCurrentTime(int64_t time)
{
	m_currentTimeInMillis = time;
}


std::optional<int64_t> Database::addSafe(int64_t a, int64_t b)
{
	if (a > 0 && b > (std::numeric_limits<long long>::max() - a))
		return std::nullopt;
	if (a < 0 && b < (std::numeric_limits<long long>::min() - a))
		return std::nullopt;
	return a + b;
}


uint64_t Database::calculateMemoryUsage()
{
	return m_memoryUsage = m_storage.getTableMemoryUsage() + SlabAllocator::getThreadLocalInstance()->getTotalAllocatedBytes();
}
#include "utility.h"




RandomGenerator::RandomGenerator() : m_gen(std::random_device{}()) {}

RandomGenerator::RandomGenerator(int64_t min, int64_t max) : m_gen(std::random_device{}()), m_dist(min, max) {}

int64_t RandomGenerator::getRandomInteger(int64_t min, int64_t max)
{
	if (min != m_min || max != m_max)
	{
		m_min = min;
		m_max = max;
		m_dist = std::uniform_int_distribution<int64_t>(min, max);
	}
	return m_dist(m_gen);
}


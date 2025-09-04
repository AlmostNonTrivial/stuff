#pragma once
#include <random>
#include <cstdint>
#include "defs.hpp"
#include "os_layer.hpp"

inline uint32_t
random_number(uint32_t min = 0, uint32_t max = UINT32_MAX)
{
	std::random_device rd;
	std::mt19937	   gen(rd());

	std::uniform_int_distribution<uint32_t> dis(min, max);

	return dis(gen);
}

inline void
random_range(uint32_t *buffer, size_t count, uint32_t min = 0, uint32_t max = UINT32_MAX)
{
	auto									seed = 12345;
	std::mt19937							rng(seed);
	std::uniform_int_distribution<uint32_t> dist(min, max);
	for (size_t i = 0; i < count; i++)
	{
		buffer[i] = dist(rng);
	}
}


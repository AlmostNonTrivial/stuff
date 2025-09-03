#pragma once
#include "vm.hpp"

struct RegisterAllocator
{
	int next_free = 0;

	// Allocate a single register
	int
	allocate()
	{
		if (next_free >= REGISTERS)
		{
			printf("Error: Out of registers\n");
			exit(1);
		}
		return next_free++;
	}

	// Allocate a contiguous range of registers
	int
	allocate_range(int count)
	{
		if (next_free + count > REGISTERS)
		{
			printf("Error: Cannot allocate %d registers (only %d available)\n", count, REGISTERS - next_free);
			exit(1);
		}
		int first = next_free;
		next_free += count;
		return first;
	}

	// Reset allocator for new program
	void
	clear()
	{
		next_free = 0;
	}
};

struct ProgramBuilder
{
	array<VMInstruction, QueryArena> instructions;

	string_map<uint32_t> labels;
	RegisterAllocator	 regs;

	// Fluent interface
	ProgramBuilder &
	emit(VMInstruction inst)
	{
		// instructions.push_back(inst);
		array_push(&instructions, inst);
		return *this;
	}

	ProgramBuilder &
	label(const char *name)
	{
		stringmap_insert(&labels, name, instructions.size);
		return *this;
	}

	int
	here()
	{
		return instructions.size;
	}

	void
	resolve_labels()
	{
		for (size_t i = 0; i < instructions.size; i++)
		{
			auto &inst = instructions.data[i];

			char *label = (char *)inst.p4;
			if (nullptr == label)
			{
				continue;
			}

			auto entry = stringmap_get(&labels, label);
			if (entry == nullptr)
			{
				continue;
			}
			if (inst.p2 == -1)
			{
				inst.p2 = *entry;
			}
			if (inst.p3 == -1)
			{
				inst.p3 = *entry;
			}
			inst.p4 = nullptr;
		}
	}
};

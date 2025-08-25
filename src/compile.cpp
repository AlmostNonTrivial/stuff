// programbuilder.cpp - Simplified version with full table scans only
#include "compile.hpp"
#include "arena.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include "parser.hpp"
#include "schema.hpp"
#include "vm.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>

// Register allocator with named registers for debugging
struct RegisterAllocator {
	Vec<std::pair<Str<QueryArena>, uint32_t>, QueryArena, REGISTERS>
	    name_to_register;

	// we need array allocation to be contiguous, so for simplicity just
	// increment them
	int
	get(const char *name)
	{
		auto it = name_to_register.find_with(
		    [name](const std::pair<Str<QueryArena>, uint32_t> entry) {
			    return entry.first.starts_with(name);
		    });

		if (it != -1) {
			return name_to_register[it].second;
		}

		std::cout << "out of registers\n";
		exit(1);
	}

	void
	free(const char *name)
	{
	}

	void
	clear()
	{
		name_to_register.clear();
	}
};

struct ProgramBuilder {
	Vec<VMInstruction, QueryArena> instructions;
	Vec<std::pair<Str<QueryArena>, int>, QueryArena> labels;
	RegisterAllocator regs;

	// Fluent interface
	ProgramBuilder &
	emit(VMInstruction inst)
	{
		instructions.push_back(inst);
		return *this;
	}

	ProgramBuilder &
	label(const char *name)
	{
		labels.push_back(std::make_pair(name, instructions.size()));
		return *this;
	}

	int
	here()
	{
		return instructions.size();
	}

	void
	resolve_labels()
	{
		for (size_t i = 0; i < instructions.size(); i++) {
			auto &inst = instructions[i];

			if (!inst.p4 && inst.p3 == -1) {
				continue;
			}
			char *label = (char *)inst.p4;

			auto it = labels.find_with(
			    [label](const std::pair<Str<QueryArena>, uint32_t>
					entry) {
				    return entry.first.starts_with(label);
			    });

			if (it == -1) {
				continue;
			}
			inst.p3 = it;
			inst.p4 = nullptr;
		}
	}
};


void
init_stuff()
{


	pager_init("stuff");
}



void
compile()
{
	init_stuff();
	// vm_execute(VMInstruction * instructions, ctx)
}

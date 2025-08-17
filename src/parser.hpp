#pragma once

#include "vm.hpp"
#include <vector>


std::vector<VMInstruction> parse_sql(const char * sql);

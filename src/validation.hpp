#pragma once
#include "parser.hpp"
#include "catalog.hpp"
#include "arena.hpp"

// Validation errors collected during semantic analysis
struct ValidationError
{
	const char *message;
	const char *context; // table/column name for context
	uint32_t	line;
	uint32_t	column;
};

struct ValidationResult
{
	array<ValidationError, QueryArena> errors;
	bool							   valid;
};

// Per-node validation functions
ValidationResult
validate_statement(Statement *stmt);

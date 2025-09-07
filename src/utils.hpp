
#include "types.hpp"
#include "vm.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
// Simple CSV parser
struct CSVReader
{
	std::ifstream file;
	std::string	  line;

	CSVReader(const char *filename) : file(filename)
	{
		if (!file.is_open())
		{
			fprintf(stderr, "Failed to open CSV file: %s\n", filename);
		}
		// Skip header
		std::getline(file, line);
	}

	bool
	next_row(std::vector<std::string> &fields)
	{
		if (!std::getline(file, line))
		{
			return false;
		}

		// Remove carriage return if present
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}

		fields.clear();
		std::stringstream ss(line);
		std::string		  field;

		while (std::getline(ss, field, ','))
		{
			fields.push_back(field);
		}
		return true;
	}
};


// ============================================================================
// Queue-based Validation System
// ============================================================================

struct ExpectedRow
{
	array<TypedValue, query_arena> values;
};
static array<ExpectedRow, query_arena> expected_queue;
static uint32_t						   validation_failures = 0;
static uint32_t						   validation_row_count = 0;
static bool							   validation_active = false;
static bool							   same_count = false;

inline void
print_result_callback(TypedValue *result, size_t count)
{
	for (int i = 0; i < count; i++)
	{
		result[i].print();
		if (i != count - 1)
		{
			std::cout << ", ";
		}
	}
	std::cout << "\n";
}
// Validation callback
inline void
validation_callback(TypedValue *result, size_t count)
{
	validation_row_count++;

	if (expected_queue.size == 0)
	{
		if (!same_count)
		{
		        return;
		}

		printf("❌ Row %u: Unexpected row (no more expected)\n", validation_row_count);
		printf("   Got: ");
		// print_result_callback(result, count);
		validation_failures++;
		return;
	}

	// Pop from front
	ExpectedRow &expected = expected_queue.data[0];

	// Validate column count
	if (expected.values.size != count)
	{
		printf("❌ Row %u: Column count mismatch (expected %u, got %zu)\n", validation_row_count, expected.values.size,
			   count);
		validation_failures++;
	}
	else
	{
		// Validate each column
		bool row_matches = true;
		for (size_t i = 0; i < count; i++)
		{
			if (expected.values.data[i].type != result[i].type ||
				type_compare(result[i].type, result[i].data, expected.values.data[i].data) != 0)
			{

				if (row_matches)
				{ // First mismatch in this row
					printf("❌ Row %u: Value mismatch\n", validation_row_count);
					row_matches = false;
				}

				printf("   Column %zu: expected ", i);
				type_print(expected.values.data[i].type, expected.values.data[i].data);
				printf(" (%s), got ", type_name(expected.values.data[i].type));
				type_print(result[i].type, result[i].data);
				printf(" (%s)\n", type_name(result[i].type));
			}
		}

		if (!row_matches)
		{
			validation_failures++;
		}
	}

	// Remove from queue (shift array)
	for (uint32_t i = 1; i < expected_queue.size; i++)
	{
		expected_queue.data[i - 1] = expected_queue.data[i];
	}
	expected_queue.size--;
}

// Clear validation state
inline void
validation_reset()
{
	expected_queue.clear();
	validation_failures = 0;
	validation_row_count = 0;
	validation_active = false;
	same_count = false;
}

// Start validation mode
inline void
validation_begin(bool same_count)
{
	validation_reset();
	validation_active = true;

	vm_set_result_callback(validation_callback);
}

// End validation and report
inline bool
validation_end()
{
	validation_active = false;

	vm_set_result_callback(print_result_callback);

	bool success = (validation_failures == 0);

	if (expected_queue.size > 0)
	{
		printf("❌ %u expected rows were not emitted\n", expected_queue.size);
		for (uint32_t i = 0; i < expected_queue.size; i++)
		{
			printf("   Missing row %u: ", validation_row_count + i + 1);
			for (uint32_t j = 0; j < expected_queue.data[i].values.size; j++)
			{
				if (j > 0)
					printf(", ");
				type_print(expected_queue.data[i].values.data[j].type, expected_queue.data[i].values.data[j].data);
			}
			printf("\n");
		}
		success = false;
	}

	if (success)
	{

	}
	else
	{
		printf("❌ Validation failed: %u mismatches\n", validation_failures);
	}

	return success;
}

// Add expected row with TypedValues
inline void
expect_row_values(std::initializer_list<TypedValue> values)
{
	ExpectedRow row;
	row.values.reserve(values.size());

	for (const auto &val : values)
	{
		// Deep copy the value
		uint32_t size = type_size(val.type);
		uint8_t *data = (uint8_t *)arena::alloc<query_arena>(size);
		type_copy(val.type, data, val.data);
		row.values.push(TypedValue::make(val.type, data));
	}

	expected_queue.push(row);
}

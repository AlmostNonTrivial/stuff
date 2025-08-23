// vm.cpp
#include "vm.hpp"
#include "blob.hpp"
#include "arena.hpp"
#include "blob.hpp"
#include "btree.hpp"
#include "defs.hpp"
#include "memtree.hpp"
#include "schema.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ios>
bool _debug = false;

// ============================================================================
// VmCursor - Unified cursor abstraction
// ============================================================================
struct VmCursor {
	// Cursor type explicitly enumerated
	enum Type {
		TABLE,	   // Primary table cursor
		INDEX,	   // Secondary index cursor
		EPHEMERAL, // Memory-only temporary cursor
		BLOB
	};
	Type type;
	RecordLayout layout; // Value type - no pointer needed!
	// Storage backends (union since only one is active)
	union {
		BtCursor btree;
		MemCursor mem;
		BlobCursor blob;
	} cursor;
	// Storage trees
	union {
		BTree *btree_ptr; // For TABLE/INDEX
		MemTree mem_tree; // For EPHEMERAL (owned by cursor)
	} storage;
	// ========================================================================
	// Helper functions
	// ========================================================================
	uint32_t
	record_size() const
	{
		return layout.record_size;
	}
	// ========================================================================
	// Initialization
	// ========================================================================
	void
	open_table(const RecordLayout &table_layout, BTree *tree)
	{
		type = TABLE;
		memcpy(&layout, &table_layout, sizeof(RecordLayout));
		storage.btree_ptr = tree;
		cursor.btree.tree = storage.btree_ptr;
		cursor.btree.state = CURSOR_INVALID;
	}
	void
	open_index(const RecordLayout &index_layout, BTree *tree)
	{
		type = INDEX;
		memcpy(&layout, &index_layout, sizeof(RecordLayout));
		storage.btree_ptr = tree;
		cursor.btree.tree = storage.btree_ptr;
		cursor.btree.state = CURSOR_INVALID;
	}
	void
	open_ephemeral(const RecordLayout &ephemeral_layout)
	{
		type = EPHEMERAL;
		memcpy(&layout, &ephemeral_layout, sizeof(RecordLayout));
		storage.mem_tree =
		    memtree_create(layout.key_type(), layout.record_size);
		// cursor.mem.ctx = e
		cursor.mem.tree = &storage.mem_tree;
		cursor.mem.state = MemCursor::INVALID;
	}
	void
	open_blob(MemoryContext *ctx)
	{
		type = BLOB;
		cursor.blob.ctx = ctx;
	}
	// ========================================================================
	// Unified Navigation
	// ========================================================================
	bool
	rewind(bool to_end = false)
	{
		switch (type) {
		case EPHEMERAL:
			return to_end ? memcursor_last(&cursor.mem)
				      : memcursor_first(&cursor.mem);
		case TABLE:
		case INDEX:
			return to_end ? btree_cursor_last(&cursor.btree)
				      : btree_cursor_first(&cursor.btree);
						case BLOB:
										default:
			return false;
		}
	}
	bool
	step(bool forward = true)
	{
		switch (type) {
		case EPHEMERAL:
			return forward ? memcursor_next(&cursor.mem)
				       : memcursor_previous(&cursor.mem);
		case TABLE:
		case INDEX:
			return forward ? btree_cursor_next(&cursor.btree)
				       : btree_cursor_previous(&cursor.btree);

		case BLOB:
		default:
			return false;
		}
	}
	bool
	seek(CompareOp op, uint8_t *key)
	{
		switch (type) {
		case EPHEMERAL:
			return memcursor_seek_cmp(&cursor.mem, key, op);
		case TABLE:
		case INDEX:
			return btree_cursor_seek_cmp(&cursor.btree, key, op);
		case BLOB:
			return blob_cursor_seek(&cursor.blob, key);
		default:
			return false;
		}
	}

	bool
	seek_exact(uint8_t *key, const uint8_t *record)
	{
		switch (type) {
		case EPHEMERAL:
			return memcursor_seek_exact(&cursor.mem, key, record);
		case TABLE:
		case INDEX:
			return btree_cursor_seek_exact(&cursor.btree, key,
						       record);
		case BLOB:
		default:
			return false;
		}
	}

	bool
	is_valid()
	{
		switch (type) {
		case EPHEMERAL:
			return memcursor_is_valid(&cursor.mem);
		case TABLE:
		case INDEX:
			return btree_cursor_is_valid(&cursor.btree);
		case BLOB:
		    return blob_cursor_is_valid(&cursor.blob);
		default:
			return false;
		}
	}
	// ========================================================================
	// Data Access
	// ========================================================================
	uint8_t *
	get_key()
	{
		switch (type) {
		case EPHEMERAL:
			return memcursor_key(&cursor.mem);
		case TABLE:
		case INDEX:
			return btree_cursor_key(&cursor.btree);
		case BLOB:
		default:

			return nullptr;
		}
	}
	uint8_t *
	get_record()
	{
		switch (type) {
		case EPHEMERAL:
			return memcursor_record(&cursor.mem);
		case TABLE:
		case INDEX:
			return btree_cursor_record(&cursor.btree);
		case BLOB:
		    return blob_cursor_record(&cursor.blob);
		default:
			return nullptr;
		}
	}
	uint8_t *
	column(uint32_t col_index)
	{
		// Bounds check
		if (col_index >= layout.column_count()) {
			return nullptr;
		}
		// Column 0 is always the key
		if (col_index == 0) {
			return get_key();
		}
		// For other columns, get the record
		uint8_t *record = get_record();
		if (!record)
			return nullptr;
		// Special case: index cursors
		if (type == INDEX) {
			// Index record is just the rowid (column 1)
			return (col_index == 1) ? record : nullptr;
		}
		// Regular table/ephemeral: use pre-calculated offsets
		return record + layout.get_offset(col_index);
	}
	DataType
	column_type(uint32_t col_index)
	{
		if (col_index >= layout.column_count()) {
			return TYPE_NULL;
		}
		return layout.layout[col_index];
	}
	// ========================================================================
	// Modification Operations
	// ========================================================================
	bool
	insert(uint8_t *key, uint8_t *record, uint32_t size = 0)
	{
		switch (type) {
		case EPHEMERAL:
			return memcursor_insert(&cursor.mem, key, record);
		case TABLE:
		case INDEX:
			return btree_cursor_insert(&cursor.btree, key, record);
		case BLOB:
			return blob_cursor_insert(&cursor.blob, key, record, size);
		default:
			return false;
		}
	}
	bool
	update(uint8_t *record)
	{
		switch (type) {
		case EPHEMERAL:
			return memcursor_update(&cursor.mem, record);
		case TABLE:
		case INDEX:
			return btree_cursor_update(&cursor.btree, record);
		case BLOB:
		default:
			return false;
		}
	}
	bool
	remove()
	{
		switch (type) {
		case EPHEMERAL:
			return memcursor_delete(&cursor.mem);
		case TABLE:
		case INDEX:
			return btree_cursor_delete(&cursor.btree);
		case BLOB:
		    return blob_cursor_delete(&cursor.blob);
		default:
			return false;
		}
	}
	const char *
	type_name()
	{
		switch (type) {
		case EPHEMERAL:
			return "MEMTREE";
		case INDEX:
			return "INDEX";
		case TABLE:
			return "TABLE";
		case BLOB:
		    return "BLOB";
		}
	}
	// Debug helper
	void
	print_current()
	{
		printf("Cursor type=%s, valid=%d", type_name(), is_valid());
		if (is_valid()) {
			uint8_t *key = get_key();
			if (key) {
				printf(", key=");
				print_value(layout.key_type(), key);
			}
		}
		printf("\n");
	}
};
// ============================================================================
// VM State
// ============================================================================
static struct {
	MemoryContext *ctx;
	VMInstruction *program;
	int program_size;
	uint32_t pc;
	bool halted;
	VMValue registers[REGISTERS];
	VmCursor cursors[CURSORS];
} VM = {};
// ============================================================================
// Helper Functions
// ============================================================================
static void
set_register(VMValue *dest, VMValue *src)
{
	dest->type = src->type;
	memcpy(dest->data, src->data, (uint32_t)src->type);
}
static void
set_register(VMValue *dest, uint8_t *data, DataType type)
{
	dest->type = type;
	if (data) {
		memcpy(dest->data, data, type);
	} else {
		memset(dest->data, 0, type);
	}
}
static void
build_record(uint8_t *data, int32_t first_reg, int32_t count)
{
	int32_t offset = 0;
	for (int i = 0; i < count; i++) {
		VMValue *val = &VM.registers[first_reg + i];
		uint32_t size = val->type;
		memcpy(data + offset, val->data, size);
		offset += size;
	}
}
static void
reset()
{
	VM.pc = 0;
	VM.halted = false;
	for (uint32_t i = 0; i < REGISTERS; i++) {
		VM.registers[i].type = TYPE_NULL;
	}
	VM.program = nullptr;
	VM.program_size = 0;
	VM.ctx = nullptr;
}
// ============================================================================
// Debug Functions
// ============================================================================
void
vm_debug_print_all_registers()
{
	printf("===== REGISTERS =====\n");
	for (int i = 0; i < REGISTERS; i++) {
		if (VM.registers[i].type != TYPE_NULL) {
			Debug::print_register(i, VM.registers[i]);
		}
	}
	printf("====================\n");
}
void
vm_debug_print_cursor(int cursor_id)
{
	if (cursor_id >= 0 && cursor_id < CURSORS) {
		printf("===== CURSOR %d =====\n", cursor_id);
		VM.cursors[cursor_id].print_current();
		printf("====================\n");
	}
}
// ============================================================================
// Main Execution Step
// ============================================================================
static VM_RESULT
step()
{
	VMInstruction *inst = &VM.program[VM.pc];
	if (_debug) {
		printf("\n[%3d] %-12s ", VM.pc,
		       Debug::opcode_name(inst->opcode));
	}
	switch (inst->opcode) {
	case OP_Halt: {
		if (_debug) {
			printf("=> Halting with code %d",
			       Opcodes::Halt::exit_code(*inst));
		}
		VM.halted = true;
		return OK;
	}
	case OP_Goto: {
		int32_t target = Opcodes::Goto::target(*inst);
		if (_debug) {
			printf("=> Jumping to PC=%d", target);
		}
		VM.pc = target;
		return OK;
	}
	case OP_Load: {
		int32_t dest_reg = Opcodes::Move::dest_reg(*inst);
		uint8_t *data = Opcodes::Move::data(*inst);
		DataType type = Opcodes::Move::type(*inst);
		if (_debug) {
			printf("=> R[%d] = ", dest_reg);
			print_value(type, data);
			printf(" (type=%d)", type);
		}
		set_register(&VM.registers[dest_reg], data, type);
		VM.pc++;
		return OK;
	}
	case OP_Move: {
		int32_t dest_reg = Opcodes::Move::dest_reg(*inst);
		int32_t src_reg = Opcodes::Move::src_reg(*inst);
		auto *src = &VM.registers[src_reg];
		if (_debug) {
			printf("=> R[%d] = R[%d] = ", dest_reg, src_reg);
			print_value(src->type, src->data);
			printf(" (type=%d)", src->type);
		}
		set_register(&VM.registers[dest_reg], src);
		VM.pc++;
		return OK;
	}
	case OP_Test: {
		int32_t dest = Opcodes::Test::dest_reg(*inst);
		int32_t left = Opcodes::Test::left_reg(*inst);
		int32_t right = Opcodes::Test::right_reg(*inst);
		CompareOp op = Opcodes::Test::op(*inst);
		VMValue *a = &VM.registers[left];
		VMValue *b = &VM.registers[right];
		int cmp_result = cmp(a->type, a->data, b->data);
		VMValue result = {.type = TYPE_4};
		bool test_result = false;
		switch (op) {
		case EQ:
			test_result = (cmp_result == 0);
			break;
		case NE:
			test_result = (cmp_result != 0);
			break;
		case LT:
			test_result = (cmp_result < 0);
			break;
		case LE:
			test_result = (cmp_result <= 0);
			break;
		case GT:
			test_result = (cmp_result > 0);
			break;
		}
		*(uint32_t *)result.data = test_result ? 1 : 0;
		if (_debug) {
			const char *op_names[] = {"==", "!=", "<",
						  "<=", ">",  ">="};
			printf("=> R[%d] = (", dest);
			print_value(a->type, a->data);
			printf(" %s ", op_names[op]);
			print_value(b->type, b->data);
			printf(") = %s", test_result ? "TRUE" : "FALSE");
		}
		set_register(&VM.registers[dest], &result);
		VM.pc++;
		return OK;
	}
		// One opcode, multiple pattern types
	case OP_Pattern: {
		int32_t str_reg = Opcodes::Pattern::str_reg(*inst);
		int32_t pattern_reg = Opcodes::Pattern::pattern_reg(*inst);
		int32_t result_reg = Opcodes::Pattern::result_reg(*inst);
		PatternType type = Opcodes::Pattern::pattern_type(*inst);

		VMValue *str = &VM.registers[str_reg];
		VMValue *pattern = &VM.registers[pattern_reg];

		bool matches = false;
		switch (type) {
		case PATTERN_LIKE:
			// matches = evaluate_like_pattern(str->data,
			// pattern->data, str->type, pattern->type);
			break;
		case PATTERN_GLOB:
			// matches = evaluate_glob_pattern(str->data,
			// pattern->data,
			//                                str->type,
			//                                pattern->type);
			break;
		case PATTERN_CONTAINS:
			// Simple substring search
			matches = (strstr((char *)str->data,
					  (char *)pattern->data) != nullptr);
			break;
		case PATTERN_REGEXP:
			// Could add later, or just say "not implemented"
			printf("REGEXP not implemented\n");
			return ERR;
		}

		VM.registers[result_reg].type = TYPE_4;
		*(uint32_t *)VM.registers[result_reg].data = matches ? 1 : 0;

		VM.pc++;
		return OK;
	}
	case OP_JumpIf: {
		int32_t test_reg = Opcodes::JumpIf::test_reg(*inst);
		int32_t target = Opcodes::JumpIf::jump_target(*inst);
		bool jump_on_true = Opcodes::JumpIf::jump_on_true(*inst);
		VMValue *val = &VM.registers[test_reg];
		bool is_true = (*(uint32_t *)val->data != 0);
		bool will_jump =
		    (is_true && jump_on_true) || (!is_true && !jump_on_true);
		if (_debug) {
			printf("=> R[%d]=", test_reg);
			print_value(val->type, val->data);
			printf(" (%s), jump_on_%s => %s to PC=%d",
			       is_true ? "TRUE" : "FALSE",
			       jump_on_true ? "true" : "false",
			       will_jump ? "JUMPING" : "CONTINUE",
			       will_jump ? target : VM.pc + 1);
		}
		if (will_jump) {
			VM.pc = target;
		} else {
			VM.pc++;
		}
		return OK;
	}
	case OP_Logic: {
		int32_t dest = Opcodes::Logic::dest_reg(*inst);
		int32_t left = Opcodes::Logic::left_reg(*inst);
		int32_t right = Opcodes::Logic::right_reg(*inst);
		LogicOp op = Opcodes::Logic::op(*inst);
		VMValue result{.type = TYPE_4};
		uint32_t a = *(uint32_t *)VM.registers[left].data;
		uint32_t b = *(uint32_t *)VM.registers[right].data;
		uint32_t res_val;
		switch (op) {
		case LOGIC_AND:
			res_val = (a && b) ? 1 : 0;
			break;
		case LOGIC_OR:
			res_val = (a || b) ? 1 : 0;
			break;
		default:
			return ERR;
		}
		*(uint32_t *)result.data = res_val;
		if (_debug) {
			const char *op_names[] = {"AND", "OR"};
			printf("=> R[%d] = %d %s %d = %d", dest, a,
			       op_names[op], b, res_val);
		}
		set_register(&VM.registers[dest], &result);
		VM.pc++;
		return OK;
	}
	case OP_Result: {
		int32_t first_reg = Opcodes::Result::first_reg(*inst);
		int32_t reg_count = Opcodes::Result::reg_count(*inst);

		if (VM.ctx && VM.ctx->emit_row) {
			// Allocate output array using the context's allocator
			TypedValue *values = (TypedValue *)VM.ctx->alloc(
			    sizeof(TypedValue) * reg_count);

			for (int i = 0; i < reg_count; i++) {
				VMValue *val = &VM.registers[first_reg + i];
				values[i].type = val->type;
				values[i].data =
				    (uint8_t *)VM.ctx->alloc(val->type);
				memcpy(values[i].data, val->data, val->type);
			}

			VM.ctx->emit_row(values, reg_count);
		}
		VM.pc++;
		return OK;
	}
	case OP_Arithmetic: {
		int32_t dest = Opcodes::Arithmetic::dest_reg(*inst);
		int32_t left = Opcodes::Arithmetic::left_reg(*inst);
		int32_t right = Opcodes::Arithmetic::right_reg(*inst);
		ArithOp op = Opcodes::Arithmetic::op(*inst);
		VMValue *a = &VM.registers[left];
		VMValue *b = &VM.registers[right];
		VMValue result = {.type =
				      (a->type > b->type) ? a->type : b->type};
		bool success = do_arithmetic(op, result.type, result.data,
					     a->data, b->data);
		if (_debug) {
			const char *op_names[] = {"+", "-", "*", "/", "%"};
			printf("=> R[%d] = ", dest);
			print_value(a->type, a->data);
			printf(" %s ", op_names[op]);
			print_value(b->type, b->data);
			printf(" = ");
			if (success) {
				print_value(result.type, result.data);
			} else {
				printf("ERROR");
			}
		}
		if (!success) {
			return ERR; // Division by zero
		}
		set_register(&VM.registers[dest], &result);
		VM.pc++;
		return OK;
	}
	case OP_Open: {
		int32_t cursor_id = Opcodes::Open::cursor_id(*inst);
		bool is_ephemeral = Opcodes::Open::is_ephemeral(*inst);
		VmCursor &cursor = VM.cursors[cursor_id];
		if (is_ephemeral) {
			RecordLayout *layout =
			    Opcodes::Open::ephemeral_schema(*inst);
			cursor.open_ephemeral(*layout);
			if (_debug) {
				printf("=> Opened ephemeral cursor %d",
				       cursor_id);
			}
		} else {
			const char *table_name =
			    Opcodes::Open::table_name(*inst);
			int32_t index_column = Opcodes::Open::index_col(*inst);
			if (index_column != 0) {
				Index *index =
				    get_index(table_name, index_column);
				cursor.open_index(index->to_layout(),
						  &index->tree);
				if (_debug) {
					printf("=> Opened index cursor %d on "
					       "%s.%d",
					       cursor_id, table_name,
					       index_column);
				}
			} else {
				Table *table = get_table(table_name);
				cursor.open_table(table->to_layout(),
						  &table->tree);
				if (_debug) {
					printf(
					    "=> Opened table cursor %d on %s",
					    cursor_id, table_name);
				}
			}
		}
		VM.pc++;
		return OK;
	}
	case OP_Close: {
		int32_t cursor_id = Opcodes::Close::cursor_id(*inst);
		if (_debug) {
			printf("=> Closed cursor %d", cursor_id);
		}
		// if(VM.cursors[cursor_id] == 0)
		// Note: cursor destructor handles cleanup
		VM.pc++;
		return OK;
	}
	case OP_Rewind: {
		int32_t cursor_id = Opcodes::Rewind::cursor_id(*inst);
		int32_t jump_if_empty = Opcodes::Rewind::jump_if_empty(*inst);
		bool to_end = Opcodes::Rewind::to_end(*inst);
		VmCursor *cursor = &VM.cursors[cursor_id];
		bool valid = cursor->rewind(to_end);
		if (_debug) {
			printf("=> Cursor %d rewound to %s, valid=%d",
			       cursor_id, to_end ? "end" : "start", valid);
			if (!valid && jump_if_empty >= 0) {
				printf(", jumping to PC=%d", jump_if_empty);
			}
		}
		if (!valid && jump_if_empty >= 0) {
			VM.pc = jump_if_empty;
		} else {
			VM.pc++;
		}
		return OK;
	}
	case OP_Step: {
		int32_t cursor_id = Opcodes::Step::cursor_id(*inst);
		int32_t jump_if_done = Opcodes::Step::jump_if_done(*inst);
		bool forward = Opcodes::Step::forward(*inst);
		VmCursor *cursor = &VM.cursors[cursor_id];
		bool has_more = cursor->step(forward);
		if (_debug) {
			printf("=> Cursor %d stepped %s, has_more=%d",
			       cursor_id, forward ? "forward" : "backward",
			       has_more);
			if (!has_more && jump_if_done >= 0) {
				printf(", jumping to PC=%d", jump_if_done);
			}
		}
		if (!has_more && jump_if_done >= 0) {
			VM.pc = jump_if_done;
		} else {
			VM.pc++;
		}
		return OK;
	}
	case OP_Seek: {
		int32_t cursor_id = Opcodes::Seek::cursor_id(*inst);
		int32_t key_reg = Opcodes::Seek::key_reg(*inst);
		int32_t jump_if_not = Opcodes::Seek::jump_if_not(*inst);
		CompareOp op = Opcodes::Seek::op(*inst);
		VmCursor *cursor = &VM.cursors[cursor_id];
		VMValue *key = &VM.registers[key_reg];
		bool found;

		/* Is there a better way to do this?? */
		if (op == EXACT) {
			VMValue *record = &VM.registers[key_reg + 1];
			found = cursor->seek_exact(key->data, record->data);
		} else {
			found = cursor->seek(op, key->data);
		}

		if (_debug) {
			const char *op_names[] = {"EQ", "NE", "LT",
						  "LE", "GT", "GE"};
			printf("=> Cursor %d seek %s with key=", cursor_id,
			       op_names[op]);
			print_value(key->type, key->data);
			printf(", found=%d", found);
			if (!found && jump_if_not >= 0) {
				printf(", jumping to PC=%d", jump_if_not);
			}
		}
		if (!found && jump_if_not >= 0) {
			VM.pc = jump_if_not;
		} else {
			VM.pc++;
		}
		return OK;
	}
	case OP_Column: {
		int32_t cursor_id = Opcodes::Column::cursor_id(*inst);
		int32_t col_index = Opcodes::Column::column_index(*inst);
		int32_t dest_reg = Opcodes::Column::dest_reg(*inst);
		VmCursor *cursor = &VM.cursors[cursor_id];
		uint8_t *data = cursor->column(col_index);
		DataType type = cursor->column_type(col_index);
		if (_debug) {
			printf("=> R[%d] = cursor[%d].col[%d] = ", dest_reg,
			       cursor_id, col_index);
			if (data) {
				print_value(type, data);
			} else {
				printf("NULL");
			}
			printf(" (type=%d)", type);
		}
		set_register(&VM.registers[dest_reg], data, type);
		VM.pc++;
		return OK;
	}
	case OP_Delete: {
		int32_t cursor_id = Opcodes::Delete::cursor_id(*inst);
		VmCursor *cursor = &VM.cursors[cursor_id];
		bool success = cursor->remove();
		if (_debug) {
			printf("=> Cursor %d delete %s", cursor_id,
			       success ? "OK" : "FAILED");
		}
		VM.pc++;
		return OK;
	}
	case OP_Insert: {
		int32_t cursor_id = Opcodes::Insert::cursor_id(*inst);
		int32_t key_reg = Opcodes::Insert::key_reg(*inst);
		VmCursor *cursor = &VM.cursors[cursor_id];
		VMValue *key = &VM.registers[key_reg];

		bool success;
		int32_t count;
		if (Opcodes::Insert::is_variable_length(*inst)) {
			uint32_t size = Opcodes::Insert::size(*inst);
			success = cursor->insert(key->data, key->data, size);
			count = 2;
		} else {
			count = Opcodes::Insert::reg_count(*inst);
			uint8_t data[cursor->record_size()];
			build_record(data, key_reg + 1, count - 1);
			success = cursor->insert(key->data, data);
		}

		if (_debug) {
			printf("=> Cursor %d insert key=", cursor_id);
			print_value(key->type, key->data);
			printf(" with %d values, success=%d", count - 1,
			       success);
		}
		if (!success) {
			return ERR;
		}
		VM.pc++;
		return OK;
	}
	case OP_Update: {
		int32_t cursor_id = Opcodes::Update::cursor_id(*inst);
		int32_t record_reg = Opcodes::Update::record_reg(*inst);
		VmCursor *cursor = &VM.cursors[cursor_id];
		uint8_t data[cursor->record_size()];
		build_record(data, record_reg,
			     cursor->layout.column_count() - 1);
		bool success = cursor->update(data);
		if (_debug) {
			printf("=> Cursor %d update with data from R[%d], "
			       "success=%d",
			       cursor_id, record_reg, success);
		}
		VM.pc++;
		return OK;
	}
	default:
		printf("Unknown opcode: %d\n", inst->opcode);
		return ERR;
	}
}
// ============================================================================
// Main VM Execute Function
// ============================================================================

VM_RESULT
vm_execute(VMInstruction *instructions, int instruction_count,
	   MemoryContext *ctx)
{
	reset();
	VM.program = instructions;
	VM.program_size = instruction_count;
	VM.ctx = ctx;
	if (_debug) {
		Debug::print_program(instructions, instruction_count);
		printf("\n===== EXECUTION TRACE =====\n");
	}
	while (!VM.halted && VM.pc < VM.program_size) {
		VM_RESULT result = step();
		if (result != OK) {
			if (_debug) {
				printf("\n\nVM execution failed at PC=%d\n",
				       VM.pc);
				vm_debug_print_all_registers();
			}
			return result;
		}
	}
	if (_debug) {
		printf("\n\n===== EXECUTION COMPLETED =====\n");
		vm_debug_print_all_registers();
	}
	return OK;
}

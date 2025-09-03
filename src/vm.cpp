// vm.cpp
#include "vm.hpp"
#include "pager.hpp"
#include "types.hpp"
#include "blob.hpp"
#include "arena.hpp"
#include "blob.hpp"
#include "bplustree.hpp"
#include "memtree.hpp"
#include "catalog.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CURSORS	  10

bool _debug = false;
// POD struct - data only
struct VmCursor
{

	CursorType type;
	Layout	   layout;

	union {
		BPtCursor  bptree;
		MemCursor  mem;
		BlobCursor blob;
	} cursor;
};

// Initialization functions
void
vmcursor_open_bplus(VmCursor *cur, const Layout &table_layout, BPlusTree *tree)
{
	cur->type = CursorType::BPLUS;
	cur->layout = table_layout;
	cur->cursor.bptree.tree = tree;
	cur->cursor.bptree.state = BPT_CURSOR_INVALID;
}

void
vmcursor_open_red_black(VmCursor *cur, const Layout &ephemeral_layout, MemoryContext *ctx)
{
	cur->type = CursorType::RED_BLACK;
	cur->layout = ephemeral_layout;
	DataType key_type = cur->layout.layout[0];
	cur->cursor.mem.tree = memtree_create(key_type, cur->layout.record_size);
	cur->cursor.mem.state = MemCursor::INVALID;
	cur->cursor.mem.ctx = ctx;
}

void
vmcursor_open_blob(VmCursor *cur, MemoryContext *ctx)
{
	cur->type = CursorType::BLOB;
	cur->cursor.blob.ctx = ctx;
}

// Navigation functions
bool
vmcursor_rewind(VmCursor *cur, bool to_end)
{
	switch (cur->type)
	{
	case CursorType::RED_BLACK:
		return to_end ? memcursor_last(&cur->cursor.mem) : memcursor_first(&cur->cursor.mem);
	case CursorType::BPLUS:
		return to_end ? bplustree_cursor_last(&cur->cursor.bptree) : bplustree_cursor_first(&cur->cursor.bptree);
	case CursorType::BLOB:
	default:
		return false;
	}
}

bool
vmcursor_step(VmCursor *cur, bool forward)
{
	switch (cur->type)
	{
	case CursorType::RED_BLACK:
		return forward ? memcursor_next(&cur->cursor.mem) : memcursor_previous(&cur->cursor.mem);
	case CursorType::BPLUS:
		return forward ? bplustree_cursor_next(&cur->cursor.bptree) : bplustree_cursor_previous(&cur->cursor.bptree);
	case CursorType::BLOB:
	default:
		return false;
	}
}

void
vmcursor_clear(VmCursor *cursor)
{
	switch (cursor->type)
	{
	case CursorType::BPLUS: {

		bplustree_clear(cursor->cursor.bptree.tree);
		break;
	}
	case CursorType::BLOB: {
		blob_cursor_delete(&cursor->cursor.blob);
		break;
	}
	}
}

bool
vmcursor_seek(VmCursor *cur, CompareOp op, uint8_t *key)
{
	switch (cur->type)
	{
	case CursorType::RED_BLACK:
		return memcursor_seek_cmp(&cur->cursor.mem, key, op);

	case CursorType::BPLUS:
		return bplustree_cursor_seek_cmp(&cur->cursor.bptree, key, op);
	case CursorType::BLOB:
		return blob_cursor_seek(&cur->cursor.blob, *(uint32_t *)key);
	default:
		return false;
	}
}

bool
vmcursor_is_valid(VmCursor *cur)
{
	switch (cur->type)
	{
	case CursorType::RED_BLACK:
		return memcursor_is_valid(&cur->cursor.mem);

	case CursorType::BPLUS:
		return bplustree_cursor_is_valid(&cur->cursor.bptree);
	case CursorType::BLOB:
	default:
		return false;
	}
}

// Data access functions
uint8_t *
vmcursor_get_key(VmCursor *cur)
{
	switch (cur->type)
	{
	case CursorType::RED_BLACK:
		return memcursor_key(&cur->cursor.mem);
	case CursorType::BPLUS:
		return bplustree_cursor_key(&cur->cursor.bptree);
	case CursorType::BLOB:
	default:
		return nullptr;
	}
}

uint8_t *
vmcursor_get_record(VmCursor *cur)
{
	switch (cur->type)
	{
	case CursorType::RED_BLACK:
		return memcursor_record(&cur->cursor.mem);
	case CursorType::BPLUS:
		return bplustree_cursor_record(&cur->cursor.bptree);
	case CursorType::BLOB:
		return (uint8_t *)blob_cursor_record(&cur->cursor.blob).ptr;
	default:
		return nullptr;
	}
}

uint8_t *
vmcursor_column(VmCursor *cur, uint32_t col_index)
{
	if (col_index == 0)
	{
		return vmcursor_get_key(cur);
	}
	uint8_t *record = vmcursor_get_record(cur);
	return record + cur->layout.offsets[col_index - 1];
}

DataType
vmcursor_column_type(VmCursor *cur, uint32_t col_index)
{
	return cur->layout.layout[col_index];
}

// Modification functions
bool
vmcursor_insert(VmCursor *cur, uint8_t *key, uint8_t *record, uint32_t size)
{
	switch (cur->type)
	{
	case CursorType::RED_BLACK:
		return memcursor_insert(&cur->cursor.mem, key, record);
	case CursorType::BPLUS:
		return bplustree_cursor_insert(&cur->cursor.bptree, key, record);
	case CursorType::BLOB:
		return blob_cursor_insert(&cur->cursor.blob, record, size);
	default:
		return false;
	}
}

bool
vmcursor_update(VmCursor *cur, uint8_t *record)
{
	switch (cur->type)
	{
	case CursorType::RED_BLACK:
		return memcursor_update(&cur->cursor.mem, record);
	case CursorType::BPLUS:
		return bplustree_cursor_update(&cur->cursor.bptree, record);
	case CursorType::BLOB:
	default:
		return false;
	}
}

bool
vmcursor_remove(VmCursor *cur)
{
	switch (cur->type)
	{
	case CursorType::RED_BLACK:
		return memcursor_delete(&cur->cursor.mem);
	case CursorType::BPLUS:
		return bplustree_cursor_delete(&cur->cursor.bptree);
	case CursorType::BLOB:
		return blob_cursor_delete(&cur->cursor.blob);
	default:
		return false;
	}
}

// Utility functions
const char *
vmcursor_type_name(VmCursor *cur)
{
	switch (cur->type)
	{
	case CursorType::RED_BLACK:
		return "RED_BLACK";
	case CursorType::BPLUS:
		return "BPLUS";
	case CursorType::BLOB:
		return "BLOB";
	}
	return "UNKNOWN";
}

void
vmcursor_print_current(VmCursor *cur)
{
	printf("Cursor type=%s, valid=%d", vmcursor_type_name(cur), vmcursor_is_valid(cur));
	if (vmcursor_is_valid(cur))
	{
		uint8_t *key = vmcursor_get_key(cur);
		if (key)
		{
			printf(", key=");
			// print_value(cur->layout.layout.data[0], key);
		}
	}
	printf("\n");
}

// ============================================================================
// VM State
// ============================================================================
static struct
{
	MemoryContext *ctx;
	VMInstruction *program;
	int			   program_size;
	uint32_t	   pc;
	bool		   halted;
	TypedValue	   registers[REGISTERS];
	VmCursor	   cursors[CURSORS];
} VM = {};
// ============================================================================
// Helper Functions
// ============================================================================
static void
set_register(TypedValue *dest, uint8_t *src, DataType type)
{
	if (dest->get_size() < type_size(type))
	{
		VM.ctx->free(dest->data, type_size(dest->type));
		dest->data = (uint8_t *)VM.ctx->alloc(type_size(type));
	}
	else if (nullptr == dest->data)
	{
		dest->data = (uint8_t *)VM.ctx->alloc(type_size(type));
	}

	dest->type = type;

	type_copy(type, dest->data, src);
}
static void
set_register(TypedValue *dest, TypedValue *src)
{
	set_register(dest, src->data, src->type);
}

static void
build_record(uint8_t *data, int32_t first_reg, int32_t count)
{
	int32_t offset = 0;
	for (int i = 0; i < count; i++)
	{
		TypedValue *val = &VM.registers[first_reg + i];
		uint32_t	size = type_size(val->type);
		memcpy(data + offset, val->data, size);
		offset += size;
	}
}
static void
reset()
{
	VM.pc = 0;
	VM.halted = false;
	for (uint32_t i = 0; i < REGISTERS; i++)
	{
		VM.registers[i].type = TYPE_NULL;
		VM.registers[i].data = nullptr;
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
	for (int i = 0; i < REGISTERS; i++)
	{
		if (VM.registers[i].type != TYPE_NULL)
		{
		}
	}
	printf("====================\n");
}
void
vm_debug_print_cursor(int cursor_id)
{
	if (cursor_id >= 0 && cursor_id < CURSORS)
	{
		printf("===== CURSOR %d =====\n", cursor_id);
		vmcursor_print_current(&VM.cursors[cursor_id]);
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
	if (_debug)
	{
	}
	switch (inst->opcode)
	{
	case OP_Halt: {
		if (_debug)
		{
			printf("=> Halting with code %d", HALT_EXIT_CODE(*inst));
		}
		VM.halted = true;
		return OK;
	}
	case OP_Goto: {
		int32_t target = GOTO_TARGET(*inst);
		if (_debug)
		{
			printf("=> Jumping to PC=%d", target);
		}
		VM.pc = target;
		return OK;
	}
	case OP_Load: {
		int32_t	 dest_reg = LOAD_DEST_REG(*inst);
		uint8_t *data = LOAD_DATA(*inst);
		DataType type = LOAD_TYPE(*inst);

		auto to_load = TypedValue::make(type, data);
		if (_debug)
		{
			printf("=> R[%d] = ", dest_reg);
			// print_value(type, data);
			printf(" (type=%d)", type);
		}

		set_register(&VM.registers[dest_reg], &to_load);

		VM.pc++;
		return OK;
	}
	case OP_Move: {
		int32_t dest_reg = MOVE_DEST_REG(*inst);
		int32_t src_reg = MOVE_SRC_REG(*inst);
		auto   *src = &VM.registers[src_reg];
		if (_debug)
		{
			printf("=> R[%d] = R[%d] = ", dest_reg, src_reg);
			// print_value(src->type, src->data);
			printf(" (type=%d)", src->type);
		}
		set_register(&VM.registers[dest_reg], src);
		VM.pc++;
		return OK;
	}
	case OP_Test: {
		int32_t		dest = TEST_DEST_REG(*inst);
		int32_t		left = TEST_LEFT_REG(*inst);
		int32_t		right = TEST_RIGHT_REG(*inst);
		CompareOp	op = TEST_OP(*inst);
		TypedValue *a = &VM.registers[left];
		TypedValue *b = &VM.registers[right];
		int			cmp_result = type_compare(a->type, a->data, b->data);
		uint32_t	test_result = false;

		switch (op)
		{
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
		case GE:
			test_result = (cmp_result >= 0);
			break;
		}

		if (_debug)
		{
			const char *op_names[] = {"==", "!=", "<", "<=", ">", ">="};
			printf("=> R[%d] = (", dest);
			// print_value(a->type, a->data);
			printf(" %s ", op_names[op]);
			// print_value(b->type, b->data);
			printf(") = %s", test_result ? "TRUE" : "FALSE");
		}

		set_register(&VM.registers[dest], (uint8_t *)&test_result, TYPE_U32);
		VM.pc++;
		return OK;
	}
	// One opcode, multiple pattern types
	case OP_Function: {
		int32_t	   dest = FUNCTION_DEST_REG(*inst);
		int32_t	   first_arg = FUNCTION_FIRST_ARG_REG(*inst);
		int32_t	   count = FUNCTION_ARG_COUNT(*inst);
		VMFunction fn = FUNCTION_FUNCTION(*inst);

		// Pass registers directly as array
		bool success = fn(&VM.registers[dest], &VM.registers[first_arg], count, VM.ctx);

		if (!success)
		{
			return ERR;
		}

		VM.pc++;
		return OK;
	}
	case OP_JumpIf: {
		int32_t		test_reg = JUMPIF_TEST_REG(*inst);
		int32_t		target = JUMPIF_JUMP_TARGET(*inst);
		bool		jump_on_true = JUMPIF_JUMP_ON_TRUE(*inst);
		TypedValue *val = &VM.registers[test_reg];
		bool		is_true = (*(uint32_t *)val->data != 0);
		bool		will_jump = (is_true && jump_on_true) || (!is_true && !jump_on_true);
		if (_debug)
		{
			printf("=> R[%d]=", test_reg);
			// print_value(val->type, val->data);
			printf(" (%s), jump_on_%s => %s to PC=%d", is_true ? "TRUE" : "FALSE", jump_on_true ? "true" : "false",
				   will_jump ? "JUMPING" : "CONTINUE", will_jump ? target : VM.pc + 1);
		}
		if (will_jump)
		{
			VM.pc = target;
		}
		else
		{
			VM.pc++;
		}
		return OK;
	}
	case OP_Logic: {
		int32_t	   dest = LOGIC_DEST_REG(*inst);
		int32_t	   left = LOGIC_LEFT_REG(*inst);
		int32_t	   right = LOGIC_RIGHT_REG(*inst);
		LogicOp	   op = LOGIC_OP(*inst);
		TypedValue result = TypedValue::make(TYPE_U32);
		uint32_t   a = *(uint32_t *)VM.registers[left].data;
		uint32_t   b = *(uint32_t *)VM.registers[right].data;
		uint32_t   res_val;
		switch (op)
		{
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
		if (_debug)
		{
			const char *op_names[] = {"AND", "OR"};
			printf("=> R[%d] = %d %s %d = %d", dest, a, op_names[op], b, res_val);
		}
		set_register(&VM.registers[dest], &result);
		VM.pc++;
		return OK;
	}
	case OP_Result: {
		int32_t first_reg = RESULT_FIRST_REG(*inst);
		int32_t reg_count = RESULT_REG_COUNT(*inst);

		// Allocate output array using the context's allocator
		TypedValue *values = (TypedValue *)VM.ctx->alloc(sizeof(TypedValue) * reg_count);

		for (int i = 0; i < reg_count; i++)
		{
			TypedValue *val = &VM.registers[first_reg + i];
			values[i].type = val->type;
			values[i].data = (uint8_t *)VM.ctx->alloc(type_size(val->type));
			memcpy(values[i].data, val->data, type_size(val->type));
		}

		// not sure how this will work for blobs
		VM.ctx->emit_row(values, reg_count);

		VM.pc++;
		return OK;
	}
	case OP_Arithmetic: {
		int32_t		dest = ARITHMETIC_DEST_REG(*inst);
		int32_t		left = ARITHMETIC_LEFT_REG(*inst);
		int32_t		right = ARITHMETIC_RIGHT_REG(*inst);
		ArithOp		op = ARITHMETIC_OP(*inst);
		TypedValue *a = &VM.registers[left];
		TypedValue *b = &VM.registers[right];

		TypedValue result = {.type = (a->type > b->type) ? a->type : b->type};
		bool	   success = true;
		arithmetic(op, result.type, result.data, a->data, b->data);
		if (_debug)
		{
			const char *op_names[] = {"+", "-", "*", "/", "%"};
			printf("=> R[%d] = ", dest);
			// print_value(a->type, a->data);
			printf(" %s ", op_names[op]);
			// print_value(b->type, b->data);
			printf(" = ");
			if (success)
			{
				// print_value(result.type, result.data);
			}
			else
			{
				printf("ERROR");
			}
		}
		if (!success)
		{
			return ERR; // Division by zero
		}
		set_register(&VM.registers[dest], &result);
		VM.pc++;
		return OK;
	}
	case OP_Open: {
		int32_t		   cursor_id = OPEN_CURSOR_ID(*inst);
		VmCursor	  *cursor = &VM.cursors[cursor_id];
		CursorContext *context = OPEN_LAYOUT(*inst);

		switch (context->type)
		{
		case CursorType::BPLUS: {
			vmcursor_open_bplus(cursor, context->context.btree.layout, &context->context.btree.tree);
			break;
		}
		case CursorType::RED_BLACK: {
			vmcursor_open_red_black(cursor, context->context.layout, VM.ctx);
			break;
		}
		case CursorType::BLOB: {
			vmcursor_open_blob(cursor, VM.ctx);
			break;
		}
		}

		// open

		VM.pc++;
		return OK;
	}
	case OP_Close: {
		int32_t cursor_id = CLOSE_CURSOR_ID(*inst);
		if (_debug)
		{
			printf("=> Closed cursor %d", cursor_id);
		}
		// if(VM.cursors[cursor_id] == 0)

		VM.pc++;
		return OK;
	}
	case OP_Rewind: {
		int32_t	  cursor_id = REWIND_CURSOR_ID(*inst);
		int32_t	  jump_if_empty = REWIND_JUMP_IF_EMPTY(*inst);
		bool	  to_end = REWIND_TO_END(*inst);
		VmCursor *cursor = &VM.cursors[cursor_id];
		bool	  valid = vmcursor_rewind(cursor, to_end);
		if (_debug)
		{
			printf("=> Cursor %d rewound to %s, valid=%d", cursor_id, to_end ? "end" : "start", valid);
			if (!valid && jump_if_empty >= 0)
			{
				printf(", jumping to PC=%d", jump_if_empty);
			}
		}
		if (!valid && jump_if_empty >= 0)
		{
			VM.pc = jump_if_empty;
		}
		else
		{
			VM.pc++;
		}
		return OK;
	}
	case OP_Step: {
		int32_t	  cursor_id = STEP_CURSOR_ID(*inst);
		int32_t	  jump_if_done = STEP_JUMP_IF_DONE(*inst);
		bool	  forward = STEP_FORWARD(*inst);
		VmCursor *cursor = &VM.cursors[cursor_id];
		bool	  has_more = vmcursor_step(cursor, forward);
		if (_debug)
		{
			printf("=> Cursor %d stepped %s, has_more=%d", cursor_id, forward ? "forward" : "backward", has_more);
			if (!has_more && jump_if_done >= 0)
			{
				printf(", jumping to PC=%d", jump_if_done);
			}
		}
		if (!has_more && jump_if_done >= 0)
		{
			VM.pc = jump_if_done;
		}
		else
		{
			VM.pc++;
		}
		return OK;
	}
	case OP_Seek: {
		int32_t		cursor_id = SEEK_CURSOR_ID(*inst);
		int32_t		key_reg = SEEK_KEY_REG(*inst);
		int32_t		jump_if_not = SEEK_JUMP_IF_NOT(*inst);
		CompareOp	op = SEEK_OP(*inst);
		VmCursor   *cursor = &VM.cursors[cursor_id];
		TypedValue *key = &VM.registers[key_reg];
		bool		found;

		found = vmcursor_seek(cursor, op, key->data);

		if (_debug)
		{
			const char *op_names[] = {"EQ", "NE", "LT", "LE", "GT", "GE"};
			printf("=> Cursor %d seek %s with key=", cursor_id, op_names[op]);
			// print_value(key->type, key->data);
			printf(", found=%d", found);
			if (!found && jump_if_not >= 0)
			{
				printf(", jumping to PC=%d", jump_if_not);
			}
		}
		if (!found && jump_if_not >= 0)
		{
			VM.pc = jump_if_not;
		}
		else
		{
			VM.pc++;
		}
		return OK;
	}
	case OP_Column: {
		int32_t	  cursor_id = COLUMN_CURSOR_ID(*inst);
		int32_t	  col_index = COLUMN_INDEX(*inst);
		int32_t	  dest_reg = COLUMN_DEST_REG(*inst);
		VmCursor *cursor = &VM.cursors[cursor_id];

		TypedValue src = {
			.type = vmcursor_column_type(cursor, col_index),
			.data = vmcursor_column(cursor, col_index),
		};

		if (_debug)
		{
			printf("=> R[%d] = cursor[%d].col[%d] = ", dest_reg, cursor_id, col_index);
			if (src.data)
			{
				// print_value(src.type, src.data);
			}
			else
			{
				printf("NULL");
			}
			printf(" (type=%d)", src.type);
		}
		set_register(&VM.registers[dest_reg], &src);
		VM.pc++;
		return OK;
	}
	case OP_Delete: {
		int32_t	   cursor_id = DELETE_CURSOR_ID(*inst);
		int32_t	   delete_occured = DELETE_DELETE_OCCURED_REG(*inst);
		int32_t	   cursor_valid = DELETE_CURSOR_VALID_REG(*inst);
		VmCursor  *cursor = &VM.cursors[cursor_id];
		int32_t	   success = vmcursor_remove(cursor) ? 1 : 0;
		int32_t	   valid = vmcursor_is_valid(cursor) ? 1 : 0;
		TypedValue src_success = TypedValue::make(TYPE_U32, &success);
		TypedValue src_valid = TypedValue::make(TYPE_U32, &valid);

		set_register(&VM.registers[delete_occured], &src_success);
		set_register(&VM.registers[cursor_valid], &src_valid);

		if (_debug)
		{
			printf("=> Cursor %d delete %s", cursor_id, success ? "OK" : "FAILED");
		}
		VM.pc++;
		return OK;
	}
	case OP_Insert: {
		int32_t cursor_id = INSERT_CURSOR_ID(*inst);
		int32_t key_reg = INSERT_KEY_REG(*inst);

		VmCursor   *cursor = &VM.cursors[cursor_id];
		TypedValue *first = &VM.registers[key_reg];
		uint32_t	count = cursor->layout.layout.size() - 1;
		bool		success;
		if (1 == count)
		{
			/*
				If we have a single register record it's either a signle TypedValue, hence doensn't need to be
				built, or b) is a pointer to a blob
			*/
			TypedValue *value = &VM.registers[key_reg + 1];
			success = vmcursor_insert(cursor, first->data, value->data, value->type);
		}
		else
		{
			uint8_t data[cursor->layout.record_size];
			build_record(data, key_reg + 1, count);

			success = vmcursor_insert(cursor, first->data, data, cursor->layout.record_size);
		}

		if (_debug)
		{
			printf("=> Cursor %d insert key=", cursor_id);
			// print_value(first->type, first->data);
			printf(" with %d values, success=%d", count - 1, success);
		}
		if (!success)
		{
			return ERR;
		}
		VM.pc++;
		return OK;
	}
	case OP_Update: {
		int32_t	  cursor_id = UPDATE_CURSOR_ID(*inst);
		int32_t	  record_reg = UPDATE_RECORD_REG(*inst);
		VmCursor *cursor = &VM.cursors[cursor_id];
		uint8_t	  data[cursor->layout.record_size];
		build_record(data, record_reg, cursor->layout.layout.size() - 1);
		bool success = vmcursor_update(cursor, data);
		if (_debug)
		{
			printf("=> Cursor %d update with data from R[%d], "
				   "success=%d",
				   cursor_id, record_reg, success);
		}
		VM.pc++;
		return OK;
	}

	case OP_Begin: {
		pager_begin_transaction();
		VM.pc++;
		break;
	}
	case OP_Commit: {
		pager_commit();
		VM.pc++;
		break;
	}
	case OP_Rollback: {
		pager_rollback();
		return ABORT;
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
vm_execute(VMInstruction *instructions, int instruction_count, MemoryContext *ctx)
{
	reset();
	VM.program = instructions;
	VM.program_size = instruction_count;
	VM.ctx = ctx;
	if (_debug)
	{

		printf("\n===== EXECUTION TRACE =====\n");
	}
	while (!VM.halted && VM.pc < VM.program_size)
	{
		VM_RESULT result = step();
		if (result != OK)
		{
			if (_debug)
			{
				printf("\n\nVM execution failed at PC=%d\n", VM.pc);
				vm_debug_print_all_registers();
			}
			return result;
		}
	}
	if (_debug)
	{
		printf("\n\n===== EXECUTION COMPLETED =====\n");
		vm_debug_print_all_registers();
	}
	return OK;
}

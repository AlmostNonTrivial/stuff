// vm.cpp
#include "vm.hpp"
#include "cassert"
#include "defs.hpp"
#include "pager.hpp"
#include "types.hpp"
#include "blob.hpp"
#include "arena.hpp"
#include "blob.hpp"
#include "btree.hpp"
#include "ephemeral.hpp"
#include "catalog.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CURSORS 10

bool _debug = false;

// POD struct - data only
struct VmCursor
{
	CursorType type;
	Layout	   layout;

	union {
		bt_cursor  bptree;
		et_cursor  mem;
		BlobCursor blob;
	} cursor;
};

// ============================================================================
// Debug Helper Functions
// ============================================================================
void
vm_debug_print_instruction(const VMInstruction *inst, int pc);

const char *
debug_compare_op_name(CompareOp op)
{
	static const char *names[] = {"==", "!=", "<", "<=", ">", ">="};
	return (op >= EQ && op <= GE) ? names[op] : "UNKNOWN";
}

const char *
debug_arith_op_name(ArithOp op)
{
	static const char *names[] = {"+", "-", "*", "/", "%"};
	return (op >= ARITH_ADD && op <= ARITH_MOD) ? names[op] : "UNKNOWN";
}

const char *
debug_logic_op_name(LogicOp op)
{
	static const char *names[] = {"AND", "OR"};
	return (op >= LOGIC_AND && op <= LOGIC_OR) ? names[op] : "UNKNOWN";
}

const char *
debug_cursor_type_name(CursorType type)
{
	switch (type)
	{
	case CursorType::BPLUS:
		return "BPLUS";
	case CursorType::RED_BLACK:
		return "RED_BLACK";
	case CursorType::BLOB:
		return "BLOB";
	default:
		return "UNKNOWN";
	}
}

void
vmcursor_open(VmCursor *cursor, CursorContext *context, MemoryContext *ctx)
{

	auto cur = cursor;
	switch (context->type)
	{
	case CursorType::BPLUS: {

		cur->type = CursorType::BPLUS;
		cur->layout = context->layout;
		cur->cursor.bptree.tree = context->storage.tree;
		cur->cursor.bptree.state = BT_CURSOR_INVALID;
		break;
	}
	case CursorType::RED_BLACK: {

		cur->type = CursorType::RED_BLACK;
		cur->layout = context->layout;
		DataType key_type = cur->layout.layout[0];
		bool	 allow_duplicates = (bool)context->flags;
		cur->cursor.mem.tree = et_create(key_type, cur->layout.record_size, allow_duplicates);
		cur->cursor.mem.state = et_cursor::INVALID;
		cur->cursor.mem.ctx = ctx;
		break;
	}
	case CursorType::BLOB: {

		cur->type = CursorType::BLOB;
		cur->cursor.blob.ctx = ctx;
		break;
	}
	}
}

// Navigation functions
bool
vmcursor_rewind(VmCursor *cur, bool to_end)
{
	switch (cur->type)
	{
	case CursorType::RED_BLACK:
		return to_end ? et_cursor_last(&cur->cursor.mem) : et_cursor_first(&cur->cursor.mem);
	case CursorType::BPLUS:
		return to_end ? btree_cursor_last(&cur->cursor.bptree) : btree_cursor_first(&cur->cursor.bptree);
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
		return forward ? et_cursor_next(&cur->cursor.mem) : et_cursor_previous(&cur->cursor.mem);
	case CursorType::BPLUS:
		return forward ? btree_cursor_next(&cur->cursor.bptree) : btree_cursor_previous(&cur->cursor.bptree);
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
		btree_clear(cursor->cursor.bptree.tree);
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
		return et_cursor_seek(&cur->cursor.mem, key, op);
	case CursorType::BPLUS:
		return btree_cursor_seek(&cur->cursor.bptree, key, op);
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
		return et_cursor_is_valid(&cur->cursor.mem);

	case CursorType::BPLUS:
		return btree_cursor_is_valid(&cur->cursor.bptree);
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
		return (uint8_t*)et_cursor_key(&cur->cursor.mem);
	case CursorType::BPLUS:
		return (uint8_t*)btree_cursor_key(&cur->cursor.bptree);
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
		return (uint8_t*)et_cursor_record(&cur->cursor.mem);
	case CursorType::BPLUS:
		return (uint8_t*)btree_cursor_record(&cur->cursor.bptree);
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
		return et_cursor_insert(&cur->cursor.mem, (void*)key, (void*)record);
	case CursorType::BPLUS:
		return btree_cursor_insert(&cur->cursor.bptree, key, record);
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
		return et_cursor_update(&cur->cursor.mem, record);
	case CursorType::BPLUS:
		return btree_cursor_update(&cur->cursor.bptree, record);
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
		return et_cursor_delete(&cur->cursor.mem);
	case CursorType::BPLUS:
		return btree_cursor_delete(&cur->cursor.bptree);
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
			DataType key_type = cur->layout.layout[0];
			type_print(key_type, key);
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
	ResultCallback emit_row;
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
	set_register(dest, (uint8_t*)src->data, src->type);
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

void
vm_debug_print_program(VMInstruction *instructions, int count)
{
	printf("\n===== PROGRAM LISTING =====\n");
	for (int i = 0; i < count; i++)
	{
		vm_debug_print_instruction(&instructions[i], i);
	}
	printf("===========================\n\n");
}

void
vm_debug_print_all_registers()
{
	printf("===== REGISTERS =====\n");
	for (int i = 0; i < REGISTERS; i++)
	{
		if (VM.registers[i].type != TYPE_NULL && VM.registers[i].data != nullptr)
		{
			printf("R[%2d] = ", i);
			type_print(VM.registers[i].type, VM.registers[i].data);
			printf(" (%s)\n", type_name(VM.registers[i].type));
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
	else
	{
		printf("Invalid cursor ID: %d\n", cursor_id);
	}
}

// ============================================================================
// Main Execution Step
// ============================================================================
static VM_RESULT
step()
{
	VMInstruction *inst = &VM.program[VM.pc];

	// Instruction decode removed - just show execution results

	switch (inst->opcode)
	{
	case OP_Halt: {
		if (_debug)
		{
			printf("=> Halting with code %d\n", HALT_EXIT_CODE(*inst));
		}
		VM.halted = true;
		return OK;
	}
	case OP_Goto: {
		int32_t target = GOTO_TARGET(*inst);
		if (_debug)
		{
			printf("=> Jumping to PC=%d\n", target);
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
			type_print(type, data);
			printf(" (%s)\n", type_name(type));
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
			if (src->data)
			{
				type_print(src->type, src->data);
			}
			else
			{
				printf("NULL");
			}
			printf(" (%s)\n", type_name(src->type));
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
			printf("=> R[%d] = (", dest);
			type_print(a->type, a->data);
			printf(" %s ", debug_compare_op_name(op));
			type_print(b->type, b->data);
			printf(") = %s\n", test_result ? "TRUE" : "FALSE");
		}

		set_register(&VM.registers[dest], (uint8_t *)&test_result, TYPE_U32);
		VM.pc++;
		return OK;
	}
	case OP_Function: {
		int32_t	   dest = FUNCTION_DEST_REG(*inst);
		int32_t	   first_arg = FUNCTION_FIRST_ARG_REG(*inst);
		int32_t	   count = FUNCTION_ARG_COUNT(*inst);
		VMFunction fn = FUNCTION_FUNCTION(*inst);

		if (_debug)
		{
			printf("=> R[%d] = fn(", dest);
			for (int i = 0; i < count; i++)
			{
				if (i > 0)
					printf(", ");
				TypedValue *arg = &VM.registers[first_arg + i];
				printf("R[%d]=", first_arg + i);
				if (arg->data)
				{
					type_print(arg->type, arg->data);
				}
				else
				{
					printf("NULL");
				}
			}
			printf(")\n");
		}

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
			type_print(val->type, val->data);
			printf(" (%s), jump_on_%s => %s to PC=%d\n", is_true ? "TRUE" : "FALSE", jump_on_true ? "true" : "false",
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
		result.data = (uint8_t *)VM.ctx->alloc(type_size(TYPE_U32));
		uint32_t a = *(uint32_t *)VM.registers[left].data;
		uint32_t b = *(uint32_t *)VM.registers[right].data;
		uint32_t res_val;

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
			printf("=> R[%d] = %d %s %d = %d\n", dest, a, debug_logic_op_name(op), b, res_val);
		}

		set_register(&VM.registers[dest], &result);
		VM.pc++;
		return OK;
	}
	case OP_Result: {
		int32_t first_reg = RESULT_FIRST_REG(*inst);
		int32_t reg_count = RESULT_REG_COUNT(*inst);

		if (_debug)
		{
			printf("=> RESULT: ");
			for (int i = 0; i < reg_count; i++)
			{
				if (i > 0)
					printf(", ");
				TypedValue *val = &VM.registers[first_reg + i];
				printf("R[%d]=", first_reg + i);
				if (val->data)
				{
					type_print(val->type, val->data);
				}
				else
				{
					printf("NULL");
				}
			}
			printf("\n");
		}

		// Allocate output array using the context's allocator
		TypedValue *values = (TypedValue *)VM.ctx->alloc(sizeof(TypedValue) * reg_count);

		for (int i = 0; i < reg_count; i++)
		{
			TypedValue *val = &VM.registers[first_reg + i];
			values[i].type = val->type;
			values[i].data = (uint8_t *)VM.ctx->alloc(type_size(val->type));
			memcpy(values[i].data, val->data, type_size(val->type));
		}

		VM.emit_row(values, reg_count);
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
		result.data = (uint8_t *)VM.ctx->alloc(type_size(result.type));

		bool success = true;
		arithmetic(op, result.type, result.data, a->data, b->data);

		if (_debug)
		{
			printf("=> R[%d] = ", dest);
			type_print(a->type, a->data);
			printf(" %s ", debug_arith_op_name(op));
			type_print(b->type, b->data);
			printf(" = ");
			if (success)
			{
				type_print(result.type, result.data);
			}
			else
			{
				printf("ERROR");
			}
			printf("\n");
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

		if (_debug)
		{
			printf("=> Opening cursor %d type=%s\n", cursor_id, debug_cursor_type_name(context->type));
		}

		vmcursor_open(cursor, context, VM.ctx);

		VM.pc++;
		return OK;
	}
	case OP_Close: {
		int32_t cursor_id = CLOSE_CURSOR_ID(*inst);
		if (_debug)
		{
			printf("=> Closed cursor %d\n", cursor_id);
		}

		VM.pc++;
		return OK;
	}
	case OP_Rewind: {
		int32_t	  cursor_id = REWIND_CURSOR_ID(*inst);
		int32_t	  result_reg = REWIND_RESULT_REG(*inst);
		bool	  to_end = REWIND_TO_END(*inst);
		VmCursor *cursor = &VM.cursors[cursor_id];
		bool	  valid = vmcursor_rewind(cursor, to_end);
		uint32_t  result_val = valid ? 1 : 0;

		if (_debug)
		{
			printf("=> Cursor %d rewound to %s, R[%d]=%d\n", cursor_id, to_end ? "end" : "start", result_reg,
				   result_val);
		}

		set_register(&VM.registers[result_reg], (uint8_t *)&result_val, TYPE_U32);
		VM.pc++;
		return OK;
	}
	case OP_Step: {
		int32_t	  cursor_id = STEP_CURSOR_ID(*inst);
		int32_t	  result_reg = STEP_RESULT_REG(*inst);
		bool	  forward = STEP_FORWARD(*inst);
		VmCursor *cursor = &VM.cursors[cursor_id];
		uint32_t  has_more = vmcursor_step(cursor, forward) ? 1 : 0;

		if (_debug)
		{
			printf("=> Cursor %d stepped %s, R[%d]=%d\n", cursor_id, forward ? "forward" : "backward", result_reg,
				   has_more);
		}

		set_register(&VM.registers[result_reg], (uint8_t *)&has_more, TYPE_U32);
		VM.pc++;
		return OK;
	}
	case OP_Seek: {
		int32_t		cursor_id = SEEK_CURSOR_ID(*inst);
		int32_t		key_reg = SEEK_KEY_REG(*inst);
		int32_t		result_reg = SEEK_RESULT_REG(*inst);
		CompareOp	op = SEEK_OP(*inst);
		VmCursor   *cursor = &VM.cursors[cursor_id];
		TypedValue *key = &VM.registers[key_reg];
		bool		found = vmcursor_seek(cursor, op, (uint8_t*)key->data);
		uint32_t	result_val = found ? 1 : 0;

		if (_debug)
		{
			printf("=> Cursor %d seek %s with key=", cursor_id, debug_compare_op_name(op));
			type_print(key->type, key->data);
			printf(", R[%d]=%d\n", result_reg, result_val);
		}

		set_register(&VM.registers[result_reg], (uint8_t *)&result_val, TYPE_U32);
		VM.pc++;
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
				type_print(src.type, src.data);
			}
			else
			{
				printf("NULL");
			}
			printf(" (%s)\n", type_name(src.type));
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
			printf("=> Cursor %d delete %s, cursor still valid=%d\n", cursor_id, success ? "SUCCESS" : "FAILED", valid);
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

		if (_debug)
		{
			printf("=> Cursor %d insert key=", cursor_id);
			type_print(first->type, first->data);
			printf(" with %d record values", count);
		}

		uint8_t data[cursor->layout.record_size];
		if (count > 1)
		{
			build_record(data, key_reg + 1, count);
		}

		success = vmcursor_insert(cursor, (uint8_t*)first->data, data, cursor->layout.record_size);

		if (_debug)
		{
			printf(" [");
			for (uint32_t i = 0; i < count; i++)
			{
				if (i > 0)
					printf(", ");
				TypedValue *val = &VM.registers[key_reg + 1 + i];
				type_print(val->type, val->data);
			}
			printf("]");
		}

		if (_debug)
		{
			printf(", success=%d\n", success);
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
		uint32_t  record_count = cursor->layout.layout.size() - 1;

		build_record(data, record_reg, record_count);
		bool success = vmcursor_update(cursor, data);

		if (_debug)
		{
			printf("=> Cursor %d update with [", cursor_id);
			for (uint32_t i = 0; i < record_count; i++)
			{
				if (i > 0)
					printf(", ");
				TypedValue *val = &VM.registers[record_reg + i];
				type_print(val->type, val->data);
			}
			printf("], success=%d\n", success);
		}

		VM.pc++;
		return OK;
	}

	case OP_Begin: {
		if (_debug)
		{
			printf("=> Beginning transaction\n");
		}
		pager_begin_transaction();
		VM.pc++;
		break;
	}
	case OP_Commit: {
		if (_debug)
		{
			printf("=> Committing transaction\n");
		}
		pager_commit();
		VM.pc++;
		break;
	}
	case OP_Rollback: {
		if (_debug)
		{
			printf("=> Rolling back transaction\n");
		}
		pager_rollback();
		return ABORT;
	}
	case OP_Pack: {
		int32_t dest = PACK2_DEST_REG(*inst);
		int32_t left = PACK2_LEFT_REG(*inst);
		int32_t right = PACK2_RIGHT_REG(*inst);

		TypedValue *a = &VM.registers[left];
		TypedValue *b = &VM.registers[right];

		// Create dual type from the two component types
		DataType dual_type = make_dual(a->type, b->type);
		uint32_t total_size = type_size(dual_type);

		// Allocate space for packed value
		uint8_t packed[total_size];

		// Pack the two values
		pack_dual(packed, a->type, a->data, b->type, b->data);

		if (_debug)
		{
			printf("=> R[%d] = pack(", dest);
			type_print(a->type, a->data);
			printf(", ");
			type_print(b->type, b->data);
			printf(") -> ");
			type_print(dual_type, packed);
			printf(" (%s)\n", type_name(dual_type));
		}

		set_register(&VM.registers[dest], packed, dual_type);
		VM.pc++;
		return OK;
	}

	case OP_Unpack: {
		int32_t first_dest = UNPACK2_FIRST_DEST_REG(*inst);
		int32_t src = UNPACK2_SRC_REG(*inst);

		TypedValue *dual_val = &VM.registers[src];

		assert(type_is_dual(dual_val->type));

		// Get component types
		DataType type1 = dual_component_type(dual_val->type, 0);
		DataType type2 = dual_component_type(dual_val->type, 1);

		// Allocate space for unpacked values
		uint8_t data1[type_size(type1)];
		uint8_t data2[type_size(type2)];

		// Unpack
		unpack_dual(dual_val->type, dual_val->data, data1, data2);

		if (_debug)
		{
			printf("=> unpack(");
			type_print(dual_val->type, dual_val->data);
			printf(") -> R[%d]=", first_dest);
			type_print(type1, data1);
			printf(" (%s), R[%d]=", type_name(type1), first_dest + 1);
			type_print(type2, data2);
			printf(" (%s)\n", type_name(type2));
		}

		set_register(&VM.registers[first_dest], data1, type1);
		set_register(&VM.registers[first_dest + 1], data2, type2);

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
vm_execute(VMInstruction *instructions, int instruction_count, MemoryContext *ctx)
{
	reset();
	VM.program = instructions;
	VM.program_size = instruction_count;
	VM.ctx = ctx;

	if (_debug)
	{
		vm_debug_print_program(instructions, instruction_count);
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

void
set_result_callback(ResultCallback callback)
{
	VM.emit_row = callback;
}

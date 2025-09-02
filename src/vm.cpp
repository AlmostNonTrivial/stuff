// vm.cpp
#include "vm.hpp"
#include "blob.hpp"
#include "arena.hpp"
#include "blob.hpp"
#include "bplustree.hpp"
#include "defs.hpp"
#include "memtree.hpp"
#include "catalog.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ios>

bool _debug = false;

// Initialization functions
void
vmcursor_open_table(VmCursor *cur, const RecordLayout &table_layout, BPlusTree *tree)
{
	cur->type = VmCursor::BPLUS_TABLE;
	cur->layout = table_layout;
	cur->storage.bptree_ptr = tree;
	cur->cursor.bptree.tree = cur->storage.bptree_ptr;
	cur->cursor.bptree.state = BPT_CURSOR_INVALID;
}

void
vmcursor_open_index(VmCursor *cur, const RecordLayout &index_layout, BPlusTree *tree)
{
	cur->type = VmCursor::BPLUS_INDEX;
	cur->layout = index_layout;
	cur->storage.bptree_ptr = tree;
	cur->cursor.bptree.tree = cur->storage.bptree_ptr;
	cur->cursor.bptree.state = BPT_CURSOR_INVALID;
}

void
vmcursor_open_ephemeral(VmCursor *cur, const RecordLayout &ephemeral_layout, MemoryContext *ctx)
{
	cur->type = VmCursor::EPHEMERAL;
	cur->layout = ephemeral_layout;
	DataType key_type = cur->layout.layout.data[0];
	cur->storage.mem_tree = memtree_create(key_type, cur->layout.record_size);
	cur->cursor.mem.tree = &cur->storage.mem_tree;
	cur->cursor.mem.state = MemCursor::INVALID;
	cur->cursor.mem.ctx = ctx;
}

void
vmcursor_open_blob(VmCursor *cur, MemoryContext *ctx)
{
	cur->type = VmCursor::BLOB;
	cur->cursor.blob.ctx = ctx;
}

// Navigation functions
bool
vmcursor_rewind(VmCursor *cur, bool to_end)
{
	switch (cur->type)
	{
	case VmCursor::EPHEMERAL:
		return to_end ? memcursor_last(&cur->cursor.mem) : memcursor_first(&cur->cursor.mem);
	case VmCursor::BPLUS_INDEX:
	case VmCursor::BPLUS_TABLE:
		return to_end ? bplustree_cursor_last(&cur->cursor.bptree) : bplustree_cursor_first(&cur->cursor.bptree);
	case VmCursor::BLOB:
	default:
		return false;
	}
}

bool
vmcursor_step(VmCursor *cur, bool forward)
{
	switch (cur->type)
	{
	case VmCursor::EPHEMERAL:
		return forward ? memcursor_next(&cur->cursor.mem) : memcursor_previous(&cur->cursor.mem);
	case VmCursor::BPLUS_INDEX:
	case VmCursor::BPLUS_TABLE:
		return forward ? bplustree_cursor_next(&cur->cursor.bptree) : bplustree_cursor_previous(&cur->cursor.bptree);
	case VmCursor::BLOB:
	default:
		return false;
	}
}

bool
vmcursor_seek(VmCursor *cur, CompareOp op, uint8_t *key)
{
	switch (cur->type)
	{
	case VmCursor::EPHEMERAL:
		return memcursor_seek_cmp(&cur->cursor.mem, key, op);
	case VmCursor::BPLUS_INDEX:
	case VmCursor::BPLUS_TABLE:
		return bplustree_cursor_seek_cmp(&cur->cursor.bptree, key, op);
	case VmCursor::BLOB:
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
	case VmCursor::EPHEMERAL:
		return memcursor_is_valid(&cur->cursor.mem);
	case VmCursor::BPLUS_INDEX:
	case VmCursor::BPLUS_TABLE:
		return bplustree_cursor_is_valid(&cur->cursor.bptree);
	case VmCursor::BLOB:
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
	case VmCursor::EPHEMERAL:
		return memcursor_key(&cur->cursor.mem);
	case VmCursor::BPLUS_INDEX:
	case VmCursor::BPLUS_TABLE:
		return bplustree_cursor_key(&cur->cursor.bptree);
	case VmCursor::BLOB:
	default:
		return nullptr;
	}
}

uint8_t *
vmcursor_get_record(VmCursor *cur)
{
	switch (cur->type)
	{
	case VmCursor::EPHEMERAL:
		return memcursor_record(&cur->cursor.mem);
	case VmCursor::BPLUS_INDEX:
	case VmCursor::BPLUS_TABLE:
		return bplustree_cursor_record(&cur->cursor.bptree);
	case VmCursor::BLOB:
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
	return record + cur->layout.offsets.data[col_index - 1];
}

DataType
vmcursor_column_type(VmCursor *cur, uint32_t col_index)
{
	return cur->layout.layout.data[col_index];
}

// Modification functions
bool
vmcursor_insert(VmCursor *cur, uint8_t *key, uint8_t *record, uint32_t size)
{
	switch (cur->type)
	{
	case VmCursor::EPHEMERAL:
		return memcursor_insert(&cur->cursor.mem, key, record);
	case VmCursor::BPLUS_TABLE:
	case VmCursor::BPLUS_INDEX:
		return bplustree_cursor_insert(&cur->cursor.bptree, key, record);
	case VmCursor::BLOB:
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
	case VmCursor::EPHEMERAL:
		return memcursor_update(&cur->cursor.mem, record);
	case VmCursor::BPLUS_INDEX:
	case VmCursor::BPLUS_TABLE:
		return bplustree_cursor_update(&cur->cursor.bptree, record);
	case VmCursor::BLOB:
	default:
		return false;
	}
}

bool
vmcursor_remove(VmCursor *cur)
{
	switch (cur->type)
	{
	case VmCursor::EPHEMERAL:
		return memcursor_delete(&cur->cursor.mem);
	case VmCursor::BPLUS_INDEX:
	case VmCursor::BPLUS_TABLE:
		return bplustree_cursor_delete(&cur->cursor.bptree);
	case VmCursor::BLOB:
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
	case VmCursor::EPHEMERAL:
		return "MEMTREE";
	case VmCursor::BPLUS_INDEX:
		return "BTREE_INDEX";
	case VmCursor::BPLUS_TABLE:
		return "BPLUS_TABLE";
	case VmCursor::BLOB:
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
			print_value(cur->layout.layout.data[0], key);
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
	VMValue		   registers[REGISTERS];
	VmCursor	   cursors[CURSORS];
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
	if (data)
	{
		memcpy(dest->data, data, type);
	}
	else
	{
		memset(dest->data, 0, type);
	}
}
static void
build_record(uint8_t *data, int32_t first_reg)
{
	int32_t offset = 0;
	int		i = 0;
	while (true)
	{
		VMValue *val = &VM.registers[first_reg + i++];
		if (TYPE_NULL == val->type)
		{
			break;
		}

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
	for (uint32_t i = 0; i < REGISTERS; i++)
	{
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
	for (int i = 0; i < REGISTERS; i++)
	{
		if (VM.registers[i].type != TYPE_NULL)
		{
			Debug::print_register(i, VM.registers[i]);
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
		printf("\n[%3d] %-12s ", VM.pc, Debug::opcode_name(inst->opcode));
	}
	switch (inst->opcode)
	{
	case OP_Halt: {
		if (_debug)
		{
			printf("=> Halting with code %d", Opcodes::Halt::exit_code(*inst));
		}
		VM.halted = true;
		return OK;
	}
	case OP_Goto: {
		int32_t target = Opcodes::Goto::target(*inst);
		if (_debug)
		{
			printf("=> Jumping to PC=%d", target);
		}
		VM.pc = target;
		return OK;
	}
	case OP_Load: {
		int32_t	 dest_reg = Opcodes::Move::dest_reg(*inst);
		uint8_t *data = Opcodes::Move::data(*inst);
		DataType type = Opcodes::Move::type(*inst);
		if (_debug)
		{
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
		auto   *src = &VM.registers[src_reg];
		if (_debug)
		{
			printf("=> R[%d] = R[%d] = ", dest_reg, src_reg);
			print_value(src->type, src->data);
			printf(" (type=%d)", src->type);
		}
		set_register(&VM.registers[dest_reg], src);
		VM.pc++;
		return OK;
	}
	case OP_Test: {
		int32_t	  dest = Opcodes::Test::dest_reg(*inst);
		int32_t	  left = Opcodes::Test::left_reg(*inst);
		int32_t	  right = Opcodes::Test::right_reg(*inst);
		CompareOp op = Opcodes::Test::op(*inst);
		VMValue	 *a = &VM.registers[left];
		VMValue	 *b = &VM.registers[right];
		int		  cmp_result = cmp(a->type, a->data, b->data);
		VMValue	  result = {.type = TYPE_4};
		bool	  test_result = false;
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
		*(uint32_t *)result.data = test_result ? 1 : 0;
		if (_debug)
		{
			const char *op_names[] = {"==", "!=", "<", "<=", ">", ">="};
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
	case OP_Function: {
		int32_t	   dest = inst->p1;
		int32_t	   first_arg = inst->p2;
		int32_t	   count = inst->p3;
		VMFunction fn = (VMFunction)inst->p4;

		// Pass registers directly as array
		bool success = fn(&VM.registers[dest], &VM.registers[first_arg], count, VM.ctx);

		if (!success) {
				return ERR;
		}

		VM.pc++;
		return OK;
	}
	case OP_JumpIf: {
		int32_t	 test_reg = Opcodes::JumpIf::test_reg(*inst);
		int32_t	 target = Opcodes::JumpIf::jump_target(*inst);
		bool	 jump_on_true = Opcodes::JumpIf::jump_on_true(*inst);
		VMValue *val = &VM.registers[test_reg];
		bool	 is_true = (*(uint32_t *)val->data != 0);
		bool	 will_jump = (is_true && jump_on_true) || (!is_true && !jump_on_true);
		if (_debug)
		{
			printf("=> R[%d]=", test_reg);
			print_value(val->type, val->data);
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
		int32_t	 dest = Opcodes::Logic::dest_reg(*inst);
		int32_t	 left = Opcodes::Logic::left_reg(*inst);
		int32_t	 right = Opcodes::Logic::right_reg(*inst);
		LogicOp	 op = Opcodes::Logic::op(*inst);
		VMValue	 result{.type = TYPE_4};
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
			const char *op_names[] = {"AND", "OR"};
			printf("=> R[%d] = %d %s %d = %d", dest, a, op_names[op], b, res_val);
		}
		set_register(&VM.registers[dest], &result);
		VM.pc++;
		return OK;
	}
	case OP_Result: {
		int32_t first_reg = Opcodes::Result::first_reg(*inst);
		int32_t reg_count = Opcodes::Result::reg_count(*inst);

		// Allocate output array using the context's allocator
		TypedValue *values = (TypedValue *)VM.ctx->alloc(sizeof(TypedValue) * reg_count);

		for (int i = 0; i < reg_count; i++)
		{
			VMValue *val = &VM.registers[first_reg + i];
			values[i].type = val->type;
			values[i].data = (uint8_t *)VM.ctx->alloc(val->type);
			memcpy(values[i].data, val->data, val->type);
		}

		// not sure how this will work for blobs
		VM.ctx->emit_row(values, reg_count);

		VM.pc++;
		return OK;
	}
	case OP_Arithmetic: {
		int32_t	 dest = Opcodes::Arithmetic::dest_reg(*inst);
		int32_t	 left = Opcodes::Arithmetic::left_reg(*inst);
		int32_t	 right = Opcodes::Arithmetic::right_reg(*inst);
		ArithOp	 op = Opcodes::Arithmetic::op(*inst);
		VMValue *a = &VM.registers[left];
		VMValue *b = &VM.registers[right];
		VMValue	 result = {.type = (a->type > b->type) ? a->type : b->type};
		bool	 success = do_arithmetic(op, result.type, result.data, a->data, b->data);
		if (_debug)
		{
			const char *op_names[] = {"+", "-", "*", "/", "%"};
			printf("=> R[%d] = ", dest);
			print_value(a->type, a->data);
			printf(" %s ", op_names[op]);
			print_value(b->type, b->data);
			printf(" = ");
			if (success)
			{
				print_value(result.type, result.data);
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
		int32_t	  cursor_id = Opcodes::Open::cursor_id(*inst);
		bool	  is_ephemeral = Opcodes::Open::is_ephemeral(*inst);
		VmCursor &cursor = VM.cursors[cursor_id];
		if (is_ephemeral)
		{
			RecordLayout *layout = Opcodes::Open::ephemeral_schema(*inst);
			vmcursor_open_ephemeral(&cursor, *layout, VM.ctx);
			if (_debug)
			{
				printf("=> Opened ephemeral cursor %d", cursor_id);
			}
		}
		else
		{
			const char *table_name = Opcodes::Open::table_name(*inst);
			int32_t		index_column = Opcodes::Open::index_col(*inst);
			if (index_column != 0)
			{
				Index *index = get_index(table_name, index_column);
				vmcursor_open_index(&cursor, index->to_layout(), &index->btree);
				if (_debug)
				{
					printf("=> Opened index cursor %d on "
						   "%s.%d",
						   cursor_id, table_name, index_column);
				}
			}
			else
			{
				Table *table = get_table(table_name);
				vmcursor_open_table(&cursor, table->to_layout(), &table->bplustree);
				if (_debug)
				{
					printf("=> Opened table cursor %d on %s", cursor_id, table_name);
				}
			}
		}
		VM.pc++;
		return OK;
	}
	case OP_Close: {
		int32_t cursor_id = Opcodes::Close::cursor_id(*inst);
		if (_debug)
		{
			printf("=> Closed cursor %d", cursor_id);
		}
		// if(VM.cursors[cursor_id] == 0)

		VM.pc++;
		return OK;
	}
	case OP_Rewind: {
		int32_t	  cursor_id = Opcodes::Rewind::cursor_id(*inst);
		int32_t	  jump_if_empty = Opcodes::Rewind::jump_if_empty(*inst);
		bool	  to_end = Opcodes::Rewind::to_end(*inst);
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
		int32_t	  cursor_id = Opcodes::Step::cursor_id(*inst);
		int32_t	  jump_if_done = Opcodes::Step::jump_if_done(*inst);
		bool	  forward = Opcodes::Step::forward(*inst);
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
		int32_t	  cursor_id = Opcodes::Seek::cursor_id(*inst);
		int32_t	  key_reg = Opcodes::Seek::key_reg(*inst);
		int32_t	  jump_if_not = Opcodes::Seek::jump_if_not(*inst);
		CompareOp op = Opcodes::Seek::op(*inst);
		VmCursor *cursor = &VM.cursors[cursor_id];
		VMValue	 *key = &VM.registers[key_reg];
		bool	  found;

		found = vmcursor_seek(cursor, op, key->data);

		if (_debug)
		{
			const char *op_names[] = {"EQ", "NE", "LT", "LE", "GT", "GE"};
			printf("=> Cursor %d seek %s with key=", cursor_id, op_names[op]);
			print_value(key->type, key->data);
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
		int32_t	  cursor_id = Opcodes::Column::cursor_id(*inst);
		int32_t	  col_index = Opcodes::Column::column_index(*inst);
		int32_t	  dest_reg = Opcodes::Column::dest_reg(*inst);
		VmCursor *cursor = &VM.cursors[cursor_id];
		uint8_t	 *data = vmcursor_column(cursor, col_index);
		DataType  type = vmcursor_column_type(cursor, col_index);
		if (_debug)
		{
			printf("=> R[%d] = cursor[%d].col[%d] = ", dest_reg, cursor_id, col_index);
			if (data)
			{
				print_value(type, data);
			}
			else
			{
				printf("NULL");
			}
			printf(" (type=%d)", type);
		}
		set_register(&VM.registers[dest_reg], data, type);
		VM.pc++;
		return OK;
	}
	case OP_Delete: {
		int32_t	  cursor_id = Opcodes::Delete::cursor_id(*inst);
		int32_t	  delete_occured = Opcodes::Delete::delete_occured_reg(*inst);
		int32_t	  cursor_valid = Opcodes::Delete::cursor_valid_reg(*inst);
		VmCursor *cursor = &VM.cursors[cursor_id];
		int32_t	  success = vmcursor_remove(cursor) ? 1 : 0;
		int32_t	  valid = vmcursor_is_valid(cursor) ? 1 : 0;
		set_register(&VM.registers[delete_occured], (uint8_t *)&success, TYPE_4);
		set_register(&VM.registers[cursor_valid], (uint8_t *)&valid, TYPE_4);

		if (_debug)
		{
			printf("=> Cursor %d delete %s", cursor_id, success ? "OK" : "FAILED");
		}
		VM.pc++;
		return OK;
	}
	case OP_Insert: {
		int32_t cursor_id = Opcodes::Insert::cursor_id(*inst);
		int32_t key_reg = Opcodes::Insert::key_reg(*inst);
		int32_t count = Opcodes::Insert::reg_count(*inst);

		VmCursor *cursor = &VM.cursors[cursor_id];
		VMValue	 *first = &VM.registers[key_reg];

		bool success;
		if (1 > count)
		{
		/*
		    If we have a single register record it's either a signle VMValue, hence doensn't need to be
			built, or b) is a pointer to a blob
	    */
			VMValue *value = &VM.registers[key_reg + 1];
			success = vmcursor_insert(cursor, first->data, value->data, value->type);
		}
		else
		{
			uint8_t data[cursor->layout.record_size];
			build_record(data, key_reg + 1);

			success = vmcursor_insert(cursor, first->data, data, cursor->layout.record_size);
		}

		if (_debug)
		{
			printf("=> Cursor %d insert key=", cursor_id);
			print_value(first->type, first->data);
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
		int32_t	  cursor_id = Opcodes::Update::cursor_id(*inst);
		int32_t	  record_reg = Opcodes::Update::record_reg(*inst);
		VmCursor *cursor = &VM.cursors[cursor_id];
		uint8_t	  data[cursor->layout.record_size];
		build_record(data, record_reg);
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
		Debug::print_program(instructions, instruction_count);
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

#include "vm.hpp"
#include "arena.hpp"
#include "defs.hpp"
#include <cstring>
#include <cstdio>
#include <vector>

VM vm_create() {
    return VM{};
}

void vm_destroy(VM* vm) {
    // Arena will clean up all memory
}

void vm_reset(VM* vm) {
    vm->pc = 0;
    vm->halted = false;
    vm->compare_result = 0;

    // Clear registers
    for (uint32_t i = 0; i < REGISTER_COUNT; i++) {
        vm->registers[i].type = TYPE_NULL;
    }

    vm->cursors.clear();

}

void vm_load_program(VM* vm, std::vector<VMInstruction> instructions, uint32_t count) {
    vm->program = instructions;
    vm->pc = 0;
    vm->halted = false;
}

bool vm_step(VM* vm) {
    if (vm->halted || vm->pc >= vm->program.size()) {
        return false;
    }

    VMInstruction* inst = &vm->program[vm->pc];

    switch (inst->opcode) {
        case OP_Halt:
            vm->halted = true;
            return true;

        case OP_Goto:
            vm->pc = inst->p2;
            return true;

        case OP_If: {
            VMValue* val = &vm->registers[inst->p1];
            if (val->type != VMValue::TYPE_NULL &&
                (val->type != VMValue::TYPE_INT32 || val->v.i32 != 0)) {
                vm->pc = inst->p2;
            } else {
                vm->pc++;
            }
            return true;
        }

        case OP_IfNot: {
            VMValue* val = &vm->registers[inst->p1];
            if (val->type == VMValue::TYPE_NULL ||
                (val->type == VMValue::TYPE_INT32 && val->v.i32 == 0)) {
                vm->pc = inst->p2;
            } else {
                vm->pc++;
            }
            return true;
        }

        case OP_Integer:
            vm->registers[inst->p1].type = VMValue::TYPE_INT32;
            vm->registers[inst->p1].v.i32 = inst->p2;
            vm->pc++;
            return true;

        case OP_String:
            vm->registers[inst->p1].type = VMValue::TYPE_TEXT;
            vm->registers[inst->p1].v.text = (char*)inst->p4;
            vm->pc++;
            return true;

        case OP_Copy:
            vm->registers[inst->p2] = vm->registers[inst->p1];
            vm->pc++;
            return true;

        case OP_Move:
            vm->registers[inst->p2] = vm->registers[inst->p1];
            vm->registers[inst->p1].type = TYPE_NULL;
            vm->pc++;
            return true;

        case OP_OpenRead:
        case OP_OpenWrite: {
            const char* table_name = (const char*)inst->p4;
            uint32_t cursor_id = inst->p2;
            bool is_write = (inst->opcode == OP_OpenWrite);

            Table* table = vm_get_table(vm, table_name);
            if (!table) {
                return false;
            }

            BtCursor* btc = bt_cursor_create(table->tree, is_write);
            vm->cursors[cursor_id] = vmc_create(btc, 0, is_write,
                                                table_name, table->schema);
            vm->pc++;
            return true;
        }

        case OP_Close:
            vm->cursors[inst->p1] = nullptr;
            vm->pc++;
            return true;

        case OP_Rewind: {
            VMCursor* cursor = vm->cursors[inst->p1];
            if (!cursor) return false;

            if (!vmc_move_first(cursor) && inst->p2 > 0) {
                vm->pc = inst->p2;
            } else {
                vm->pc++;
            }
            return true;
        }

        case OP_Next: {
            VMCursor* cursor = vm->cursors[inst->p1];
            if (!cursor) return false;

            if (vmc_move_next(cursor)) {
                vm->pc = inst->p2;
            } else {
                vm->pc++;
            }
            return true;
        }

        case OP_Prev: {
            VMCursor* cursor = vm->cursors[inst->p1];
            if (!cursor) return false;

            if (vmc_move_prev(cursor)) {
                vm->pc = inst->p2;
            } else {
                vm->pc++;
            }
            return true;
        }

        case OP_SeekEQ: {
            VMCursor* cursor = vm->cursors[inst->p1];
            uint32_t key = vm->registers[inst->p2].v.i32;

            if (!vmc_seek_rowid(cursor, key) && inst->p3 > 0) {
                vm->pc = inst->p3;
            } else {
                vm->pc++;
            }
            return true;
        }

        case OP_Column: {
            VMCursor* cursor = vm->cursors[inst->p1];
            void* data = vmc_get_column(cursor, inst->p2);

            if (data) {
                // Based on schema type, set register value
                VMType col_type = cursor->schema->columns[inst->p2].type;
                VMValue* reg = &vm->registers[inst->p3];

                switch (col_type) {
                    case VM_UINT32:
                        reg->type = VMValue::TYPE_INT32;
                        reg->v.i32 = *(int32_t*)data;
                        break;
                    case VM_INT64:
                        reg->type = VMValue::TYPE_INT64;
                        reg->v.i64 = *(int64_t*)data;
                        break;
                    case VM_TEXT32:
                    case VM_TEXT256:
                        reg->type = VMValue::TYPE_TEXT;
                        reg->v.text = (char*)data;
                        break;
                    default:
                        reg->type = VMValue::TYPE_NULL;
                }
            }
            vm->pc++;
            return true;
        }

        case OP_Rowid: {
            VMCursor* cursor = vm->cursors[inst->p1];
            vm->registers[inst->p2].type = VMValue::TYPE_INT32;
            vm->registers[inst->p2].v.i32 = vmc_get_rowid(cursor);
            vm->pc++;
            return true;
        }

        case OP_Insert: {
            VMCursor* cursor = vm->cursors[inst->p1];
            uint32_t rowid = vm->registers[inst->p3].v.i32;
            void* record = vm->registers[inst->p2].v.blob.data;

            if (!vmc_insert(cursor, rowid, record)) {
                return false;
            }
            vm->pc++;
            return true;
        }

        case OP_Delete: {
            VMCursor* cursor = vm->cursors[inst->p1];
            if (!vmc_delete(cursor)) {
                return false;
            }
            vm->pc++;
            return true;
        }

        case OP_Compare: {
            VMValue* a = &vm->registers[inst->p1];
            VMValue* b = &vm->registers[inst->p2];

            // Simple integer comparison for now
            if (a->type == VMValue::TYPE_INT32 && b->type == VMValue::TYPE_INT32) {
                if (a->v.i32 < b->v.i32) vm->compare_result = -1;
                else if (a->v.i32 > b->v.i32) vm->compare_result = 1;
                else vm->compare_result = 0;
            } else {
                vm->compare_result = 0;
            }
            vm->pc++;
            return true;
        }

        case OP_Jump: {
            if (vm->compare_result < 0 && inst->p1 > 0) {
                vm->pc = inst->p1;
            } else if (vm->compare_result == 0 && inst->p2 > 0) {
                vm->pc = inst->p2;
            } else if (vm->compare_result > 0 && inst->p3 > 0) {
                vm->pc = inst->p3;
            } else {
                vm->pc++;
            }
            return true;
        }

        case OP_ResultRow: {
            if (vm->result_callback) {
                VMValue** row = ARENA_ALLOC_ARRAY(VMValue*, inst->p2);
                for (int i = 0; i < inst->p2; i++) {
                    row[i] = &vm->registers[inst->p1 + i];
                }
                vm->result_callback(row, inst->p2);
            }
            vm->pc++;
            return true;
        }

        case OP_Begin:
            pager_begin_transaction();
            vm->in_transaction = true;
            vm->pc++;
            return true;

        case OP_Commit:
            pager_commit();
            vm->in_transaction = false;
            vm->pc++;
            return true;

        case OP_Rollback:
            pager_rollback();
            vm->in_transaction = false;
            vm->pc++;
            return true;

        default:
            printf("Unknown opcode: %d\n", inst->opcode);
            return false;
    }
}

bool vm_execute(VM* vm) {
    while (!vm->halted && vm->pc < vm->program_size) {
        if (!vm_step(vm)) {
            if (vm->in_transaction) {
                pager_rollback();
                vm->in_transaction = false;
            }
            return false;
        }
    }
    return true;
}

bool vm_create_table(VM* vm, TableSchema* schema) {
    // Expand tables array
    Table new_tables = {0};
    new_tables.schema = *schema;

    uint32_t record_size = 0;
    for (uint32_t i = 1; i < schema->column_count; i++) {
        record_size += schema->columns[i].type;
    }
    if(record_size == 0) {
        return false;
    }

    new_tables.tree = bt_create(schema->columns[0], record_size, BPLUS);
    bp_init(*tree);

    return true;
}

Table* vm_get_table(VM* vm, const char* name) {
    for (uint32_t i = 0; i < vm->table_count; i++) {
        if (strcmp(vm->tables[i].schema->table_name, name) == 0) {
            return &vm->tables[i];
        }
    }
    return nullptr;
}

Index* vm_get_index(VM* vm, const char* name) {
    for (uint32_t i = 0; i < vm->index_count; i++) {
        if (strcmp(vm->indexes[i].name, name) == 0) {
            return &vm->indexes[i];
        }
    }
    return nullptr;
}

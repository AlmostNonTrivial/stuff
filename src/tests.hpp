#pragma once
// test_integration.cpp
#include "pager.hpp"
#include "schema.hpp"
#include "btree.hpp"
#include "bplustree.hpp"
#include "vm.hpp"
#include "arena.hpp"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>



// Test data arrays
const char* product_names[] = {
    "Laptop", "Mouse", "Keyboard", "Monitor", "Headphones",
    "USB Cable", "Webcam", "Microphone", "Speaker", "Tablet"
};

const char* customer_names[] = {
    "Alice Johnson", "Bob Smith", "Carol White", "David Brown",
    "Eve Davis", "Frank Miller", "Grace Wilson", "Henry Moore"
};

const char* order_statuses[] = {
    "pending", "processing", "shipped", "delivered", "cancelled"
};

// Helper function to create a test table
inline Table* create_test_table(const char* table_name,
                         const std::vector<ColumnInfo>& columns,
                         TreeType tree_type = TreeType::BTREE) {

    Table* table = (Table*)arena::alloc<RegistryArena>(sizeof(Table));
    table->table_name = table_name;
    table->tree_type = tree_type;

    // Add columns
    for (const auto& col : columns) {
        table->columns.push_back(col);
    }

    // Calculate layout
    RecordLayout layout = table->to_layout();
    DataType key_type = table->columns[0].type;
    uint32_t record_size = layout.record_size - key_type;

    // Create the tree
    if (tree_type == TreeType::BTREE) {
        table->tree.btree = btree_create(key_type, record_size);
    } else {
        table->tree.bplustree = bplustree_create(key_type, record_size);
    }

    // Register the table
    if (!add_table(table)) {
        printf("Failed to register table '%s'\n", table_name);
        return nullptr;
    }

    printf("Created table '%s' with %zu columns (using %s)\n",
           table_name, columns.size(),
           tree_type == TreeType::BTREE ? "BTree" : "B+Tree");

    return table;
}

// Create Products table
inline void setup_products_table() {
    std::vector<ColumnInfo> columns = {
        {"product_id", TYPE_4},     // INT32 primary key
        {"name", TYPE_32},           // VARCHAR32
        {"price", TYPE_4},           // INT32 (in cents)
        {"stock_quantity", TYPE_4}   // INT32
    };

    Table* products = create_test_table("products", columns, TreeType::BTREE);

    // Insert test data using cursor
    VmCursor cursor;
    cursor.open_btree_table(products->to_layout(), &products->tree.btree);



    printf("Inserting products...\n");
    for (int i = 0; i < 10; i++) {
        uint8_t key[TYPE_4];
        uint8_t record[TYPE_32 + TYPE_4 + TYPE_4];

        // Set product_id (key)
        *(int32_t*)key = i + 1;

        // Set name
        memset(record, 0, TYPE_32);
        strncpy((char*)record, product_names[i], TYPE_32 - 1);

        // Set price (in cents - random between $10 and $500)
        *(int32_t*)(record + TYPE_32) = 1000 + (i * 3900 / 9);

        // Set stock quantity (random between 5 and 100)
        *(int32_t*)(record + TYPE_32 + TYPE_4) = 5 + (i * 10);

        if (cursor.insert(key, record)) {
            printf("  - Inserted product %d: %s\n", i + 1, product_names[i]);
        } else {
            printf("  - Failed to insert product %d\n", i + 1);
        }
    }
}

// Create Customers table (using B+Tree)
inline void setup_customers_table() {
    std::vector<ColumnInfo> columns = {
        {"customer_id", TYPE_4},   // INT32 primary key
        {"name", TYPE_32},          // VARCHAR32
        {"email", TYPE_32},         // VARCHAR32
        {"created_date", TYPE_4}    // INT32 (unix timestamp)
    };

    Table* customers = create_test_table("customers", columns, TreeType::BPLUSTREE);
    if (!customers) return;

    // Insert test data using cursor
    VmCursor cursor;
    cursor.open_bplus_table(customers->to_layout(), &customers->tree.bplustree);

    printf("Inserting customers...\n");
    for (int i = 0; i < 8; i++) {
        uint8_t key[TYPE_4];
        uint8_t record[TYPE_32 + TYPE_32 + TYPE_4];

        // Set customer_id (key)
        *(int32_t*)key = i + 1;

        // Set name
        memset(record, 0, TYPE_32);
        strncpy((char*)record, customer_names[i], TYPE_32 - 1);

        // Set email
        char email[TYPE_32];
        snprintf(email, TYPE_32, "%s@example.com",
                customer_names[i][0] == ' ' ? "user" : customer_names[i]);
        // Simple email: first letter + last name
        for (char* p = email; *p; p++) {
            if (*p == ' ') *p = '.';
            else if (*p >= 'A' && *p <= 'Z') *p = *p - 'A' + 'a';
        }
        memset(record + TYPE_32, 0, TYPE_32);
        strncpy((char*)(record + TYPE_32), email, TYPE_32 - 1);

        // Set created_date (simulated timestamps)
        *(int32_t*)(record + TYPE_32 + TYPE_32) = 1700000000 + (i * 86400);

        if (cursor.insert(key, record)) {
            printf("  - Inserted customer %d: %s\n", i + 1, customer_names[i]);
        } else {
            printf("  - Failed to insert customer %d\n", i + 1);
        }
    }
}

// Create Orders table
inline void setup_orders_table() {
    std::vector<ColumnInfo> columns = {
        {"order_id", TYPE_4},       // INT32 primary key
        {"customer_id", TYPE_4},    // INT32 foreign key
        {"product_id", TYPE_4},     // INT32 foreign key
        {"quantity", TYPE_4},       // INT32
        {"status", TYPE_32},        // VARCHAR32
        {"order_date", TYPE_4}      // INT32 (unix timestamp)
    };

    Table* orders = create_test_table("orders", columns, TreeType::BTREE);
    if (!orders) return;

    // Insert test data using cursor
    VmCursor cursor;
    cursor.open_btree_table(orders->to_layout(), &orders->tree.btree);

    printf("Inserting orders...\n");
    for (int i = 0; i < 20; i++) {
        uint8_t key[TYPE_4];
        uint8_t record[TYPE_4 + TYPE_4 + TYPE_4 + TYPE_32 + TYPE_4];

        // Set order_id (key)
        *(int32_t*)key = i + 1;

        // Set customer_id (1-8)
        *(int32_t*)record = (i % 8) + 1;

        // Set product_id (1-10)
        *(int32_t*)(record + TYPE_4) = (i % 10) + 1;

        // Set quantity (1-5)
        *(int32_t*)(record + TYPE_4 + TYPE_4) = (i % 5) + 1;

        // Set status
        memset(record + TYPE_4 + TYPE_4 + TYPE_4, 0, TYPE_32);
        strncpy((char*)(record + TYPE_4 + TYPE_4 + TYPE_4),
                order_statuses[i % 5], TYPE_32 - 1);

        // Set order_date (simulated timestamps)
        *(int32_t*)(record + TYPE_4 + TYPE_4 + TYPE_4 + TYPE_32) =
            1700000000 + (i * 3600);

        if (cursor.insert(key, record)) {
            printf("  - Inserted order %d (customer: %d, product: %d)\n",
                   i + 1, (i % 8) + 1, (i % 10) + 1);
        } else {
            printf("  - Failed to insert order %d\n", i + 1);
        }
    }
}

// Create an index on orders.customer_id
inline void create_customer_index() {
    printf("\nCreating index on orders.customer_id...\n");

    Table* orders = get_table("orders");
    if (!orders) {
        printf("Orders table not found\n");
        return;
    }

    // Create index using B+Tree for efficient range queries
    if (!orders->create_index("idx_orders_customer", 1, TreeType::BPLUSTREE)) {
        printf("Failed to create index structure\n");
        return;
    }

    Index* index = get_index("orders", 1);
    if (!index) {
        printf("Failed to get index\n");
        return;
    }

    // Populate index by scanning the table
    VmCursor table_cursor;
    table_cursor.open_btree_table(orders->to_layout(), &orders->tree.btree);

    VmCursor index_cursor;
    index_cursor.open_bplus_index(index->to_layout(), &index->tree.bplustree);

    int indexed_count = 0;
    if (table_cursor.rewind()) {
        do {
            // Get the primary key (order_id) and customer_id from table
            uint8_t* order_id = table_cursor.get_key();
            uint8_t* record = table_cursor.get_record();

            if (order_id && record) {
                // customer_id is the first field in the record
                uint8_t customer_id[TYPE_4];
                memcpy(customer_id, record, TYPE_4);

                // Insert into index: key=customer_id, value=order_id
                if (index_cursor.insert(customer_id, order_id)) {
                    indexed_count++;
                }
            }
        } while (table_cursor.step());
    }

    printf("Index created with %d entries\n", indexed_count);
}

// Test function to verify data using cursors
inline void verify_data() {
    printf("\n=== Verifying Data ===\n");

    // Verify products
    Table* products = get_table("products");
    if (products) {
        VmCursor cursor;
        cursor.open_btree_table(products->to_layout(), &products->tree.btree);

        int count = 0;
        if (cursor.rewind()) {
            do {
                count++;
            } while (cursor.step());
        }
        printf("Products table: %d records\n", count);
    }

    // Verify customers
    Table* customers = get_table("customers");
    if (customers) {
        VmCursor cursor;
        cursor.open_bplus_table(customers->to_layout(), &customers->tree.bplustree);

        int count = 0;
        if (cursor.rewind()) {
            do {
                count++;
            } while (cursor.step());
        }
        printf("Customers table: %d records\n", count);
    }

    // Verify orders
    Table* orders = get_table("orders");
    if (orders) {
        VmCursor cursor;
        cursor.open_btree_table(orders->to_layout(), &orders->tree.btree);

        int count = 0;
        if (cursor.rewind()) {
            do {
                count++;
            } while (cursor.step());
        }
        printf("Orders table: %d records\n", count);
    }

    // Test index lookup
    Index* idx = get_index("orders", 1);
    if (idx) {
        VmCursor cursor;
        cursor.open_bplus_index(idx->to_layout(), &idx->tree.bplustree);

        // Find orders for customer_id = 3
        uint8_t search_key[TYPE_4];
        *(int32_t*)search_key = 3;

        int found = 0;
        if (cursor.seek(GE, search_key)) {
            do {
                uint8_t* key = cursor.get_key();
                if (key && *(int32_t*)key == 3) {
                    found++;
                } else {
                    break; // Different customer_id
                }
            } while (cursor.step());
        }
        printf("Index lookup: Found %d orders for customer_id=3\n", found);
    }
}

#pragma  once



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

bool execute_sql_statement(const char *sql, bool asda = false);
inline void create_all_tables_sql(bool create) {
    if (!create) return;

    const char *create_users_sql = "CREATE TABLE users ("
                                   "user_id INT, "
                                   "username TEXT, "
                                   "email TEXT, "
                                   "age INT, "
                                   "city TEXT"
                                   ");";

    if (!execute_sql_statement(create_users_sql)) return;

    const char *create_products_sql = "CREATE TABLE products ("
                                      "product_id INT, "
                                      "title TEXT, "
                                      "category TEXT, "
                                      "price INT, "
                                      "stock INT, "
                                      "brand TEXT"
                                      ");";

    if (!execute_sql_statement(create_products_sql)) return;

    const char *create_orders_sql = "CREATE TABLE orders ("
                                    "order_id INT, "
                                    "user_id INT, "
                                    "total INT, "
                                    "total_quantity INT, "
                                    "discount INT"
                                    ");";

    if (!execute_sql_statement(create_orders_sql)) return;
}

inline void load_table_from_csv_sql(const char *csv_file, const char *table_name) {
    CSVReader reader(csv_file);
    std::vector<std::string> fields;

    int count = 0;
    int batch_count = 0;
    const int BATCH_SIZE = 50;

    Structure *structure = catalog.get(table_name);
    if (!structure) return;

    string<query_arena> column_list;
    for (uint32_t i = 0; i < structure->columns.size; i++) {
        if (i > 0) column_list.append(", ");
        column_list.append(structure->columns[i].name);
    }

    while (reader.next_row(fields)) {
        if (fields.size() != structure->columns.size) {
            printf("Warning: row has %zu fields, expected %zu\n", fields.size(), structure->columns.size);
            continue;
        }

        string<query_arena> sql;
        sql.append("INSERT INTO ");
        sql.append(table_name);
        sql.append(" (");
        sql.append(column_list);
        sql.append(") VALUES (");

        for (size_t i = 0; i < fields.size(); i++) {
            if (i > 0) sql.append(", ");

            DataType col_type = structure->columns[i].type;

            if (type_is_numeric(col_type)) {
                sql.append(fields[i].c_str());
            } else if (type_is_string(col_type)) {
                sql.append("'");
                for (char c : fields[i]) {
                    if (c == '\'') {
                        sql.append("''");
                    } else {
                        sql.append(&c, 1);
                    }
                }
                sql.append("'");
            }
        }

        sql.append(");");

        if (execute_sql_statement(sql.c_str())) {
            count++;
        } else {
            printf("❌ Failed to insert row %d\n", count + 1);
        }

        if (++batch_count >= BATCH_SIZE) {
            batch_count = 0;
        }
    }
}

inline void load_all_data_sql() {
    load_table_from_csv_sql("../users.csv", "users");
    load_table_from_csv_sql("../products.csv", "products");
    load_table_from_csv_sql("../orders.csv", "orders");
}

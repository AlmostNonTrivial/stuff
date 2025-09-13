#include "arena.hpp"
#include "catalog.hpp"
#include "common.hpp"
#include "repl.hpp"
#include <cstdio>
#include <cstring>

void print_usage(const char* program_name)
{
    printf("Usage: %s [database_file]\n", program_name);
    printf("  database_file: Path to the database file (default: relational_test.db)\n");
    printf("\nExamples:\n");
    printf("  %s                    # Use default database\n", program_name);
    printf("  %s mydata.db          # Use custom database\n", program_name);
    printf("  %s /path/to/data.db   # Use database at specific path\n", program_name);
}

int main(int argc, char** argv)
{
    arena<query_arena>::init();
    arena<global_arena>::init();
    arena<catalog_arena>::init();


    const char* database_path = "relational_test.db";

    if (argc > 2)
    {
        print_usage(argv[0]);
        return 1;
    }
    else if (argc == 2)
    {

        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
        {
            print_usage(argv[0]);
            return 0;
        }
        database_path = argv[1];
    }


    return run_repl(database_path);
}

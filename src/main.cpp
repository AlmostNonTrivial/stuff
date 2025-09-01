#include "btree_tests.hpp"
#include "defs.hpp"
#include "pager_tests.hpp"
#include "parser_tests.hpp"
#include "intergration_tests.hpp"
int
main()
{
	init_type_ops();

	test_btree();
	// test_parser();
	// test_pager();
	// test_integration();
	return 0;
}

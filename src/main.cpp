#include "tests_memtree.hpp"
#include "tests_blob.hpp"
#include "defs.hpp"

int
main()
{
	init_type_ops();

	test_blob();
	// test_memtree();
	// test_parser();
	// test_pager();
	// test_integration();
	return 0;
}

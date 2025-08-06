#include "pager.hpp"

#include <iostream>
#include <vector>
#include <set>
#include <map>
#include <string>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <random>

struct TestData {
    unsigned int index;
    char data[32];
    int value;
    char padding[PAGE_SIZE - sizeof(unsigned int) - 32 - sizeof(int)];
};

static int failure_count = 0;
static std::set<unsigned int> allocated_pages;
static std::set<unsigned int> freed_pages;


static std::map<unsigned int, TestData> capture_accessible_pages() {
    std::map<unsigned int, TestData> accessible_pages;

    unsigned int total_pages;
    unsigned int x;
    unsigned int y;
    unsigned int z;
    pager_get_stats(&total_pages, &x, &y, &z);

    // Try to access each page that should exist
    for (unsigned int i = 1; i <= total_pages; i++) {
        void* data = pager_get(i);
        if (data) {  // Page is accessible (not freed)
            accessible_pages[i] = *(TestData*)data;
        }
    }

    return accessible_pages;
}

static bool accessible_pages_equal(const std::map<unsigned int, TestData>& a,
                                  const std::map<unsigned int, TestData>& b) {
    if (a.size() != b.size()) return false;

    for (const auto& [page_id, data] : a) {
        auto it = b.find(page_id);
        if (it == b.end()) return false;

        const TestData& other = it->second;
        if (data.index != other.index ||
            strcmp(data.data, other.data) != 0 ||
            data.value != other.value) {
            return false;
        }
    }
    return true;
}

static bool assert_condition(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        failure_count++;
        exit(1);
        return false;
    }
    return true;
}

static void fill_test_data(TestData* data, unsigned int index, const std::string& data_str, int value) {
    data->index = index;
    strncpy(data->data, data_str.c_str(), 31);
    data->data[31] = '\0';
    data->value = value;
    memset(data->padding, 0xAB, sizeof(data->padding)); // Use pattern instead of zeros
}

static bool verify_test_data(const TestData* data, unsigned int expected_index,
                           const std::string& expected_data, int expected_value) {
    if (!data) return false;

    bool index_ok = (data->index == expected_index);
    bool data_ok = (strcmp(data->data, expected_data.c_str()) == 0);
    bool value_ok = (data->value == expected_value);

    if (!index_ok || !data_ok || !value_ok) {
        std::cerr << "Data verification failed for page " << expected_index
                  << ": got index=" << data->index << " data='" << data->data
                  << "' value=" << data->value << std::endl;
    }

    return index_ok && data_ok && value_ok;
}

// Clean up any leftover files
static void cleanup_test_files() {
    std::system("rm -f test_database.txt test_database.txt-journal");
}

int test_pager() {
    std::cout << "=== Comprehensive Pager Testing ===" << std::endl;

    failure_count = 0;
    allocated_pages.clear();
    freed_pages.clear();

    cleanup_test_files();

    // Test 0: Basic initialization and stats
    std::cout << "Test 0: Initialization and basic stats" << std::endl;
    pager_init("test_database.txt");

    unsigned int total_pages, free_pages, cached_pages, dirty_pages;
    pager_get_stats(&total_pages, &free_pages, &cached_pages, &dirty_pages);
    assert_condition(total_pages == 0, "Initial page counter should be 0 as root doesn't count");
    assert_condition(free_pages == 0, "Initial free list should be empty");
    assert_condition(cached_pages == 0, "No pages should be cached initially");
    assert_condition(dirty_pages == 0, "No pages should be dirty initially");

    // Test edge cases for get
    void* null_page = pager_get(0);
    assert_condition(null_page == nullptr, "Page 0 (root) should not be accessible");

    null_page = pager_get(999);
    assert_condition(null_page == nullptr, "Non-existent page should return null");

    // Test that operations outside transactions return 0
    unsigned int should_be_0 = pager_new();
    assert_condition(should_be_0 == 0, "pager_new outside transaction should return 0");

    // Test basic transaction for single allocation
    pager_begin_transaction();
    unsigned int first_page = pager_new();
    assert_condition(first_page != 0, "pager_new should succeed in transaction");
    pager_commit();

    // Test that we can't delete the root page (should be no-op)
    pager_begin_transaction();
    pager_delete(0); // Should be a no-op
    pager_commit();
    pager_get_stats(&total_pages, &free_pages, &cached_pages, &dirty_pages);
    assert_condition(total_pages == 1, "Root page should not be deletable");

    // Test file persistence - close and reopen
    pager_close();
    pager_init("test_database.txt");
    pager_get_stats(&total_pages, &free_pages, &cached_pages, &dirty_pages);
    assert_condition(total_pages == 1, "Page counter should persist across close/reopen");

    // Test 1: Sequential allocation and verification
    std::cout << "Test 1: Sequential allocation" << std::endl;

    pager_begin_transaction();
    std::vector<unsigned int> page_sequence;

    for (int i = 0; i < MAX_CACHE_ENTRIES - 1; i++) {
        unsigned int page_index = pager_new();
        assert_condition(page_index > 0, "Page index should be > 0");
        assert_condition(allocated_pages.find(page_index) == allocated_pages.end(),
                       "Page " + std::to_string(page_index) + " allocated twice");

        allocated_pages.insert(page_index);
        page_sequence.push_back(page_index);

        void* page_data = pager_get(page_index);
        assert_condition(page_data != nullptr, "Newly allocated page should not be null");

        TestData* test_data = (TestData*)page_data;
        fill_test_data(test_data, page_index, "seq_" + std::to_string(i), i * 10);
        pager_mark_dirty(page_index);
    }

    // Test that allocated pages are accessible within transaction
    for (unsigned int page_index : page_sequence) {
        void* data = pager_get(page_index);
        assert_condition(data != nullptr, "Allocated page should remain accessible");
    }

    // Test allocation counter increments properly
    unsigned int last_allocated = pager_new();
    assert_condition(last_allocated == MAX_CACHE_ENTRIES + 1,
        "Page counter should increment sequentially on fresh database");

    pager_commit();

    // Verify pages are sequential
    bool sequential = true;
    for (size_t i = 1; i < page_sequence.size(); i++) {
        if (page_sequence[i] <= page_sequence[i-1]) {
            sequential = false;
            break;
        }
    }
    assert_condition(sequential, "Pages should be sequential on first allocation");

    pager_get_stats(&total_pages, &free_pages, &cached_pages, &dirty_pages);
    assert_condition(total_pages == MAX_CACHE_ENTRIES + 1, "Page counter should include all allocated pages");

    // Test 2: Data integrity across cache operations
    std::cout << "Test 2: Data integrity verification" << std::endl;

    // Force some cache evictions by allocating more than cache size
    pager_begin_transaction();
    std::vector<unsigned int> overflow_pages;
    for (int i = 0; i < MAX_CACHE_ENTRIES + 10; i++) {
        unsigned int page_index = pager_new();
        overflow_pages.push_back(page_index);

        TestData* data = (TestData*)pager_get(page_index);
        fill_test_data(data, page_index, "overflow_" + std::to_string(i), i * 100);
        pager_mark_dirty(page_index);
    }
    pager_commit();

    // Verify ALL original pages still have correct data (forces reload from disk)
    for (size_t i = 0; i < page_sequence.size(); i++) {
        unsigned int page_index = page_sequence[i];
        TestData* data = (TestData*)pager_get(page_index);
        assert_condition(verify_test_data(data, page_index, "seq_" + std::to_string(i), i * 10),
                       "Page " + std::to_string(page_index) + " data corrupted");
    }

    // Test 3: Free list management and reuse
    std::cout << "Test 3: Free list stress test" << std::endl;

    // Free a bunch of pages in random order
    std::vector<unsigned int> pages_to_free = overflow_pages;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(pages_to_free.begin(), pages_to_free.end(), gen);

    pager_begin_transaction();
    for (size_t i = 0; i < pages_to_free.size() / 2; i++) {
        unsigned int page_index = pages_to_free[i];
        pager_delete(page_index);
        freed_pages.insert(page_index);

        // Verify page is no longer accessible
        void* deleted_page = pager_get(page_index);
        assert_condition(deleted_page == nullptr,
                       "Deleted page " + std::to_string(page_index) + " should not be accessible");
    }
    pager_commit();

    pager_get_stats(&total_pages, &free_pages, &cached_pages, &dirty_pages);
    assert_condition(free_pages == pages_to_free.size() / 2, "Free list size should match deleted pages");

    // Reallocate and verify reuse
    pager_begin_transaction();
    std::set<unsigned int> reused_pages;
    for (size_t i = 0; i < freed_pages.size(); i++) {
        unsigned int page_index = pager_new();
        assert_condition(freed_pages.find(page_index) != freed_pages.end(),
                       "New page should reuse freed page");
        reused_pages.insert(page_index);
    }
    pager_commit();

    pager_get_stats(&total_pages, &free_pages, &cached_pages, &dirty_pages);
    assert_condition(free_pages == 0, "Free list should be empty after reuse");

    // Test 3a: Free list persistence across transactions
    std::cout << "Test 3a: Free list transaction handling" << std::endl;

    pager_begin_transaction();

    // Free some pages in transaction
    std::set<unsigned int> txn_freed_pages;
    for (size_t i = pages_to_free.size() / 2; i < pages_to_free.size() * 3/4; i++) {
        pager_delete(pages_to_free[i]);
        txn_freed_pages.insert(pages_to_free[i]);
    }

    // Verify they're immediately unavailable
    for (unsigned int page : txn_freed_pages) {
        void* data = pager_get(page);
        assert_condition(data == nullptr, "Deleted page in transaction should be inaccessible");
    }

    // Allocate new pages - CAN reuse transaction-freed pages
    std::set<unsigned int> new_in_txn;
    for (size_t i = 0; i < txn_freed_pages.size(); i++) {
        unsigned int new_page = pager_new();
        new_in_txn.insert(new_page);
    }

    // Verify we got the freed pages back
    size_t reuse_count = 0;
    for (auto page : new_in_txn) {
        if (txn_freed_pages.find(page) != txn_freed_pages.end()) {
            reuse_count++;
        }
    }
    assert_condition(reuse_count > 0, "Should be able to reuse pages freed in same transaction");

    pager_commit();

    // Test 3a-extended: Transaction rollback with freed and reused pages
    std::cout << "Test 3a-ext: Rollback with freed/reused pages" << std::endl;

    // Create some pages to work with
    pager_begin_transaction();
    std::vector<unsigned int> rollback_test_pages;
    std::map<unsigned int, std::string> pre_txn_data;
    for (int i = 0; i < 10; i++) {
        unsigned int page = pager_new();
        rollback_test_pages.push_back(page);
        TestData* data = (TestData*)pager_get(page);
        std::string content = "pre_txn_" + std::to_string(i);
        fill_test_data(data, page, content, i * 111);
        pager_mark_dirty(page);
        pre_txn_data[page] = content;
    }
    pager_commit();
    pager_sync();

    pager_begin_transaction();

    // Free half the pages
    std::set<unsigned int> freed_in_txn;
    for (size_t i = 0; i < rollback_test_pages.size() / 2; i++) {
        pager_delete(rollback_test_pages[i]);
        freed_in_txn.insert(rollback_test_pages[i]);
    }

    // Reuse those pages with new data
    std::vector<unsigned int> reused_in_txn;
    for (size_t i = 0; i < freed_in_txn.size(); i++) {
        unsigned int reused = pager_new();
        reused_in_txn.push_back(reused);
        TestData* data = (TestData*)pager_get(reused);
        fill_test_data(data, reused, "reused_in_txn_" + std::to_string(i), i * 222);
        pager_mark_dirty(reused);
    }

    pager_sync();
    pager_rollback();

    // After rollback, original pages should be back with original data
    for (auto page : rollback_test_pages) {
        TestData* data = (TestData*)pager_get(page);
        assert_condition(data != nullptr, "Original page should exist after rollback");
        assert_condition(strcmp(data->data, pre_txn_data[page].c_str()) == 0,
            "Original data should be restored after rollback");
    }

    // Test 3a-commit: Commit with freed/reused pages
    std::cout << "Test 3a-commit: Commit with freed/reused pages" << std::endl;

    // Create initial pages
    pager_begin_transaction();
    std::vector<unsigned int> commit_test_pages;
    for (int i = 0; i < 10; i++) {
        commit_test_pages.push_back(pager_new());
    }
    pager_commit();

    pager_begin_transaction();

    // Free and immediately reuse
    std::set<unsigned int> to_free;
    for (size_t i = 0; i < commit_test_pages.size() / 2; i++) {
        to_free.insert(commit_test_pages[i]);
        pager_delete(commit_test_pages[i]);
    }

    // Reuse with new data
    std::map<unsigned int, std::string> new_data;
    for (size_t i = 0; i < to_free.size(); i++) {
        unsigned int reused = pager_new();
        TestData* data = (TestData*)pager_get(reused);
        std::string content = "committed_reuse_" + std::to_string(i);
        fill_test_data(data, reused, content, i * 333);
        pager_mark_dirty(reused);
        new_data[reused] = content;
    }

    pager_commit();

    // Verify commit persisted the reused pages
    for (const auto& [page, content] : new_data) {
        TestData* data = (TestData*)pager_get(page);
        assert_condition(data != nullptr, "Reused page should exist after commit");
        assert_condition(strcmp(data->data, content.c_str()) == 0,
            "Reused page data should persist after commit");
    }

    // Verify original non-freed pages still exist
    for (size_t i = commit_test_pages.size() / 2; i < commit_test_pages.size(); i++) {
        void* data = pager_get(commit_test_pages[i]);
        assert_condition(data != nullptr, "Non-freed pages should still exist");
    }

    // Test 3b: Free page list integrity with many pages
    std::cout << "Test 3b: Free page list overflow handling" << std::endl;

    const int MASS_FREE_COUNT = FREE_PAGES_PER_FREE_PAGE * 3; // Force multiple free pages

    pager_begin_transaction();
    std::vector<unsigned int> mass_pages;
    for (int i = 0; i < MASS_FREE_COUNT; i++) {
        mass_pages.push_back(pager_new());
    }
    pager_commit();
    pager_get_stats(&total_pages, &free_pages, &cached_pages, &dirty_pages);
    pager_begin_transaction();
    for (auto page : mass_pages) {
        pager_delete(page);
    }
    pager_commit();

    pager_get_stats(&total_pages, &free_pages, &cached_pages, &dirty_pages);
    assert_condition(free_pages >= MASS_FREE_COUNT,
        "All freed pages should be in free list");

    // Reallocate all and verify
    pager_begin_transaction();
    for (int i = 0; i < MASS_FREE_COUNT; i++) {
        unsigned int page = pager_new();
        assert_condition(page != 0, "Should successfully reallocate all freed pages");
    }
    pager_commit();

    // Test 4: Transaction rollback edge cases
    std::cout << "Test 4: Transaction rollback edge cases" << std::endl;

    // Setup: Create some pages and save their state
    pager_begin_transaction();
    std::vector<unsigned int> txn_test_pages;
    std::map<unsigned int, TestData> original_state;

    for (int i = 0; i < 10; i++) {
        unsigned int page_index = pager_new();
        txn_test_pages.push_back(page_index);

        TestData* data = (TestData*)pager_get(page_index);
        fill_test_data(data, page_index, "original_" + std::to_string(i), i * 50);
        pager_mark_dirty(page_index);
        original_state[page_index] = *data;
    }
    pager_commit();
    pager_sync(); // Ensure original state is on disk

    // Test rollback with new pages in transaction
    pager_begin_transaction();

    std::vector<unsigned int> new_pages_in_txn;
    for (int i = 0; i < 5; i++) {
        unsigned int page_index = pager_new();
        new_pages_in_txn.push_back(page_index);

        TestData* data = (TestData*)pager_get(page_index);
        fill_test_data(data, page_index, "new_in_txn_" + std::to_string(i), i * 777);
        pager_mark_dirty(page_index);
    }

    // Modify existing pages
    for (unsigned int page_index : txn_test_pages) {
        void* page_data = pager_get(page_index);
        pager_mark_dirty(page_index); // Journal original BEFORE modifying

        TestData* data = (TestData*)page_data;
        fill_test_data(data, page_index, "modified_in_txn", 999);
    }

    // Verify transaction state
    for (unsigned int page_index : new_pages_in_txn) {
        TestData* data = (TestData*)pager_get(page_index);
        assert_condition(data != nullptr, "New page in transaction should be accessible");
    }

    // Rollback
    pager_rollback();

    // Verify new pages are gone
    for (unsigned int page_index : new_pages_in_txn) {
        void* data = pager_get(page_index);
        assert_condition(data == nullptr,
                       "New page " + std::to_string(page_index) + " should be gone after rollback");
    }

    // Verify original pages restored
    for (const auto& pair : original_state) {
        unsigned int page_index = pair.first;
        const TestData& original = pair.second;

        TestData* current = (TestData*)pager_get(page_index);
        assert_condition(verify_test_data(current, original.index, original.data, original.value),
                       "Page " + std::to_string(page_index) + " not properly restored after rollback");
    }

    // Test 4a: Rollback with free page list changes
    std::cout << "Test 4a: Rollback with free page list changes" << std::endl;


    // Setup: Create pages and free some
    pager_begin_transaction();

    std::vector<unsigned int> setup_pages;
    for (int i = 0; i < 20; i++) {
        setup_pages.push_back(pager_new());
    }
    pager_commit();


    // Free every other page
    pager_begin_transaction();
    for (size_t i = 0; i < setup_pages.size(); i += 2) {
        pager_delete(setup_pages[i]);
    }
    pager_commit();

    unsigned int free_before;
    pager_get_stats(&total_pages, &free_before, &cached_pages, &dirty_pages);

    pager_begin_transaction();
    auto x = capture_accessible_pages();

    // Free more pages in transaction
    for (size_t i = 1; i < setup_pages.size(); i += 2) {
        pager_delete(setup_pages[i]);
    }

    // Allocate some back
    std::vector<unsigned int> realloc_in_txn;
    for (int i = 0; i < 5; i++) {
        realloc_in_txn.push_back(pager_new());
    }

    auto z  = capture_accessible_pages();
    pager_rollback();
    auto y  = capture_accessible_pages();
    assert_condition(accessible_pages_equal(x, y) && !accessible_pages_equal(x, z), "Not equal");

    // Verify free list restored
    // unsigned int free_after;
    // pager_get_stats(&total_pages, &free_after, &cached_pages, &dirty_pages);
    // assert_condition(free_after == free_before, "Free list should be restored after rollback");

    // Verify transaction-allocated pages are gone
    // for (auto page : realloc_in_txn) {
    //     void* data = pager_get(page);
    // }

    // Test 4b: Transaction state management
    std::cout << "Test 4b: Transaction state management" << std::endl;

    pager_begin_transaction();
    pager_begin_transaction(); // Should be idempotent or error

    unsigned int txn_page = pager_new();
    TestData* txn_data = (TestData*)pager_get(txn_page);
    fill_test_data(txn_data, txn_page, "nested_txn", 12345);
    pager_mark_dirty(txn_page);

    pager_commit();
    pager_commit(); // Should be safe

    // Verify page persisted
    TestData* verify = (TestData*)pager_get(txn_page);
    assert_condition(verify_test_data(verify, txn_page, "nested_txn", 12345),
        "Transaction should commit properly despite multiple begin/commit calls");

    // Test 4c: Complex rollback scenario
    std::cout << "Test 4c: Complex rollback scenario" << std::endl;

    // Create initial state
    pager_begin_transaction();
    std::map<unsigned int, std::string> initial_state_complex;
    std::vector<unsigned int> test_pages;
    for (int i = 0; i < 10; i++) {
        unsigned int page = pager_new();
        test_pages.push_back(page);
        TestData* data = (TestData*)pager_get(page);
        std::string content = "initial_" + std::to_string(i);
        fill_test_data(data, page, content, i * 100);
        pager_mark_dirty(page);
        initial_state_complex[page] = content;
    }
    pager_commit();
    pager_sync();

    pager_begin_transaction();

    // Modify some pages multiple times
    for (int round = 0; round < 3; round++) {
        for (size_t i = 0; i < test_pages.size() / 2; i++) {
            unsigned int page = test_pages[i];
            void* data = pager_get(page);
            pager_mark_dirty(page); // Should journal only once
            TestData* td = (TestData*)data;
            fill_test_data(td, page, "round_" + std::to_string(round), round);
        }
    }

    // Delete some original pages
    for (size_t i = test_pages.size() / 2; i < test_pages.size() * 3/4; i++) {
        pager_delete(test_pages[i]);
    }

    // Create new pages
    for (int i = 0; i < 5; i++) {
        unsigned int new_page = pager_new();
        TestData* data = (TestData*)pager_get(new_page);
        fill_test_data(data, new_page, "new_in_complex_txn", 9999);
        pager_mark_dirty(new_page);
    }

    pager_rollback();

    // Verify all original pages restored
    for (const auto& [page, content] : initial_state_complex) {
        TestData* data = (TestData*)pager_get(page);
        if (page < test_pages[test_pages.size() * 3/4]) {
            assert_condition(data != nullptr, "Original page should exist after rollback");
            assert_condition(strcmp(data->data, content.c_str()) == 0,
                "Page content should be restored to pre-transaction state");
        }
    }

    // Test 4c-extra: Free-reuse-free within transaction
    std::cout << "Test 4c-extra: Free-reuse-free cycle in transaction" << std::endl;

    // Create test page
    pager_begin_transaction();
    unsigned int cycle_page = pager_new();
    TestData* data = (TestData*)pager_get(cycle_page);
    fill_test_data(data, cycle_page, "original_cycle", 444);
    pager_mark_dirty(cycle_page);
    pager_commit();
    pager_sync();

    pager_begin_transaction();

    // Free the page
    pager_delete(cycle_page);
    assert_condition(pager_get(cycle_page) == nullptr, "Freed page should be inaccessible");

    // Try to reuse - should get the same page back
    unsigned int reused = pager_new();
    assert_condition(reused == cycle_page, "Should reuse the just-freed page");

    data = (TestData*)pager_get(reused);
    fill_test_data(data, reused, "reused_cycle", 555);
    pager_mark_dirty(reused);

    // Free it again
    pager_delete(reused);
    assert_condition(pager_get(reused) == nullptr, "Re-freed page should be inaccessible");

    // Reuse again
    unsigned int reused2 = pager_new();
    assert_condition(reused2 == cycle_page, "Should reuse the same page again");

    data = (TestData*)pager_get(reused2);
    fill_test_data(data, reused2, "final_cycle", 666);
    pager_mark_dirty(reused2);

    pager_rollback();

    // After rollback, should have original data
    data = (TestData*)pager_get(cycle_page);
    assert_condition(data != nullptr, "Original page should exist after rollback");
    assert_condition(verify_test_data(data, cycle_page, "original_cycle", 444),
        "Original data should be restored after complex free/reuse cycle");

    // Test 5: Transaction commit with mixed operations
    std::cout << "Test 5: Transaction commit stress" << std::endl;

    pager_begin_transaction();

    // Mix of new pages, modifications, and deletions
    std::vector<unsigned int> commit_new_pages;
    std::map<unsigned int, TestData> commit_expected;

    // Create new pages
    for (int i = 0; i < 3; i++) {
        unsigned int page_index = pager_new();
        commit_new_pages.push_back(page_index);

        TestData* data = (TestData*)pager_get(page_index);
        fill_test_data(data, page_index, "commit_new_" + std::to_string(i), i * 123);
        pager_mark_dirty(page_index);
        commit_expected[page_index] = *data;
    }

    // Modify existing pages
    for (size_t i = 0; i < std::min(txn_test_pages.size(), size_t(5)); i++) {
        unsigned int page_index = txn_test_pages[i];
        void* page_data = pager_get(page_index);
        pager_mark_dirty(page_index);

        TestData* data = (TestData*)page_data;
        fill_test_data(data, page_index, "commit_mod_" + std::to_string(i), i * 456);
        commit_expected[page_index] = *data;
    }

    pager_commit();

    // Verify all changes persisted
    for (const auto& pair : commit_expected) {
        unsigned int page_index = pair.first;
        const TestData& expected = pair.second;

        TestData* current = (TestData*)pager_get(page_index);
        assert_condition(verify_test_data(current, expected.index, expected.data, expected.value),
                       "Committed page " + std::to_string(page_index) + " data incorrect");
    }

    // Test 6: Persistence across close/reopen
    std::cout << "Test 6: Persistence verification" << std::endl;

    // Select some pages to verify persistence
    std::map<unsigned int, TestData> persistence_test;
    int count = 0;
    for (const auto& pair : commit_expected) {
        if (count++ < 5) {
            persistence_test[pair.first] = pair.second;
        }
    }

    pager_close();
    pager_init("test_database.txt");

    // Verify data survived close/reopen
    for (const auto& pair : persistence_test) {
        unsigned int page_index = pair.first;
        const TestData& expected = pair.second;

        TestData* current = (TestData*)pager_get(page_index);
        assert_condition(verify_test_data(current, expected.index, expected.data, expected.value),
                       "Page " + std::to_string(page_index) + " did not persist across close/reopen");
    }

    // Test 7: Error conditions and edge cases
    std::cout << "Test 7: Error conditions" << std::endl;

    // Test multiple begin_transaction calls
    pager_begin_transaction();
    pager_begin_transaction(); // Should be safe to call multiple times

    // Test rollback without transaction
    pager_rollback();
    pager_rollback(); // Should be safe

    // Test commit without transaction
    pager_commit(); // Should be safe

    // Test mark_dirty on non-existent page (should not crash)
    pager_begin_transaction();
    pager_mark_dirty(999999);
    pager_commit();

    // Test delete on non-existent page (should not crash)
    pager_begin_transaction();
    pager_delete(999999);
    pager_commit();

    // Test accessing page after delete
    pager_begin_transaction();
    unsigned int delete_test_page = pager_new();
    TestData* delete_data = (TestData*)pager_get(delete_test_page);
    assert_condition(delete_data != nullptr, "New page should be accessible");

    pager_delete(delete_test_page);
    delete_data = (TestData*)pager_get(delete_test_page);
    assert_condition(delete_data == nullptr, "Deleted page should not be accessible");
    pager_commit();

    // Test 8: Cache behavior under pressure
    std::cout << "Test 8: Cache pressure test" << std::endl;

    // Allocate way more pages than cache can hold
    pager_begin_transaction();
    std::vector<unsigned int> pressure_pages;
    const int PRESSURE_COUNT = MAX_CACHE_ENTRIES * 3;

    for (int i = 0; i < PRESSURE_COUNT; i++) {
        unsigned int page_index = pager_new();
        pressure_pages.push_back(page_index);

        TestData* data = (TestData*)pager_get(page_index);
        fill_test_data(data, page_index, "pressure_" + std::to_string(i), i * 321);
        pager_mark_dirty(page_index);

        // Occasionally sync to test mixed dirty/clean state
        if (i % 20 == 0) {
            pager_sync();
        }
    }
    pager_commit();

    // Random access pattern to stress LRU
    std::vector<unsigned int> access_pattern = pressure_pages;
    std::shuffle(access_pattern.begin(), access_pattern.end(), gen);

    for (size_t i = 0; i < std::min(access_pattern.size(), size_t(100)); i++) {
        unsigned int page_index = access_pattern[i];
        TestData* data = (TestData*)pager_get(page_index);
        assert_condition(data != nullptr, "Page should be accessible under pressure");

        // Verify data integrity
        auto it = std::find(pressure_pages.begin(), pressure_pages.end(), page_index);
        if (it != pressure_pages.end()) {
            size_t original_index = it - pressure_pages.begin();
            std::string expected_data = "pressure_" + std::to_string(original_index);
            assert_condition(strcmp(data->data, expected_data.c_str()) == 0,
                           "Data corrupted under cache pressure for page " + std::to_string(page_index));
        }
    }

    pager_get_stats(&total_pages, &free_pages, &cached_pages, &dirty_pages);
    assert_condition(cached_pages <= MAX_CACHE_ENTRIES, "Cache should not exceed maximum");

    // Test 8a: Cache pressure during transaction
    std::cout << "Test 8a: Cache pressure during transaction" << std::endl;

    pager_begin_transaction();

    // Create many pages in transaction
    std::vector<unsigned int> txn_pressure_pages;
    for (int i = 0; i < MAX_CACHE_ENTRIES * 2; i++) {
        unsigned int page = pager_new();
        txn_pressure_pages.push_back(page);
        TestData* data = (TestData*)pager_get(page);
        fill_test_data(data, page, "txn_pressure_" + std::to_string(i), i);
        pager_mark_dirty(page);
    }

    // Access them randomly to stress cache during transaction
    for (int i = 0; i < 100; i++) {
        unsigned int page = txn_pressure_pages[rand() % txn_pressure_pages.size()];
        TestData* data = (TestData*)pager_get(page);
        assert_condition(data != nullptr, "Transaction pages should remain accessible");
    }

    // Rollback and verify all gone
    pager_rollback();
    for (auto page : txn_pressure_pages) {
        void* data = pager_get(page);
        assert_condition(data == nullptr, "Rolled back pages should not exist");
    }

    // Test 9: Final consistency check
    std::cout << "Test 9: Final consistency" << std::endl;

    pager_sync();
    pager_get_stats(&total_pages, &free_pages, &cached_pages, &dirty_pages);
    assert_condition(dirty_pages == 0, "No pages should be dirty after final sync");

    // Clean up pressure test pages
    pager_begin_transaction();
    for (unsigned int page_index : pressure_pages) {
        pager_delete(page_index);
    }
    pager_commit();

    pager_sync();
    pager_close();

    // Final report
    if (failure_count == 0) {
        std::cout << "All passed" << std::endl;
    } else {
        std::cout << "❌ " << failure_count << " TESTS FAILED" << std::endl;
    }

    cleanup_test_files();
    std::cout << "=== Test Complete ===" << std::endl;
    return failure_count;
}

/* main.cpp */
int main() {
    int result = test_pager();
    return result == 0 ? 0 : 1;
}

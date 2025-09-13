#pragma once

void
load_all_data_sql();
void
create_all_tables_sql();
void
demo_like_pattern(const char *args);
void
demo_nested_loop_join(const char *args);
// Demo 3: Subquery pattern
// Usage: .demo_subquery [age] [city]
void
demo_subquery_pattern(const char *args);
// Demo 4: Composite index performance comparison
// Usage: .demo_index [user_id] [min_order_id]
void
demo_composite_index(const char *args);
// Demo 5: GROUP BY with aggregation
// Usage: .demo_group [show_avg]
void
demo_group_by_aggregate(const char *args);

void
demo_blob_storage(const char *args);

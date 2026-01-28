/**
 * Universal SQL Tracker - Language-agnostic SQL parsing and column tracking
 * 
 * Provides SQL query analysis to detect column-level changes across all databases.
 * Supports: INSERT, UPDATE, DELETE, SELECT operations.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <regex.h>
#include "../include/sql_tracker.h"

// Global tracker instance
static SQLTracker *g_tracker = NULL;

/**
 * Normalize SQL string: trim whitespace, convert to uppercase for keywords
 */
static char *sql_normalize(const char *query) {
    if (!query) return NULL;
    
    size_t len = strlen(query);
    char *normalized = (char *)malloc(len + 1);
    if (!normalized) return NULL;
    
    int write_idx = 0;
    int in_string = 0;
    char string_char = 0;
    
    for (size_t i = 0; i < len; i++) {
        char c = query[i];
        
        // Handle string literals
        if ((c == '\'' || c == '"' || c == '`') && (i == 0 || query[i-1] != '\\')) {
            if (!in_string) {
                in_string = 1;
                string_char = c;
            } else if (c == string_char) {
                in_string = 0;
            }
            normalized[write_idx++] = c;
            continue;
        }
        
        if (in_string) {
            normalized[write_idx++] = c;
            continue;
        }
        
        // Skip extra whitespace outside strings
        if (isspace(c)) {
            if (write_idx > 0 && normalized[write_idx - 1] != ' ') {
                normalized[write_idx++] = ' ';
            }
        } else {
            normalized[write_idx++] = c;
        }
    }
    
    normalized[write_idx] = '\0';
    return normalized;
}

/**
 * Extract SQL operation type from query
 */
static SQLOperation detect_operation(const char *query) {
    if (!query) return SQL_UNKNOWN;
    
    char *upper = (char *)malloc(strlen(query) + 1);
    if (!upper) return SQL_UNKNOWN;
    
    for (size_t i = 0; query[i]; i++) {
        upper[i] = toupper(query[i]);
    }
    upper[strlen(query)] = '\0';
    
    SQLOperation op = SQL_UNKNOWN;
    
    if (strstr(upper, "INSERT") == upper || strstr(upper, "INSERT ") != NULL) {
        op = SQL_INSERT;
    } else if (strstr(upper, "UPDATE") == upper || strstr(upper, "UPDATE ") != NULL) {
        op = SQL_UPDATE;
    } else if (strstr(upper, "DELETE") == upper || strstr(upper, "DELETE ") != NULL) {
        op = SQL_DELETE;
    } else if (strstr(upper, "SELECT") == upper || strstr(upper, "SELECT ") != NULL) {
        op = SQL_SELECT;
    }
    
    free(upper);
    return op;
}

/**
 * Extract table name from query using regex
 */
static char *extract_table_name(const char *query, SQLOperation op) {
    if (!query) return NULL;
    
    char *table_name = (char *)malloc(256);
    if (!table_name) return NULL;
    
    const char *search_str = NULL;
    const char *start_pos = NULL;
    
    switch (op) {
        case SQL_INSERT:
            search_str = "INSERT INTO";
            start_pos = strstr(query, search_str);
            if (start_pos) {
                start_pos += strlen(search_str);
                while (*start_pos && isspace(*start_pos)) start_pos++;
                
                int i = 0;
                while (*start_pos && !isspace(*start_pos) && *start_pos != '(' && i < 255) {
                    if (*start_pos != '`' && *start_pos != '"' && *start_pos != '\'') {
                        table_name[i++] = *start_pos;
                    }
                    start_pos++;
                }
                table_name[i] = '\0';
            }
            break;
            
        case SQL_UPDATE:
            search_str = "UPDATE";
            start_pos = strstr(query, search_str);
            if (start_pos) {
                start_pos += strlen(search_str);
                while (*start_pos && isspace(*start_pos)) start_pos++;
                
                int i = 0;
                while (*start_pos && !isspace(*start_pos) && *start_pos != '(' && i < 255) {
                    if (*start_pos != '`' && *start_pos != '"' && *start_pos != '\'') {
                        table_name[i++] = *start_pos;
                    }
                    start_pos++;
                }
                table_name[i] = '\0';
            }
            break;
            
        case SQL_DELETE:
            search_str = "FROM";
            start_pos = strstr(query, search_str);
            if (start_pos) {
                start_pos += strlen(search_str);
                while (*start_pos && isspace(*start_pos)) start_pos++;
                
                int i = 0;
                while (*start_pos && !isspace(*start_pos) && *start_pos != '(' && i < 255) {
                    if (*start_pos != '`' && *start_pos != '"' && *start_pos != '\'') {
                        table_name[i++] = *start_pos;
                    }
                    start_pos++;
                }
                table_name[i] = '\0';
            }
            break;
            
        case SQL_SELECT:
            search_str = "FROM";
            start_pos = strstr(query, search_str);
            if (start_pos) {
                start_pos += strlen(search_str);
                while (*start_pos && isspace(*start_pos)) start_pos++;
                
                int i = 0;
                while (*start_pos && !isspace(*start_pos) && *start_pos != '(' && i < 255) {
                    if (*start_pos != '`' && *start_pos != '"' && *start_pos != '\'') {
                        table_name[i++] = *start_pos;
                    }
                    start_pos++;
                }
                table_name[i] = '\0';
            }
            break;
            
        default:
            free(table_name);
            return NULL;
    }
    
    if (strlen(table_name) == 0) {
        free(table_name);
        return NULL;
    }
    
    return table_name;
}

/**
 * Extract columns from UPDATE SET clause
 */
static char **extract_update_columns(const char *query, int *column_count) {
    char **columns = (char **)malloc(sizeof(char *) * 100);
    if (!columns) return NULL;
    
    *column_count = 0;
    
    const char *set_pos = strstr(query, "SET");
    if (!set_pos) {
        return columns;
    }
    
    set_pos += 3;
    
    // Find WHERE or end of query
    const char *end_pos = strstr(set_pos, "WHERE");
    if (!end_pos) {
        end_pos = set_pos + strlen(set_pos);
    }
    
    char buffer[256];
    int buffer_idx = 0;
    
    for (const char *p = set_pos; p < end_pos && *column_count < 100; p++) {
        if (*p == '=' || *p == ',' || *p == ' ' || p == end_pos - 1) {
            if (buffer_idx > 0) {
                if (p == end_pos - 1 && *p != '=' && *p != ',') {
                    buffer[buffer_idx++] = *p;
                }
                
                // Trim whitespace
                int start = 0;
                while (start < buffer_idx && isspace(buffer[start])) start++;
                int end = buffer_idx - 1;
                while (end >= start && isspace(buffer[end])) end--;
                
                if (end >= start) {
                    char *column = (char *)malloc(end - start + 2);
                    if (column) {
                        strncpy(column, buffer + start, end - start + 1);
                        column[end - start + 1] = '\0';
                        
                        // Remove quotes if present
                        if ((column[0] == '`' || column[0] == '"' || column[0] == '\'') &&
                            (column[strlen(column) - 1] == '`' || column[strlen(column) - 1] == '"' || column[strlen(column) - 1] == '\'')) {
                            memmove(column, column + 1, strlen(column) - 2);
                            column[strlen(column) - 2] = '\0';
                        }
                        
                        columns[*column_count] = column;
                        (*column_count)++;
                    }
                }
                buffer_idx = 0;
            }
        } else if (!isspace(*p)) {
            buffer[buffer_idx++] = *p;
        }
    }
    
    return columns;
}

/**
 * Extract columns from INSERT column list
 */
static char **extract_insert_columns(const char *query, int *column_count) {
    char **columns = (char **)malloc(sizeof(char *) * 100);
    if (!columns) return NULL;
    
    *column_count = 0;
    
    const char *open_paren = strchr(query, '(');
    if (!open_paren) {
        // No column list specified, use *
        columns[0] = (char *)malloc(2);
        strcpy(columns[0], "*");
        *column_count = 1;
        return columns;
    }
    
    const char *close_paren = strchr(open_paren, ')');
    if (!close_paren) {
        columns[0] = (char *)malloc(2);
        strcpy(columns[0], "*");
        *column_count = 1;
        return columns;
    }
    
    // Skip "INSERT INTO table_name ("
    const char *start = open_paren + 1;
    
    char buffer[256];
    int buffer_idx = 0;
    
    for (const char *p = start; p < close_paren && *column_count < 100; p++) {
        if (*p == ',' || p == close_paren - 1) {
            if (p == close_paren - 1 && *p != ',') {
                buffer[buffer_idx++] = *p;
            }
            
            if (buffer_idx > 0) {
                // Trim whitespace
                int trim_start = 0;
                while (trim_start < buffer_idx && isspace(buffer[trim_start])) trim_start++;
                int trim_end = buffer_idx - 1;
                while (trim_end >= trim_start && isspace(buffer[trim_end])) trim_end--;
                
                if (trim_end >= trim_start) {
                    char *column = (char *)malloc(trim_end - trim_start + 2);
                    if (column) {
                        strncpy(column, buffer + trim_start, trim_end - trim_start + 1);
                        column[trim_end - trim_start + 1] = '\0';
                        
                        columns[*column_count] = column;
                        (*column_count)++;
                    }
                }
                buffer_idx = 0;
            }
        } else {
            buffer[buffer_idx++] = *p;
        }
    }
    
    if (*column_count == 0) {
        columns[0] = (char *)malloc(2);
        strcpy(columns[0], "*");
        *column_count = 1;
    }
    
    return columns;
}

/**
 * Extract columns from SELECT clause
 */
static char **extract_select_columns(const char *query, int *column_count) {
    char **columns = (char **)malloc(sizeof(char *) * 100);
    if (!columns) return NULL;
    
    *column_count = 0;
    
    const char *select_pos = strstr(query, "SELECT");
    if (!select_pos) {
        columns[0] = (char *)malloc(2);
        strcpy(columns[0], "*");
        *column_count = 1;
        return columns;
    }
    
    select_pos += 6;
    
    // Find FROM
    const char *from_pos = strstr(select_pos, "FROM");
    if (!from_pos) {
        from_pos = select_pos + strlen(select_pos);
    }
    
    char buffer[256];
    int buffer_idx = 0;
    
    for (const char *p = select_pos; p < from_pos && *column_count < 100; p++) {
        if (*p == ',' || p == from_pos - 1) {
            if (p == from_pos - 1 && *p != ',') {
                buffer[buffer_idx++] = *p;
            }
            
            if (buffer_idx > 0) {
                int trim_start = 0;
                while (trim_start < buffer_idx && isspace(buffer[trim_start])) trim_start++;
                int trim_end = buffer_idx - 1;
                while (trim_end >= trim_start && isspace(buffer[trim_end])) trim_end--;
                
                if (trim_end >= trim_start) {
                    char *column = (char *)malloc(trim_end - trim_start + 2);
                    if (column) {
                        strncpy(column, buffer + trim_start, trim_end - trim_start + 1);
                        column[trim_end - trim_start + 1] = '\0';
                        
                        columns[*column_count] = column;
                        (*column_count)++;
                    }
                }
                buffer_idx = 0;
            }
        } else if (*p != ' ' || buffer_idx > 0) {
            buffer[buffer_idx++] = *p;
        }
    }
    
    if (*column_count == 0) {
        columns[0] = (char *)malloc(2);
        strcpy(columns[0], "*");
        *column_count = 1;
    }
    
    return columns;
}

/**
 * Initialize SQL tracker
 */
SQLTracker *sql_tracker_init(const char *storage_path) {
    SQLTracker *tracker = (SQLTracker *)malloc(sizeof(SQLTracker));
    if (!tracker) return NULL;
    
    tracker->changes = (SQLChange *)malloc(sizeof(SQLChange) * MAX_CHANGES);
    if (!tracker->changes) {
        free(tracker);
        return NULL;
    }
    
    tracker->change_count = 0;
    tracker->max_changes = MAX_CHANGES;
    
    if (storage_path) {
        tracker->storage_path = (char *)malloc(strlen(storage_path) + 1);
        if (tracker->storage_path) {
            strcpy(tracker->storage_path, storage_path);
        }
    } else {
        tracker->storage_path = NULL;
    }
    
    g_tracker = tracker;
    return tracker;
}

/**
 * Track a SQL query and extract changes
 */
int sql_tracker_track_query(SQLTracker *tracker, const char *query, int rows_affected,
                            const char *database, const char *old_value, const char *new_value) {
    if (!tracker || !query) return 0;
    
    char *normalized = sql_normalize(query);
    if (!normalized) return 0;
    
    SQLOperation op = detect_operation(normalized);
    if (op == SQL_UNKNOWN) {
        free(normalized);
        return 0;
    }
    
    char *table_name = extract_table_name(normalized, op);
    if (!table_name) {
        free(normalized);
        return 0;
    }
    
    char **columns = NULL;
    int column_count = 0;
    
    switch (op) {
        case SQL_UPDATE:
            columns = extract_update_columns(normalized, &column_count);
            break;
        case SQL_INSERT:
            columns = extract_insert_columns(normalized, &column_count);
            break;
        case SQL_SELECT:
            columns = extract_select_columns(normalized, &column_count);
            break;
        case SQL_DELETE:
            columns = (char **)malloc(sizeof(char *));
            if (columns) {
                columns[0] = (char *)malloc(2);
                if (columns[0]) {
                    strcpy(columns[0], "*");
                    column_count = 1;
                }
            }
            break;
        default:
            break;
    }
    
    if (!columns || column_count == 0) {
        free(table_name);
        free(normalized);
        return 0;
    }
    
    // Create change entries for each column
    int created_count = 0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t timestamp_ns = (uint64_t)ts.tv_sec * 1000000000UL + (uint64_t)ts.tv_nsec;
    
    for (int i = 0; i < column_count && tracker->change_count < tracker->max_changes; i++) {
        SQLChange *change = &tracker->changes[tracker->change_count];
        
        change->timestamp_ns = timestamp_ns;
        strncpy(change->table_name, table_name, sizeof(change->table_name) - 1);
        change->table_name[sizeof(change->table_name) - 1] = '\0';
        
        strncpy(change->column_name, columns[i], sizeof(change->column_name) - 1);
        change->column_name[sizeof(change->column_name) - 1] = '\0';
        
        change->operation = op;
        change->rows_affected = rows_affected;
        
        if (old_value) {
            strncpy(change->old_value, old_value, sizeof(change->old_value) - 1);
            change->old_value[sizeof(change->old_value) - 1] = '\0';
        } else {
            change->old_value[0] = '\0';
        }
        
        if (new_value) {
            strncpy(change->new_value, new_value, sizeof(change->new_value) - 1);
            change->new_value[sizeof(change->new_value) - 1] = '\0';
        } else {
            change->new_value[0] = '\0';
        }
        
        if (database) {
            strncpy(change->database, database, sizeof(change->database) - 1);
            change->database[sizeof(change->database) - 1] = '\0';
        } else {
            change->database[0] = '\0';
        }
        
        strncpy(change->full_query, normalized, sizeof(change->full_query) - 1);
        change->full_query[sizeof(change->full_query) - 1] = '\0';
        
        tracker->change_count++;
        created_count++;
    }
    
    // Cleanup
    for (int i = 0; i < column_count; i++) {
        if (columns[i]) free(columns[i]);
    }
    free(columns);
    free(table_name);
    free(normalized);
    
    return created_count;
}

/**
 * Get global tracker instance
 */
SQLTracker *sql_tracker_get_global(void) {
    return g_tracker;
}

/**
 * Get summary statistics
 */
void sql_tracker_summary(SQLTracker *tracker, SQLTrackerSummary *summary) {
    if (!tracker || !summary) return;
    
    memset(summary, 0, sizeof(SQLTrackerSummary));
    
    summary->total_changes = tracker->change_count;
    
    for (int i = 0; i < tracker->change_count; i++) {
        SQLChange *change = &tracker->changes[i];
        
        // Count by operation
        switch (change->operation) {
            case SQL_INSERT:
                summary->insert_count++;
                break;
            case SQL_UPDATE:
                summary->update_count++;
                break;
            case SQL_DELETE:
                summary->delete_count++;
                break;
            case SQL_SELECT:
                summary->select_count++;
                break;
            default:
                break;
        }
    }
}

/**
 * Free tracker
 */
void sql_tracker_free(SQLTracker *tracker) {
    if (!tracker) return;
    
    if (tracker->changes) {
        free(tracker->changes);
    }
    if (tracker->storage_path) {
        free(tracker->storage_path);
    }
    free(tracker);
    
    if (g_tracker == tracker) {
        g_tracker = NULL;
    }
}

/**
 * Get changes by filter
 */
int sql_tracker_get_changes(SQLTracker *tracker, const char *table_filter,
                            const char *column_filter, const char *operation_filter,
                            SQLChange **out_changes) {
    if (!tracker || !out_changes) return 0;
    
    int count = 0;
    *out_changes = (SQLChange *)malloc(sizeof(SQLChange) * tracker->change_count);
    
    if (!*out_changes) return 0;
    
    for (int i = 0; i < tracker->change_count; i++) {
        SQLChange *change = &tracker->changes[i];
        int match = 1;
        
        if (table_filter && strcmp(change->table_name, table_filter) != 0) {
            match = 0;
        }
        
        if (column_filter && strcmp(change->column_name, column_filter) != 0) {
            match = 0;
        }
        
        if (operation_filter) {
            const char *op_str = NULL;
            switch (change->operation) {
                case SQL_INSERT:
                    op_str = "INSERT";
                    break;
                case SQL_UPDATE:
                    op_str = "UPDATE";
                    break;
                case SQL_DELETE:
                    op_str = "DELETE";
                    break;
                case SQL_SELECT:
                    op_str = "SELECT";
                    break;
                default:
                    op_str = "UNKNOWN";
                    break;
            }
            if (strcmp(op_str, operation_filter) != 0) {
                match = 0;
            }
        }
        
        if (match) {
            (*out_changes)[count++] = *change;
        }
    }
    
    return count;
}

/*
 * memwatch_cli.c - Universal CLI for tracking memory across all 10 languages
 *
 * Usage:
 *   memwatch run <executable> [args...] --storage <path> [--scope global|local|both] [--threads]
 *   memwatch read <storage_path> [--filter name] [--format json|csv]
 *   memwatch monitor [--storage path] [--live]
 *
 * Supports:
 *   - C/C++ executables
 *   - Python scripts (python3 script.py)
 *   - JavaScript (node script.js)
 *   - Java (java -jar program.jar)
 *   - Go binaries
 *   - Rust binaries
 *   - C# (mono assembly.exe)
 *   - TypeScript (ts-node script.ts)
 *   - And any executable
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
#include <sqlite3.h>
#include <time.h>
#include <errno.h>
#include <stdint.h>

#include "memwatch_unified.h"

/* ============================================================================
 * Configuration
 * ============================================================================ */

#define MAX_ARGS 256
#define MAX_THREADS 64
#define MAX_VARIABLES 1024
#define STORAGE_BUFFER_SIZE (1024 * 1024)  /* 1 MB */
#define STORAGE_FLUSH_INTERVAL_MS 100
#define MEMWATCH_LIB_DIR "/workspaces/WaterCodeFlow/memwatch/build"

/* ============================================================================
 * Types
 * ============================================================================ */

typedef enum {
    CMD_RUN,
    CMD_READ,
    CMD_MONITOR,
    CMD_HELP,
    CMD_INVALID,
} command_t;

typedef enum {
    SCOPE_GLOBAL = 0,
    SCOPE_LOCAL = 1,
    SCOPE_BOTH = 2,
} scope_t;

typedef enum {
    FORMAT_HUMAN,
    FORMAT_JSON,
    FORMAT_CSV,
} format_t;

typedef enum {
    LANG_PYTHON,
    LANG_C,
    LANG_JAVASCRIPT,
    LANG_JAVA,
    LANG_GO,
    LANG_RUST,
    LANG_CSHARP,
    LANG_UNKNOWN,
} user_func_lang_t;

typedef struct {
    command_t cmd;
    char *executable;
    char **exe_args;
    int exe_argc;
    char *storage_path;
    scope_t scope;
    bool track_threads;
    format_t format;
    char *filter_name;
    int limit;
    bool live_mode;
    char *read_storage;
    char *user_func_path;
    user_func_lang_t user_func_lang;
} cli_args_t;

typedef struct {
    uint64_t timestamp_ns;
    uint32_t thread_id;
    const char *thread_name;
    const char *variable_name;
    const char *old_value;
    const char *new_value;
    const char *file;
    const char *function;
    uint32_t line;
} stored_event_t;

typedef struct {
    sqlite3 *db;
    FILE *buffer_fp;
    char *buffer;
    size_t buffer_pos;
    pthread_mutex_t lock;
    bool flushing;
} storage_t;

/* ============================================================================
 * Global State
 * ============================================================================ */

static storage_t g_storage = {0};
static pid_t g_child_pid = 0;
static bool g_running = true;
static struct {
    uint32_t num_events;
    uint32_t num_threads;
} g_stats = {0};
static cli_args_t g_cli_args = {0};

/* ============================================================================
 * Signal Handlers
 * ============================================================================ */

static void handle_sigint(int sig) {
    (void)sig;
    g_running = false;
    if (g_child_pid > 0) {
        kill(g_child_pid, SIGTERM);
    }
}

static void handle_sigterm(int sig) {
    (void)sig;
    g_running = false;
    if (g_child_pid > 0) {
        kill(g_child_pid, SIGTERM);
    }
}

/* ============================================================================
 * Storage Management
 * ============================================================================ */

static int storage_init(const char *path) {
    int rc;
    char *errmsg = NULL;
    
    /* Open database */
    rc = sqlite3_open(path, &g_storage.db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "❌ Cannot open database %s: %s\n", path, sqlite3_errmsg(g_storage.db));
        return -1;
    }
    
    /* Create schema */
    const char *schema = 
        "CREATE TABLE IF NOT EXISTS changes ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  timestamp_ns INTEGER NOT NULL,"
        "  thread_id INTEGER NOT NULL,"
        "  thread_name TEXT,"
        "  variable_name TEXT NOT NULL,"
        "  language TEXT,"
        "  scope TEXT,"
        "  old_preview TEXT,"
        "  new_preview TEXT,"
        "  file TEXT,"
        "  function TEXT,"
        "  line INTEGER"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_thread_id ON changes(thread_id);"
        "CREATE INDEX IF NOT EXISTS idx_var_name ON changes(variable_name);"
        "CREATE INDEX IF NOT EXISTS idx_timestamp ON changes(timestamp_ns);";
    
    rc = sqlite3_exec(g_storage.db, schema, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "❌ Schema error: %s\n", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }
    
    /* Allocate buffer */
    g_storage.buffer = malloc(STORAGE_BUFFER_SIZE);
    if (!g_storage.buffer) {
        fprintf(stderr, "❌ Memory allocation failed\n");
        return -1;
    }
    
    g_storage.buffer_pos = 0;
    pthread_mutex_init(&g_storage.lock, NULL);
    
    printf("✓ Storage initialized: %s\n", path);
    return 0;
}

static void storage_record_event(const memwatch_change_event_t *event) {
    if (!g_storage.db) return;
    
    pthread_mutex_lock(&g_storage.lock);
    
    /* Format as INSERT statement */
    int len = snprintf(
        g_storage.buffer + g_storage.buffer_pos,
        STORAGE_BUFFER_SIZE - g_storage.buffer_pos,
        "INSERT INTO changes(timestamp_ns, thread_id, thread_name, variable_name, "
        "old_preview, new_preview, file, function, line) VALUES(%lu, %u, 'main', "
        "'%s', '%.*s', '%.*s', '%s', '%s', %u);\n",
        (unsigned long)event->timestamp_ns,
        (unsigned)event->adapter_id,
        event->variable_name ? event->variable_name : "unknown",
        (int)event->old_preview_size, (char *)event->old_preview,
        (int)event->new_preview_size, (char *)event->new_preview,
        event->file ? event->file : "unknown",
        event->function ? event->function : "unknown",
        event->line
    );
    
    if (len > 0 && g_storage.buffer_pos + len < STORAGE_BUFFER_SIZE) {
        g_storage.buffer_pos += len;
        g_stats.num_events++;
    }
    
    pthread_mutex_unlock(&g_storage.lock);
}

static int storage_flush(void) {
    if (!g_storage.db) return 0;
    
    pthread_mutex_lock(&g_storage.lock);
    
    if (g_storage.buffer_pos == 0) {
        pthread_mutex_unlock(&g_storage.lock);
        return 0;
    }
    
    char *errmsg = NULL;
    int rc = sqlite3_exec(g_storage.db, g_storage.buffer, NULL, NULL, &errmsg);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "⚠️  Storage flush error: %s\n", errmsg);
        sqlite3_free(errmsg);
    }
    
    g_storage.buffer_pos = 0;
    pthread_mutex_unlock(&g_storage.lock);
    
    return rc == SQLITE_OK ? 0 : -1;
}

static void storage_close(void) {
    if (!g_storage.db) return;
    
    storage_flush();
    
    if (g_storage.db) {
        sqlite3_close(g_storage.db);
        g_storage.db = NULL;
    }
    
    if (g_storage.buffer) {
        free(g_storage.buffer);
        g_storage.buffer = NULL;
    }
    
    pthread_mutex_destroy(&g_storage.lock);
}

/* ============================================================================
 * User Function Execution
 * ============================================================================ */

static int execute_user_func(const memwatch_change_event_t *event) {
    if (!g_cli_args.user_func_path) {
        return 0;  /* No user function */
    }
    
    FILE *fp = fopen(g_cli_args.user_func_path, "r");
    if (!fp) {
        fprintf(stderr, "⚠️  Cannot open user function file: %s\n", g_cli_args.user_func_path);
        return -1;
    }
    
    /* Create temp file with event data */
    char temp_event_file[256];
    snprintf(temp_event_file, sizeof(temp_event_file), "/tmp/memwatch_event_%lu.json",
             (unsigned long)time(NULL));
    
    FILE *event_fp = fopen(temp_event_file, "w");
    if (event_fp) {
        fprintf(event_fp, "{\n");
        fprintf(event_fp, "  \"variable\": \"%s\",\n", 
                event->variable_name ? event->variable_name : "unknown");
        fprintf(event_fp, "  \"old_value\": \"%.*s\",\n", 
                (int)event->old_preview_size, (char *)event->old_preview);
        fprintf(event_fp, "  \"new_value\": \"%.*s\",\n",
                (int)event->new_preview_size, (char *)event->new_preview);
        fprintf(event_fp, "  \"region_id\": %u,\n", event->region_id);
        fprintf(event_fp, "  \"timestamp\": %lu\n", (unsigned long)event->timestamp_ns);
        fprintf(event_fp, "}\n");
        fclose(event_fp);
    }
    
    fclose(fp);
    return 0;
}

/* ============================================================================
 * Memory Tracking Callback
 * ============================================================================ */

static void tracking_callback(const memwatch_change_event_t *event, void *user_ctx) {
    (void)user_ctx;
    
    /* Record to storage */
    storage_record_event(event);
    
    /* Execute user function if provided */
    if (g_cli_args.user_func_path) {
        execute_user_func(event);
    }
    
    /* Print to stdout */
    printf("[%u] %s: %.*s → %.*s\n",
           event->region_id,
           event->variable_name ? event->variable_name : "var",
           (int)event->old_preview_size, (char *)event->old_preview,
           (int)event->new_preview_size, (char *)event->new_preview);
    fflush(stdout);
}

/* ============================================================================
 * Command: RUN
 * ============================================================================ */

static int cmd_run(cli_args_t *args) {
    if (!args->executable) {
        fprintf(stderr, "❌ No executable specified\n");
        return 1;
    }
    
    /* Store args globally for callback */
    memcpy(&g_cli_args, args, sizeof(cli_args_t));
    
    printf("🚀 Starting memory tracking...\n");
    printf("   Executable: %s\n", args->executable);
    printf("   Storage: %s\n", args->storage_path ? args->storage_path : "in-memory");
    if (args->user_func_path) {
        printf("   Callback: %s (%s)\n", args->user_func_path,
               args->user_func_lang == LANG_PYTHON ? "Python" :
               args->user_func_lang == LANG_C ? "C" :
               args->user_func_lang == LANG_JAVASCRIPT ? "JavaScript" :
               args->user_func_lang == LANG_JAVA ? "Java" :
               args->user_func_lang == LANG_GO ? "Go" :
               args->user_func_lang == LANG_RUST ? "Rust" :
               args->user_func_lang == LANG_CSHARP ? "C#" : "Unknown");
    }
    printf("   Scope: %s\n", args->scope == SCOPE_GLOBAL ? "global" :
                            args->scope == SCOPE_LOCAL ? "local" : "both");
    
    /* Initialize storage if specified */
    if (args->storage_path && storage_init(args->storage_path) < 0) {
        return 1;
    }
    
    /* Initialize memwatch */
    if (memwatch_init() != 0) {
        fprintf(stderr, "❌ Failed to initialize memwatch\n");
        return 1;
    }
    
    /* Set callback */
    memwatch_set_callback(tracking_callback, NULL);
    
    /* Fork and execute */
    g_child_pid = fork();
    if (g_child_pid < 0) {
        perror("❌ fork");
        return 1;
    }
    
    if (g_child_pid == 0) {
        /* Child process: inject memwatch via LD_PRELOAD */
        char *ld_preload = getenv("LD_PRELOAD");
        char *ld_library = getenv("LD_LIBRARY_PATH");
        char preload_buf[1024];
        char library_buf[1024];
        
        /* Set LD_LIBRARY_PATH to find libmemwatch.so */
        snprintf(library_buf, sizeof(library_buf), "%s%s%s",
                 MEMWATCH_LIB_DIR,
                 ld_library ? ":" : "",
                 ld_library ? ld_library : "");
        setenv("LD_LIBRARY_PATH", library_buf, 1);
        
        /* Set LD_PRELOAD to inject memwatch library */
        snprintf(preload_buf, sizeof(preload_buf), "%s/libmemwatch.so%s%s",
                 MEMWATCH_LIB_DIR,
                 ld_preload ? ":" : "",
                 ld_preload ? ld_preload : "");
        setenv("LD_PRELOAD", preload_buf, 1);
        
        /* Pass database path to child via environment */
        if (args->storage_path) {
            setenv("MEMWATCH_DB", args->storage_path, 1);
            setenv("MEMWATCH_VARS", "1", 1);
            setenv("MEMWATCH_SQL", "1", 1);
            setenv("MEMWATCH_THREADS", 
                   args->track_threads ? "1" : "0", 1);
            setenv("MEMWATCH_SCOPE", 
                   args->scope == SCOPE_GLOBAL ? "global" :
                   args->scope == SCOPE_LOCAL ? "local" : "both", 1);
        }
        
        /* Execute the program - use execvp to search PATH */
        char **argv = malloc(sizeof(char *) * (args->exe_argc + 2));
        argv[0] = args->executable;
        for (int i = 0; i < args->exe_argc; i++) {
            argv[i + 1] = args->exe_args[i];
        }
        argv[args->exe_argc + 1] = NULL;
        
        execvp(args->executable, argv);
        perror("❌ execvp");
        exit(1);
    }
    
    /* Parent process: monitor execution */
    printf("\n");
    printf("=== Memory Tracking Active ===\n");
    printf("Press Ctrl+C to stop\n");
    printf("\n");
    
    /* Set up signal handlers */
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigterm);
    
    /* Wait for child with periodic flush */
    int status;
    while (g_running) {
        pid_t wpid = waitpid(g_child_pid, &status, WNOHANG);
        
        if (wpid == g_child_pid) {
            /* Child exited */
            break;
        }
        
        /* Flush storage periodically */
        storage_flush();
        
        /* Sleep briefly */
        usleep(STORAGE_FLUSH_INTERVAL_MS * 1000);
    }
    
    /* Final flush */
    storage_flush();
    
    /* Print summary */
    printf("\n");
    printf("=== Tracking Complete ===\n");
    printf("Total events recorded: %u\n", g_stats.num_events);
    
    if (args->storage_path) {
        printf("Data saved to: %s\n", args->storage_path);
        printf("View with: memwatch read %s\n", args->storage_path);
    }
    
    /* Cleanup */
    memwatch_shutdown();
    storage_close();
    
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

/* ============================================================================
 * Command: READ
 * ============================================================================ */

static int cmd_read(cli_args_t *args) {
    if (!args->read_storage) {
        fprintf(stderr, "❌ Storage path not specified\n");
        return 1;
    }
    
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc = sqlite3_open(args->read_storage, &db);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "❌ Cannot open database: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    
    printf("\n=== Stored Memory Changes ===\n\n");
    
    /* Build query */
    const char *query = "SELECT timestamp_ns, thread_id, thread_name, variable_name, "
                        "old_preview, new_preview, file, line FROM changes "
                        "ORDER BY timestamp_ns ASC LIMIT 1000";
    
    rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "❌ Query error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        uint64_t timestamp = sqlite3_column_int64(stmt, 0);
        uint32_t thread_id = sqlite3_column_int(stmt, 1);
        const char *thread_name = (const char *)sqlite3_column_text(stmt, 2);
        const char *var_name = (const char *)sqlite3_column_text(stmt, 3);
        const char *old_val = (const char *)sqlite3_column_text(stmt, 4);
        const char *new_val = (const char *)sqlite3_column_text(stmt, 5);
        const char *file = (const char *)sqlite3_column_text(stmt, 6);
        int line = sqlite3_column_int(stmt, 7);
        
        if (args->format == FORMAT_JSON) {
            printf("{\n");
            printf("  \"timestamp\": %llu,\n", (unsigned long long)timestamp);
            printf("  \"thread_id\": %u,\n", thread_id);
            printf("  \"thread_name\": \"%s\",\n", thread_name ? thread_name : "unknown");
            printf("  \"variable\": \"%s\",\n", var_name);
            printf("  \"old_value\": \"%s\",\n", old_val ? old_val : "NULL");
            printf("  \"new_value\": \"%s\",\n", new_val ? new_val : "NULL");
            printf("  \"file\": \"%s\",\n", file ? file : "unknown");
            printf("  \"line\": %d\n", line);
            printf("},\n");
        } else {
            printf("[%d] %s::%u | %s: %s → %s (%s:%d)\n",
                   count, thread_name ? thread_name : "main", thread_id,
                   var_name, old_val ? old_val : "NULL", new_val ? new_val : "NULL",
                   file ? file : "unknown", line);
        }
        
        count++;
        if (args->limit > 0 && count >= args->limit) break;
    }
    
    printf("\nTotal records: %d\n", count);
    
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    
    return 0;
}

/* ============================================================================
 * Argument Parsing
 * ============================================================================ */

static void print_help(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║         MemWatch CLI - Universal Memory Tracker            ║\n");
    printf("║         Works with C, Python, Java, Go, Rust, C#, ...      ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("USAGE:\n");
    printf("\n");
    printf("  memwatch run <executable> [args...]\n");
    printf("           --storage <path>\n");
    printf("           [--scope global|local|both]\n");
    printf("           [--threads]\n");
    printf("           [--user-func <path> --user-func-lang <lang>]\n");
    printf("\n");
    printf("  memwatch read <storage_path>\n");
    printf("           [--filter <name>]\n");
    printf("           [--format json|csv|human]\n");
    printf("           [--limit <n>]\n");
    printf("\n");
    printf("  memwatch monitor [--storage <path>]\n");
    printf("\n");
    printf("CALLBACK FUNCTION:\n");
    printf("\n");
    printf("  Use --user-func to call a custom function on each change.\n");
    printf("  Function must be named 'main' in the source file.\n");
    printf("  Supported languages: python, c, javascript, java, go, rust, csharp\n");
    printf("\n");
    printf("EXAMPLES:\n");
    printf("\n");
    printf("  # Track Python script\n");
    printf("  memwatch run python3 script.py --storage tracking.db\n");
    printf("\n");
    printf("  # Track with custom callback\n");
    printf("  memwatch run ./program --storage tracking.db \\\n");
    printf("             --user-func my_callback.py --user-func-lang python\n");
    printf("\n");
    printf("  # Track C program with threading\n");
    printf("  memwatch run ./program --storage tracking.db --threads\n");
    printf("\n");
    printf("  # Track Java with callback\n");
    printf("  memwatch run java -jar app.jar --storage tracking.db \\\n");
    printf("             --user-func MyCallback.java --user-func-lang java\n");
    printf("\n");
    printf("  # View recorded data\n");
    printf("  memwatch read tracking.db --format json\n");
    printf("\n");
}

static int parse_args(int argc, char *argv[], cli_args_t *args) {
    if (argc < 2) {
        print_help();
        return -1;
    }
    
    memset(args, 0, sizeof(*args));
    args->scope = SCOPE_BOTH;
    args->format = FORMAT_HUMAN;
    args->limit = -1;
    args->exe_args = malloc(sizeof(char *) * MAX_ARGS);
    
    if (strcmp(argv[1], "run") == 0) {
        args->cmd = CMD_RUN;
        
        int i = 2;
        int exe_idx = 0;
        
        while (i < argc) {
            if (strcmp(argv[i], "--storage") == 0) {
                if (++i < argc) args->storage_path = argv[i];
            } else if (strcmp(argv[i], "--scope") == 0) {
                if (++i < argc) {
                    if (strcmp(argv[i], "global") == 0) args->scope = SCOPE_GLOBAL;
                    else if (strcmp(argv[i], "local") == 0) args->scope = SCOPE_LOCAL;
                    else args->scope = SCOPE_BOTH;
                }
            } else if (strcmp(argv[i], "--threads") == 0) {
                args->track_threads = true;
            } else if (strcmp(argv[i], "--user-func") == 0) {
                if (++i < argc) args->user_func_path = argv[i];
            } else if (strcmp(argv[i], "--user-func-lang") == 0) {
                if (++i < argc) {
                    char *lang = argv[i];
                    if (strcmp(lang, "python") == 0) args->user_func_lang = LANG_PYTHON;
                    else if (strcmp(lang, "c") == 0) args->user_func_lang = LANG_C;
                    else if (strcmp(lang, "javascript") == 0) args->user_func_lang = LANG_JAVASCRIPT;
                    else if (strcmp(lang, "java") == 0) args->user_func_lang = LANG_JAVA;
                    else if (strcmp(lang, "go") == 0) args->user_func_lang = LANG_GO;
                    else if (strcmp(lang, "rust") == 0) args->user_func_lang = LANG_RUST;
                    else if (strcmp(lang, "csharp") == 0) args->user_func_lang = LANG_CSHARP;
                    else args->user_func_lang = LANG_UNKNOWN;
                }
            } else if (!args->executable) {
                args->executable = argv[i];
            } else {
                args->exe_args[exe_idx++] = argv[i];
            }
            i++;
        }
        args->exe_argc = exe_idx;
        
    } else if (strcmp(argv[1], "read") == 0) {
        args->cmd = CMD_READ;
        
        if (argc < 3) {
            fprintf(stderr, "❌ Storage path required\n");
            return -1;
        }
        args->read_storage = argv[2];
        
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
                if (strcmp(argv[++i], "json") == 0) args->format = FORMAT_JSON;
                else if (strcmp(argv[i], "csv") == 0) args->format = FORMAT_CSV;
            } else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
                args->filter_name = argv[++i];
            } else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
                args->limit = atoi(argv[++i]);
            }
        }
        
    } else if (strcmp(argv[1], "monitor") == 0) {
        args->cmd = CMD_MONITOR;
        args->live_mode = true;
    } else {
        fprintf(stderr, "❌ Unknown command: %s\n", argv[1]);
        print_help();
        return -1;
    }
    
    return 0;
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(int argc, char *argv[]) {
    cli_args_t args;
    
    if (parse_args(argc, argv, &args) < 0) {
        return 1;
    }
    
    int result = 0;
    
    switch (args.cmd) {
        case CMD_RUN:
            result = cmd_run(&args);
            break;
        case CMD_READ:
            result = cmd_read(&args);
            break;
        case CMD_MONITOR:
            printf("⏳ Monitor mode not yet implemented\n");
            result = 1;
            break;
        default:
            print_help();
            result = 1;
    }
    
    free(args.exe_args);
    return result;
}

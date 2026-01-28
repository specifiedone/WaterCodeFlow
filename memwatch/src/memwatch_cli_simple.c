/*
 * memwatch_cli_simple.c - Memory tracking CLI using real mprotect tracking
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sqlite3.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include "memwatch_tracker.h"

typedef struct {
    bool track_all_vars;
    bool track_sql;
    bool threads;
    char scope[20];
} options_t;

static int cmd_run(const char *program, int argc, char **argv, const char *db_path, options_t opts) {
    if (tracker_init(db_path, opts.track_all_vars, opts.track_sql, opts.threads, opts.scope) < 0) {
        fprintf(stderr, "❌ Failed to initialize tracker\n");
        return 1;
    }

    printf("\n╔═══════════════════════════════════════════════════════════╗\n");
    printf("║   MemWatch CLI - REAL Memory Tracking (mprotect+SIGSEGV)   ║\n");
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║ Program:        %s\n", program);
    printf("║ Database:       %s\n", db_path);
    printf("║ Track All Vars: %s\n", opts.track_all_vars ? "YES ✅" : "NO");
    printf("║ Track SQL:      %s\n", opts.track_sql ? "YES ✅" : "NO");
    printf("║ Thread Aware:   %s\n", opts.threads ? "YES ✅" : "NO");
    printf("║ Scope:          %s\n", opts.scope);
    printf("║ Method:         mprotect() + SIGSEGV + Reprotect\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");

    char **exec_argv = malloc((argc + 2) * sizeof(char *));
    exec_argv[0] = (char *)program;
    for (int i = 0; i < argc; i++) {
        exec_argv[i + 1] = argv[i];
    }
    exec_argv[argc + 1] = NULL;

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        /* Child process: Set up environment for LD_PRELOAD to work */
        char db_env[512];
        char sql_env[32];
        char threads_env[32];
        char vars_env[32];
        char scope_env[32];
        char preload_path[256];
        
        /* Get the path to our preload library */
        snprintf(preload_path, sizeof(preload_path), "%s", getenv("MEMWATCH_PRELOAD") ?: "./build/libmemwatch.so");
        
        snprintf(db_env, sizeof(db_env), "MEMWATCH_DB=%s", db_path);
        snprintf(sql_env, sizeof(sql_env), "MEMWATCH_SQL=%d", opts.track_sql);
        snprintf(threads_env, sizeof(threads_env), "MEMWATCH_THREADS=%d", opts.threads);
        snprintf(vars_env, sizeof(vars_env), "MEMWATCH_VARS=%d", opts.track_all_vars);
        snprintf(scope_env, sizeof(scope_env), "MEMWATCH_SCOPE=%s", opts.scope);
        
        setenv("MEMWATCH_DB", db_path, 1);
        setenv("MEMWATCH_SQL", opts.track_sql ? "1" : "0", 1);
        setenv("MEMWATCH_THREADS", opts.threads ? "1" : "0", 1);
        setenv("MEMWATCH_VARS", opts.track_all_vars ? "1" : "0", 1);
        setenv("MEMWATCH_SCOPE", opts.scope, 1);
        
        /* Set LD_PRELOAD if SQL tracking is enabled */
        if (opts.track_sql) {
            setenv("LD_PRELOAD", preload_path, 1);
        }
        
        execvp(program, exec_argv);
        perror("execvp");
        exit(1);
    }

    free(exec_argv);

    int status;
    waitpid(pid, &status, 0);

    int events = tracker_get_event_count();
    
    tracker_close();

    printf("\n╔═══════════════════════════════════════════════════════════╗\n");
    printf("║                   Tracking Complete!                        ║\n");
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║ Memory changes recorded: %d\n", events);
    printf("║ Database: %s\n", db_path);
    printf("║ View results: ./build/memwatch_cli read %s\n", db_path);
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return 1;
}

static int cmd_read(const char *db_path) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_open(db_path, &db);
    if (rc) {
        fprintf(stderr, "❌ Cannot open database: %s\n", db_path);
        return 1;
    }

    const char *sql = "SELECT timestamp_ms, region_name, offset, old_value, new_value FROM memory_changes ORDER BY timestamp_ms LIMIT 100";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "❌ SQL error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    printf("\n┌──────────────────────────────────────────────────────────────┐\n");
    printf("│ Memory Changes (last 100 events)                             │\n");
    printf("├────────────┬──────────────────────┬────────────┬────────────┤\n");
    printf("│ Timestamp  │ Region               │ Old        │ New        │\n");
    printf("├────────────┼──────────────────────┼────────────┼────────────┤\n");

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int64_t ts = sqlite3_column_int64(stmt, 0);
        const char *region = (const char *)sqlite3_column_text(stmt, 1);
        const char *old = (const char *)sqlite3_column_text(stmt, 3);
        const char *new = (const char *)sqlite3_column_text(stmt, 4);

        printf("│ %10ld │ %-20s │ %-10s │ %-10s │\n", ts, region ?: "?", old ?: "?", new ?: "?");
        count++;
    }

    printf("└────────────┴──────────────────────┴────────────┴────────────┘\n");
    printf("Total: %d events\n\n", count);

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
}

static void print_help(void) {
    printf("\n╔═══════════════════════════════════════════════════════════╗\n");
    printf("║          MemWatch - Real Memory Tracking                   ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");
    printf("USAGE:\n");
    printf("  memwatch run <program> [args...] --storage <db.db> [OPTIONS]\n");
    printf("  memwatch read <db.db>\n\n");
    printf("OPTIONS:\n");
    printf("  --track-all-vars    Auto-track all memory changes\n");
    printf("  --track-sql         Track SQL operations\n");
    printf("  --threads           Record thread IDs\n");
    printf("  --scope SCOPE       Filter: global, local, or both\n\n");
    printf("EXAMPLES:\n");
    printf("  memwatch run python3 script.py --storage data.db --track-all-vars\n");
    printf("  memwatch read data.db\n\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help();
        return 1;
    }

    if (strcmp(argv[1], "run") == 0) {
        if (argc < 4) {
            fprintf(stderr, "❌ Usage: memwatch run <program> [args...] --storage <db.db>\n");
            return 1;
        }

        const char *storage = NULL;
        bool track_all_vars = false, track_sql = false, threads = false;
        char scope[20] = "both";
        int prog_args_count = 0;
        
        for (int i = argc - 1; i > 2; i--) {
            if (strcmp(argv[i], "--storage") == 0 && i + 1 < argc) {
                storage = argv[i + 1];
                prog_args_count = i - 3;
                break;
            }
        }
        
        if (!storage) {
            fprintf(stderr, "❌ --storage path is required\n");
            return 1;
        }
        
        for (int i = 3 + prog_args_count; i < argc; i++) {
            if (strcmp(argv[i], "--track-all-vars") == 0) track_all_vars = true;
            else if (strcmp(argv[i], "--track-sql") == 0) track_sql = true;
            else if (strcmp(argv[i], "--threads") == 0) threads = true;
            else if (strcmp(argv[i], "--scope") == 0 && i + 1 < argc)
                strncpy(scope, argv[++i], sizeof(scope) - 1);
        }

        options_t opts = {track_all_vars, track_sql, threads, {}};
        strcpy(opts.scope, scope);

        return cmd_run(argv[2], prog_args_count, &argv[3], storage, opts);

    } else if (strcmp(argv[1], "read") == 0) {
        if (argc < 3) {
            fprintf(stderr, "❌ Usage: memwatch read <database>\n");
            return 1;
        }
        return cmd_read(argv[2]);
    }

    print_help();
    return 1;
}

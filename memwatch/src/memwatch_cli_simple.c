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
        return 1;
    }

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

    tracker_close();

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
        return 1;
    }

    const char *sql = "SELECT timestamp_ms, region_name, offset, old_value, new_value FROM memory_changes ORDER BY timestamp_ms LIMIT 100";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_close(db);
        return 1;
    }

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        count++;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
}

static void print_help(void) {
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help();
        return 1;
    }

    if (strcmp(argv[1], "run") == 0) {
        if (argc < 4) {
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
            return 1;
        }
        return cmd_read(argv[2]);
    }

    print_help();
    return 1;
}

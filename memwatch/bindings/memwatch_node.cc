/*
 * bindings/memwatch_node.cc - Node.js native binding using N-API
 * 
 * Provides a unified API for JavaScript/TypeScript
 */

#include <node_api.h>
#include <memwatch_unified.h>
#include <string.h>

/* Helper to throw error from native code */
static napi_value throw_error(napi_env env, const char *msg) {
    napi_throw_error(env, NULL, msg);
    return NULL;
}

/* Global state */
static napi_ref global_callback_ref = NULL;
static napi_env global_callback_env = NULL;

/* Change event callback that invokes JavaScript callback */
static void node_event_callback(const memwatch_change_event_t *event, void *user_ctx) {
    if (!global_callback_ref || !global_callback_env) return;
    
    napi_env env = global_callback_env;
    napi_status status;
    
    /* Get callback function */
    napi_value callback;
    status = napi_get_reference_value(env, global_callback_ref, &callback);
    if (status != napi_ok) return;
    
    /* Build event object */
    napi_value event_obj;
    napi_create_object(env, &event_obj);
    
    /* seq */
    napi_value seq;
    napi_create_uint32(env, event->seq, &seq);
    napi_set_named_property(env, event_obj, "seq", seq);
    
    /* timestamp_ns */
    napi_value ts;
    napi_create_bigint_uint64(env, event->timestamp_ns, &ts);
    napi_set_named_property(env, event_obj, "timestamp_ns", ts);
    
    /* variable_name */
    if (event->variable_name) {
        napi_value name;
        napi_create_string_utf8(env, event->variable_name, NAPI_AUTO_LENGTH, &name);
        napi_set_named_property(env, event_obj, "variable_name", name);
    }
    
    /* location info */
    napi_value where;
    napi_create_object(env, &where);
    
    if (event->file) {
        napi_value file;
        napi_create_string_utf8(env, event->file, NAPI_AUTO_LENGTH, &file);
        napi_set_named_property(env, where, "file", file);
    }
    
    if (event->function) {
        napi_value func;
        napi_create_string_utf8(env, event->function, NAPI_AUTO_LENGTH, &func);
        napi_set_named_property(env, where, "function", func);
    }
    
    napi_value line;
    napi_create_uint32(env, event->line, &line);
    napi_set_named_property(env, where, "line", line);
    
    napi_set_named_property(env, event_obj, "where", where);
    
    /* old_preview */
    if (event->old_preview && event->old_preview_size > 0) {
        napi_value old_buf;
        void *data;
        napi_create_buffer_copy(env, event->old_preview_size, (void*)event->old_preview, &data, &old_buf);
        napi_set_named_property(env, event_obj, "old_preview", old_buf);
    }
    
    /* new_preview */
    if (event->new_preview && event->new_preview_size > 0) {
        napi_value new_buf;
        void *data;
        napi_create_buffer_copy(env, event->new_preview_size, (void*)event->new_preview, &data, &new_buf);
        napi_set_named_property(env, event_obj, "new_preview", new_buf);
    }
    
    /* Invoke callback */
    napi_call_function(env, NULL, callback, 1, &event_obj, NULL);
}

/* init() */
static napi_value init(napi_env env, napi_callback_info info) {
    int result = memwatch_init();
    
    napi_value ret;
    napi_create_int32(env, result, &ret);
    return ret;
}

/* shutdown() */
static napi_value shutdown(napi_env env, napi_callback_info info) {
    memwatch_shutdown();
    
    napi_value ret;
    napi_get_undefined(env, &ret);
    return ret;
}

/* watch(addr, size, name, user_data?) */
static napi_value watch(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value argv[4];
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    
    /* Get arguments */
    uint64_t addr;
    napi_get_value_bigint_uint64(env, argv[0], &addr, NULL);
    
    uint32_t size;
    napi_get_value_uint32(env, argv[1], &size);
    
    size_t name_len = 256;
    char name[256] = {0};
    if (argc > 2 && !napi_is_null(env, argv[2])) {
        napi_get_value_string_utf8(env, argv[2], name, sizeof(name), &name_len);
    }
    
    void *user_data = NULL;
    if (argc > 3) {
        napi_get_value_external(env, argv[3], &user_data);
    }
    
    uint32_t region_id = memwatch_watch(addr, size, name_len > 0 ? name : NULL, user_data);
    
    napi_value ret;
    napi_create_uint32(env, region_id, &ret);
    return ret;
}

/* unwatch(region_id) */
static napi_value unwatch(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1];
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    
    uint32_t region_id;
    napi_get_value_uint32(env, argv[0], &region_id);
    
    bool result = memwatch_unwatch(region_id);
    
    napi_value ret;
    napi_get_boolean(env, result, &ret);
    return ret;
}

/* set_callback(fn) */
static napi_value set_callback(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1];
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    
    /* Store callback reference */
    if (global_callback_ref) {
        napi_delete_reference(env, global_callback_ref);
    }
    
    if (!napi_is_null(env, argv[0])) {
        napi_create_reference(env, argv[0], 1, &global_callback_ref);
        global_callback_env = env;
        memwatch_set_callback(node_event_callback, NULL);
    } else {
        memwatch_set_callback(NULL, NULL);
        global_callback_ref = NULL;
    }
    
    napi_value ret;
    napi_get_undefined(env, &ret);
    return ret;
}

/* check_changes() */
static napi_value check_changes(napi_env env, napi_callback_info info) {
    memwatch_change_event_t events[16];
    int count = memwatch_check_changes(events, 16);
    
    napi_value events_array;
    napi_create_array_with_length(env, count, &events_array);
    
    for (int i = 0; i < count; i++) {
        napi_value event_obj;
        napi_create_object(env, &event_obj);
        
        /* Fill event object (same as callback) */
        napi_value seq;
        napi_create_uint32(env, events[i].seq, &seq);
        napi_set_named_property(env, event_obj, "seq", seq);
        
        /* ... (similar to node_event_callback) ... */
        
        napi_set_element(env, events_array, i, event_obj);
        memwatch_free_event(&events[i]);
    }
    
    return events_array;
}

/* get_stats() */
static napi_value get_stats(napi_env env, napi_callback_info info) {
    memwatch_stats_t stats = {0};
    memwatch_get_stats(&stats);
    
    napi_value stats_obj;
    napi_create_object(env, &stats_obj);
    
    napi_value val;
    
    napi_create_uint32(env, stats.num_tracked_regions, &val);
    napi_set_named_property(env, stats_obj, "num_tracked_regions", val);
    
    napi_create_uint32(env, stats.num_active_watchpoints, &val);
    napi_set_named_property(env, stats_obj, "num_active_watchpoints", val);
    
    napi_create_bigint_uint64(env, stats.total_events, &val);
    napi_set_named_property(env, stats_obj, "total_events", val);
    
    return stats_obj;
}

/* Module init */
static napi_value init_module(napi_env env, napi_value exports) {
    napi_property_descriptor descriptors[] = {
        {"init", NULL, init, NULL, NULL, NULL, napi_default, NULL},
        {"shutdown", NULL, shutdown, NULL, NULL, NULL, napi_default, NULL},
        {"watch", NULL, watch, NULL, NULL, NULL, napi_default, NULL},
        {"unwatch", NULL, unwatch, NULL, NULL, NULL, napi_default, NULL},
        {"set_callback", NULL, set_callback, NULL, NULL, NULL, napi_default, NULL},
        {"check_changes", NULL, check_changes, NULL, NULL, NULL, napi_default, NULL},
        {"get_stats", NULL, get_stats, NULL, NULL, NULL, napi_default, NULL},
    };
    
    napi_define_properties(env, exports, 7, descriptors);
    return exports;
}

NAPI_MODULE(memwatch_native, init_module)

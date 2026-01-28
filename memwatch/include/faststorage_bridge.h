/*
 * faststorage_bridge.h - C interface to FastStorage
 */

#ifndef FASTSTORAGE_BRIDGE_H
#define FASTSTORAGE_BRIDGE_H

#include <stddef.h>

/* Initialize FastStorage backend */
int faststorage_bridge_init(const char *db_path, size_t capacity);

/* Write to storage */
int faststorage_bridge_write(const char *key, const char *value);

/* Read from storage */
const char* faststorage_bridge_read(const char *key);

/* Flush to disk */
int faststorage_bridge_flush(void);

/* Get bytes used */
size_t faststorage_bridge_bytes_used(void);

/* Get utilization percentage */
float faststorage_bridge_utilization(size_t capacity);

/* Close storage */
void faststorage_bridge_close(void);

#endif /* FASTSTORAGE_BRIDGE_H */

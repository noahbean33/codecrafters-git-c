#ifndef OBJECT_H
#define OBJECT_H

#include <stdio.h>
#include <sys/types.h>
/* 
 * Will Implement later. This is for structure purposes
 * This will cover generic object operations (read any object type, decompress, parse header) 
 * since trees and commits will need similar logic later.
*/

// Data structure for objects; using for all since they follow the same basic format
typedef struct {
    size_t size;
    unsigned char *data;
} Object;

/**
 * Writes an object to .git/objects
 * 
 * @param type    - "blob" or "tree"
 * @param content - the raw content (file data or tree entries)
 * @param size    - size of content in bytes
 * @param outHash - buffer to store resulting 40-char hex SHA (must be 41 bytes)
 * @return 0 on success, -1 on error
 */
int writeObject(const char *type, const unsigned char *content, size_t size, char *outHash);

typedef struct {
    char mode[8];
    char name[256];
    unsigned char rawsha[20]; // raw SHA-1 bytes
} Entry;

/**
 * @ tree <tree_sha>
 * parent <parent_sha>
 * author <name> <email> <timestamp> <timezone>
 * committer <name> <email> <timestamp> <timezone>
 */
typedef struct {
    char author[256];
    char timestamp[64];
    char *treesha; // tree sha
    char *parentsha; // parent commit sha(s) if any
    char *message;
} Commit;

#endif // OBJECT_H
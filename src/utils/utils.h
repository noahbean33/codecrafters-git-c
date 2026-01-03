#ifndef UTILS_H
#define UTILS_H

#include <stddef.h> 

#define SHA_DIGEST_LENGTH 20

char* hash(const char *data, size_t length, char *outHash);
void hexToRaw(const char *hex, unsigned char *raw);
char* buildPath(const char *hash);
int compareEntries(const void *a, const void *b);

#endif // UTILS_H
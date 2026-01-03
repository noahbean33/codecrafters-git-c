#include "utils.h"
#include <errno.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

char* hash(const char *data, size_t length, char *outHash) {
    unsigned char hash[SHA_DIGEST_LENGTH]; // SHA1 produces a 20-byte hash
    SHA1((unsigned char *)data, length, hash); // From OPENSSL; compute SHA1 hash

    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
        sprintf(&outHash[i * 2], "%02x", hash[i]);
    }
    outHash[SHA_DIGEST_LENGTH * 2] = '\0'; // Null terminate the string

    return 0;
}

void hexToRaw(const char *hex, unsigned char *raw) {
    for (int i = 0; i < 20; i++) {
        sscanf(hex + (i * 2), "%2hhx", &raw[i]);
    }
}
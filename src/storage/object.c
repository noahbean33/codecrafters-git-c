#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>
#include <errno.h>
#include "../utils/utils.h"

int writeObject(const char *type, const unsigned char *content, size_t size, char *outHash) {
    // Build header: "<type> <size>\0"
    char header[64];
    int headerLength = snprintf(header, sizeof(header), "%s %zu", type, size) + 1;

    int blobSize = headerLength + size; 
    unsigned char *buffer = malloc(blobSize);
    memcpy(buffer, header, headerLength);
    memcpy(buffer + headerLength, content, size);

    // Compute sha-1: hash entire blob (header + content) 
    char hashOutput[SHA_DIGEST_LENGTH * 2 + 1];
    hash((const char *)buffer, blobSize, hashOutput);

    // Compress with zlib 
    unsigned long compressedSize = compressBound(blobSize);
    unsigned char *compressed = malloc(compressedSize);

    compress(compressed, &compressedSize, buffer, blobSize);

    // Create the directory - ./git/objects/xx/ (first 2 chars of hash)
    char dirPath[256];
    snprintf(dirPath, sizeof(dirPath), ".git/objects/%c%c", hashOutput[0], hashOutput[1]);
    if (mkdir(dirPath, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Error: Could not create directory %s: %s\n", dirPath, strerror(errno));
        free(buffer);
        return 1;
    }

    // Write the file to ./git/objects/xx/yy... (rest of hash)
    char *filePath = buildPath(hashOutput);
    FILE *outFile = fopen(filePath, "wb");
    if (outFile == NULL) {
        fprintf(stderr, "Error: Could not create object file %s: %s\n", filePath, strerror(errno));
        free(buffer);
        return 1;
    }
    fwrite(compressed, 1, compressedSize, outFile);
    fclose(outFile);
    
    // Copy hash to output parameter
    strcpy(outHash, hashOutput);

    // Cleanup
    free(compressed);
    free(buffer);

    return 0;
}
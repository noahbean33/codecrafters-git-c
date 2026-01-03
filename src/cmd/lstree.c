#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <zlib.h>
#include "../utils/utils.h"

// LSTree command implementation: ls-tree --name-only <tree_sha>
int LSTree(int argc, char *argv[]) {
    int nameOnly = 0;
    const char *hash;
    if (argc < 3) {
        fprintf(stderr, "Error: Not enough arguments for ls-tree\n");
        return 1;
    }

    if (strcmp(argv[2], "--name-only") == 0) {
        // Print only names of files in the tree
        nameOnly = 1;
        hash = argv[3];
    } else {
        hash = argv[2];
    }
    
    char *path = buildPath(hash);

    // Decompress (similar to catfile)
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Error: Could not open object %s: %s\n", hash, strerror(errno));
        return 1;   
    }
    // Get file size
    fseek(file, 0, SEEK_END);
    long filesize = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Read compressed data
    unsigned char *compressed = malloc(filesize);
    fread(compressed, 1, filesize, file);
    fclose(file);

    // Decompress with zlib
    unsigned long decompressedSize = filesize * 10; // Estimate larger buffer
    unsigned char *decompressed = malloc(decompressedSize);

    // Uncompress file
    int result = uncompress(decompressed, &decompressedSize, compressed, filesize);
    if (result != Z_OK) {
        fprintf(stderr, "Error: Failed to decompress object %s\n", hash);
        free(compressed);
        free(decompressed);
        return 1;
    }

    // Skip header; find first null byte to get past tree <size>\0
    char *nullByte = memchr(decompressed, '\0', decompressedSize);
    if (!nullByte) {
        fprintf(stderr, "Error: Invalid object format for %s\n", hash);
        free(compressed);
        free(decompressed);
        return 1;
    }
    unsigned char *content = nullByte + 1;

    // Parse entries in a loop
    /*
     * for each eentry
        * read mode until space
        * read name until null byte
        * read sha (20 bytes)
        * repeat until end of data
    */
    for (char *ptr = content; ptr < decompressed + decompressedSize;) {
        // read mode until space
        unsigned char *space = memchr(ptr, ' ', (char *)decompressed + decompressedSize - ptr);
        if (!space) break;
        ptr = (char *)(space + 1);

        // read name until null byte
        unsigned char *nullByte = memchr(ptr, '\0', (char *)decompressed + decompressedSize - ptr);
        if (!nullByte) break;
        if (nameOnly) {
            printf("%.*s\n", (int)(nullByte - (unsigned char *)ptr), ptr);
        }
        ptr = (char *)(nullByte + 1 + 20); // move past null byte and sha

        // read sha (20 bytes)
        unsigned char sha[20];
        memcpy(sha, nullByte + 1, 20);
        // repeat until end of data
    }

    return 0;
}
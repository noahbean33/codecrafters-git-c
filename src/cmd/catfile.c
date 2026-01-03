#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <zlib.h>

char *buildPath(const char *hash);

int catFile(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Error: Not enough arguments for cat-file\n");
        return 1;
    }

    if (strcmp(argv[2], "-p") != 0) {
        fprintf(stderr, "Error: Unknown flag %s\n", argv[2]);
        return 1;
    }

    const char *hash = argv[3];

    // Build the path to the object file
    char *path = buildPath(hash);

    // Open and read compressed file
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

    // Parse header and find content: blob <size>\0<content>
    char *nullByte = memchr(decompressed, '\0', decompressedSize);
    if (!nullByte) {
        fprintf(stderr, "Error: Invalid object format for %s\n", hash);
        free(compressed);
        free(decompressed);
        return 1;
    }
    char *content = nullByte + 1;

    // Print content
    long contentLen = decompressedSize - (content - (char *)decompressed);
    fwrite(content, 1, contentLen, stdout);

    // cleanup
    free(compressed);
    free(decompressed);

    return 0;
}
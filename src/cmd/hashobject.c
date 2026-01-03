#include "../utils/utils.h"
#include "../storage/object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <zlib.h>

int hashObject(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Error: Not enough arguments for hash-object\n");
        return 1;
    }

    if (strcmp(argv[2], "-w") != 0) {
        fprintf(stderr, "Error: Unknown flag %s\n", argv[2]);
        return 1;
    }

    const char *filename = argv[3];

    // Read source file 
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        fprintf(stderr, "Error: Could not open file %s: %s\n", filename, strerror(errno));
        return 1;   
    }

    // Build blob fomat: "blob <size>\0<content>"
    fseek(file, 0, SEEK_END);
    long filesize = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *content = malloc(filesize);
    fread(content, 1, filesize, file);
    fclose(file);

    // Write object and get hash
    char hashOutput[41]; // 40 chars + null terminator
    writeObject("blob", (unsigned char *)content, filesize, hashOutput);
    free(content);

    // Print the hash to stdout
    printf("%s\n", hashOutput);

    return 0;
}
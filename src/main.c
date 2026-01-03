#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include "utils/utils.h"
#include "cmd/cmd.h"

int main(int argc, char *argv[]) {
    // Disable output buffering
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    if (argc < 2) {
        fprintf(stderr, "Usage: ./your_program.sh <command> [<args>]\n");
        return 1;
    }
    
    const char *command = argv[1];
    
    if (strcmp(command, "init") == 0) {
        return init();
    } 
    if (strcmp(command, "cat-file") == 0) {
        return catFile(argc, argv);
    } if (strcmp(command, "hash-object") == 0) {
        return hashObject(argc, argv);
    } if (strcmp(command, "ls-tree") == 0) {
        return LSTree(argc, argv);   
    } if (strcmp(command, "write-tree") == 0) {
        char *sha = writeTree(".");
        if (sha) {
            printf("%s\n", sha);
            free(sha);
            return 0;
        } else {
            return 1;
        }
    } if (strcmp(command, "commit-tree") == 0) {
        return commitTree(argc, argv);
    } else {
        fprintf(stderr, "Unknown command %s\n", command);
        return 1;
    }
    
    return 0;
}

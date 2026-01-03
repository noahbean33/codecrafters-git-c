#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <zlib.h>
#include <dirent.h>
#include "../utils/utils.h"
#include "../storage/object.h"

char* writeTree(char *dirname) {
    Entry entries[256];
    int entrycount = 0;
    
    DIR *dir = opendir(dirname);
    if (dir == NULL) {
        fprintf(stderr, "Error: Could not open directory %s: %s\n", dirname, strerror(errno));
        return NULL;
    }
    struct dirent *entry;
    // COllect entries, sort them, then build content

    while ((entry = readdir(dir)) != NULL) {
        // Skip . and .. and .git
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, ".git") == 0) {
            continue;
        }

        // process file or direcotry 
        char childPath[256];
        snprintf(childPath, sizeof(childPath), "%s/%s", dirname, entry->d_name);

        char hexSha[41];
        char rawSha[20];
        const char *mode;

        // store in entries[entryCount++]
        if (entry->d_type == DT_REG) {
            mode = "100644";
            // read file content, call writeObject("blob", ...), get hexSha
            FILE *file = fopen(childPath, "rb");

            fseek(file, 0, SEEK_END);
            long filesize = ftell(file);
            fseek(file, 0, SEEK_SET);

            char *content = malloc(filesize);
            fread(content, 1, filesize, file);
            fclose(file);

            writeObject("blob", (unsigned char *)content, filesize, hexSha); 
            free(content);
        } else if (entry->d_type == DT_DIR) {
            mode = "40000";
            // recursive call to writeTree(childPath), get hexSha
            char *sha = writeTree(childPath);
            strcpy(hexSha, sha);
        }

        // Add entry to tree content
        // Copy mode space name and null byte
        // Convert hexSha to 20 raw bytes and append
        hexToRaw(hexSha, (unsigned char *)rawSha);
        strcpy(entries[entrycount].mode, mode);
        strcpy(entries[entrycount].name, entry->d_name);
        memcpy(entries[entrycount].rawsha, rawSha, 20);
        entrycount++;
    }
    closedir(dir);

    // Sort entries alphabetically by name
    qsort(entries, entrycount, sizeof(Entry), compareEntries);

    // Build tree content from sorted entries
    unsigned char *treeContent = malloc(1024 * 1024); // 1MB buffer
    size_t treeSize = 0;

    // Iterate entries with readdir
    for (int i = 0; i < entrycount; i++) {
        // append: mode + " " + name + "\0" + rawSha (20 bytes)
        int modeLen = strlen(entries[i].mode);
        memcpy(treeContent + treeSize, entries[i].mode, modeLen);
        treeSize += modeLen;

        // append space 
        treeContent[treeSize++] = ' ';

        // append name and null byte
        int nameLen = strlen(entries[i].name);
        memcpy(treeContent + treeSize, entries[i].name, nameLen + 1); // +1 for null byte
        treeSize += nameLen + 1;

        // append rawSha (20 bytes)
        memcpy(treeContent + treeSize, entries[i].rawsha, 20);
        treeSize += 20;
    }
    // Build tree content from entries
    // Create tree object "tree <size>\0<content>"
    // Compute sha, compress, write to .git/objects
    // return sha (for parent to use)
    char *outHash = malloc(41); // 40 chars + null terminator
    writeObject("tree", treeContent, treeSize, outHash);
    free(treeContent);

    return outHash;
}


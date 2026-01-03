#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <zlib.h>
#include <dirent.h>
#include <time.h>
#include "../utils/utils.h"
#include "../storage/object.h"
/*
 * You'll receive exactly one parent commit
 * You'll receive exactly one line in the message
 * You're free to hardcode any valid name/email for the author/committer fields
 * 
 * commit-tree <tree_sha> -p <commit_sha> -m <message>
*/
int commitTree(int argc, char *argv[]) {
    // Create a commit object and print its 40-char SHA-1 hash to stdout
    if (argc < 5) {
        fprintf(stderr, "Usage: commit-tree <tree_sha> -p <commit_sha> -m <message>\n");
        return 1;
    }

    const char *treeSha = argv[2];
    const char *parentSha = argv[4];
    const char *message = argv[6];

    // Hardcode author/committer
    const char *author = "Devin Lynch <devin@lynch.com>";

    // Get current timestamp
    time_t now = time(NULL);
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "%ld +0000", (long)now);

    // Build content 
    char content[4096];
    int len = snprintf(content, sizeof(content),
        "tree %s\n"
        "parent %s\n"
        "author %s %s\n"
        "committer %s %s\n"
        "\n"
        "%s\n",
        treeSha,
        parentSha,
        author, timestamp,
        author, timestamp,
        message
    );

    // Commit object usage; would still have to create a string and pass it to writeObject
    // Commit commit = {
    //     .treesha = (char *)treeSha,
    //     .parentsha = (char *)parentSha,
    //     .author = (char *)author,
    //     .timestamp = (char *)timestamp,
    //     .message = (char *)message
    // };

    // Call writeObject("commit", ...), get sha
    char outHash[41];
    writeObject("commit", (unsigned char *)content, len, outHash); // Reuse content buffer for sha

    printf("%s\n", outHash);
    return 0;
}
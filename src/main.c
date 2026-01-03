#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <openssl/sha.h>
#include <dirent.h>
#include "zlib.h"

#define BUFFER_SIZE 1024 * 1024

#define OBJECTS_DIR ".git/objects"
#define MAX_PATH 128

typedef struct {
    char mode[7];
    char name[256];
    unsigned char sha[20];
} TreeEntry;

TreeEntry entries[1000];
int entry_count = 0;

void write_object(const char* unused_hash, const char* type, const void* content, size_t size);


char* hash_blob(const char* path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        perror("fopen");
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    rewind(fp);

    unsigned char *content = malloc(fsize);
    if (fread(content, 1, fsize, fp) != fsize) {
        perror("fread");
        fclose(fp);
        free(content);
        return NULL;
    }
    fclose(fp);

    // Build full blob: "blob <size>\0<data>"
    char header[64];
    int header_len = sprintf(header, "blob %ld", fsize);
    size_t total_size = header_len + 1 + fsize;
    unsigned char *full_buf = malloc(total_size);
    memcpy(full_buf, header, header_len);
    full_buf[header_len] = '\0';
    memcpy(full_buf + header_len + 1, content, fsize);

    // Compute SHA-1
    unsigned char sha1[20];
    SHA1(full_buf, total_size, sha1);

    // Save object
    write_object(NULL, "blob", full_buf, total_size);

    // Convert to hex
    char *hex = malloc(41);
    for (int i = 0; i < 20; i++) {
        sprintf(hex + i * 2, "%02x", sha1[i]);
    }
    hex[40] = '\0';

    free(content);
    free(full_buf);

    return hex;
}

// Sort entries by name
int compare_entries(const void *a, const void *b) {
    const TreeEntry *ea = a, *eb = b;
    return strcmp(ea->name, eb->name);
}

void write_object(const char* unused_hash, const char* type, const void* content, size_t size) {
    // Compute SHA-1 of full buffer
    unsigned char sha1[20];
    SHA1((const unsigned char*)content, size, sha1);

    // Convert SHA to hex
    char hex[41];
    for (int i = 0; i < 20; i++) {
        sprintf(hex + i * 2, "%02x", sha1[i]);
    }
    hex[40] = '\0';

    // Create path: .git/objects/xx/yyyy...
    char dir[64], path[128];
    snprintf(dir, sizeof(dir), ".git/objects/%.2s", hex);
    snprintf(path, sizeof(path), ".git/objects/%.2s/%.38s", hex, hex + 2);

    // Make directory if needed
    mkdir(dir, 0755);

    // Compress content
    unsigned char outbuf[BUFFER_SIZE];
    z_stream zs = {0};
    deflateInit(&zs, Z_DEFAULT_COMPRESSION);
    zs.next_in = (unsigned char*)content;
    zs.avail_in = size;
    zs.next_out = outbuf;
    zs.avail_out = sizeof(outbuf);
    deflate(&zs, Z_FINISH);
    deflateEnd(&zs);

    size_t compressed_size = zs.total_out;

    // Write to file
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror("fopen");
        return;
    }

    fwrite(outbuf, 1, compressed_size, f);
    fclose(f);
}

char *write_tree(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        perror("opendir");
        return NULL;
    }

    TreeEntry entries[1000];
    int entry_count = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || 
            strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, ".git") == 0)
            continue;

        char full_path[MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat st;
        if (lstat(full_path, &st) == -1) {
            perror("lstat");
            continue;
        }

        char *sha1_hex = NULL;
        if (S_ISDIR(st.st_mode)) {
            sha1_hex = write_tree(full_path);
            strcpy(entries[entry_count].mode, "40000");
        } else if (S_ISREG(st.st_mode)) {
            sha1_hex = hash_blob(full_path);
            strcpy(entries[entry_count].mode, "100644");
        } else {
            continue; // ignore other types
        }

        strcpy(entries[entry_count].name, entry->d_name);

        // Convert hex SHA1 to binary
        sscanf(sha1_hex,
               "%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx"
               "%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx",
               &entries[entry_count].sha[0], &entries[entry_count].sha[1],
               &entries[entry_count].sha[2], &entries[entry_count].sha[3],
               &entries[entry_count].sha[4], &entries[entry_count].sha[5],
               &entries[entry_count].sha[6], &entries[entry_count].sha[7],
               &entries[entry_count].sha[8], &entries[entry_count].sha[9],
               &entries[entry_count].sha[10], &entries[entry_count].sha[11],
               &entries[entry_count].sha[12], &entries[entry_count].sha[13],
               &entries[entry_count].sha[14], &entries[entry_count].sha[15],
               &entries[entry_count].sha[16], &entries[entry_count].sha[17],
               &entries[entry_count].sha[18], &entries[entry_count].sha[19]);

        free(sha1_hex);
        entry_count++;
    }

    closedir(dir);

    qsort(entries, entry_count, sizeof(TreeEntry), compare_entries);

    // Build serialized tree
    char *tree_buf = NULL;
    size_t tree_size = 0;
    for (int i = 0; i < entry_count; i++) {
        char entry_buf[4096];
        size_t entry_len = sprintf(entry_buf, "%s %s", entries[i].mode, entries[i].name);
        tree_buf = realloc(tree_buf, tree_size + entry_len + 1 + 20);
        memcpy(tree_buf + tree_size, entry_buf, entry_len);
        tree_buf[tree_size + entry_len] = '\0';
        memcpy(tree_buf + tree_size + entry_len + 1, entries[i].sha, 20);
        tree_size += entry_len + 1 + 20;
    }

    // Add header: "tree <size>\0"
    char header[64];
    int header_len = sprintf(header, "tree %zu", tree_size);
    size_t total_size = header_len + 1 + tree_size;

    char *final_buf = malloc(total_size);
    memcpy(final_buf, header, header_len);
    final_buf[header_len] = '\0';
    memcpy(final_buf + header_len + 1, tree_buf, tree_size);

    // SHA-1 of full buffer
    unsigned char sha1[20];
    SHA1((unsigned char *)final_buf, total_size, sha1);

    // Write to .git/objects
    write_object(NULL, "tree", final_buf, total_size);

    // Convert to hex string
    char *hex = malloc(41);
    for (int i = 0; i < 20; i++) {
        sprintf(hex + i * 2, "%02x", sha1[i]);
    }
    hex[40] = '\0';

    free(tree_buf);
    free(final_buf);

    return hex;
}

void hash_object(const char *filename) {

    // read file
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("fopen failed");
        exit(1);
    }

    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);


    unsigned char *file_content = malloc(filesize);
    fread(file_content, 1, filesize, fp);
    fclose(fp);

    // prepare "blob <size>\0<co;ntent>"
    char header[128];
    int header_len = snprintf(header, sizeof(header), "blob %ld", filesize);

    int total_len = header_len + 1 + filesize;
    unsigned char *store_data = malloc(total_len);
    memcpy(store_data, header, header_len);
    store_data[header_len] = '\0';
    memcpy(store_data + header_len + 1, file_content, filesize);

    unsigned char hash[20];
    SHA1(store_data, total_len, hash);

    char hash_hex[41];
    for (int i = 0; i < 20; i++) {
        sprintf(hash_hex + i * 2, "%02x", hash[i]);
    }

    unsigned char *compressed = malloc(BUFFER_SIZE);
    uLong compressed_size = BUFFER_SIZE;
    compress(compressed, &compressed_size, store_data, total_len);
    free(store_data);

    char dir[64], file_path[128];
    snprintf(dir, sizeof(dir), ".git/objects/%.2s", hash_hex);
    snprintf(file_path, sizeof(file_path), ".git/objects/%.2s/%.38s", hash_hex, hash_hex + 2);

    mkdir(".git", 0755);
    mkdir(".git/objects", 0755);
    mkdir(dir, 0755);

    FILE *out = fopen(file_path, "wb");
    if (!out) {
        perror("fopen failed");
        exit(1);
    }

    fwrite(compressed, 1, compressed_size, out);
    fclose(out);
    free(compressed);

    printf("%s\n", hash_hex);

}

void sha_to_hex(const unsigned char *sha_bin, char *sha_hex) {
    for (int i = 0; i < 20; i++) {
        sprintf(sha_hex + (i * 2), "%02x", sha_bin[i]);
    }
    sha_hex[40] = '\0';
}

void read_tree_object(char *tree_sha) {
    if (strlen(tree_sha) != 40) {
        fprintf(stderr, "Usage: ./main ls-tree --name-only <tree-sha>\n");
        return;
    }

    // Construct path to .git/objects/xx/yyyy...
    char path[256];
    snprintf(path, sizeof(path), ".git/objects/%.2s/%.38s", tree_sha, tree_sha + 2);

    FILE *file = fopen(path, "rb");
    if (!file) {
        perror("Failed to open object file");
        return;
    }

    // Get file size
    struct stat st;
    if (stat(path, &st) != 0) {
        perror("stat failed");
        fclose(file);
        return;
    }

    size_t compressed_size = st.st_size;
    unsigned char *compressed_data = malloc(compressed_size);
    if (!compressed_data) {
        fprintf(stderr, "Memory allocation failed (compressed)\n");
        fclose(file);
        return;
    }

    fread(compressed_data, 1, compressed_size, file);
    fclose(file);

    unsigned char *decompressed_data = malloc(BUFFER_SIZE);
    if (!decompressed_data) {
        fprintf(stderr, "Memory allocation failed (decompressed)\n");
        free(compressed_data);
        return;
    }

    z_stream stream = {0};
    stream.next_in = compressed_data;
    stream.avail_in = compressed_size;
    stream.next_out = decompressed_data;
    stream.avail_out = BUFFER_SIZE;

    if (inflateInit(&stream) != Z_OK) {
        fprintf(stderr, "inflateInit failed\n");
        free(compressed_data);
        free(decompressed_data);
        return;
    }

    if (inflate(&stream, Z_FINISH) != Z_STREAM_END) {
        fprintf(stderr, "inflate failed\n");
        inflateEnd(&stream);
        free(compressed_data);
        free(decompressed_data);
        return;
    }

    inflateEnd(&stream);
    free(compressed_data);

    size_t total_out = stream.total_out;
    unsigned char *start = decompressed_data;

    // Skip "tree <size>\0" header
    unsigned char *p = memchr(start, '\0', total_out);
    if (!p) {
        fprintf(stderr, "Malformed object: no null byte after header\n");
        free(decompressed_data);
        return;
    }
    p++;  // Move past null byte
    size_t remaining = total_out - (p - start);

    // Parse tree entries
    while (remaining > 0) {
        // Read mode
        char mode[8] = {0};
        int i = 0;
        while (remaining > 0 && *p != ' ' && i < 7) {
            mode[i++] = *p++;
            remaining--;
        }
        mode[i] = '\0';

        if (remaining == 0 || *p != ' ') {
            fprintf(stderr, "Malformed entry: no space after mode\n");
            break;
        }
        p++; remaining--;  // skip space

        // Read filename
        char filename[256] = {0};
        i = 0;
        while (remaining > 0 && *p != '\0' && i < 255) {
            filename[i++] = *p++;
            remaining--;
        }
        filename[i] = '\0';

        if (remaining == 0 || *p != '\0') {
            fprintf(stderr, "Malformed entry: filename not null-terminated\n");
            break;
        }
        p++; remaining--;  // skip null byte

        // Read 20-byte SHA-1
        if (remaining < 20) {
            fprintf(stderr, "Malformed entry: not enough bytes for SHA\n");
            break;
        }
        unsigned char sha_bin[20];
        memcpy(sha_bin, p, 20);
        p += 20;
        remaining -= 20;

        // Convert SHA to hex if needed
        // char sha_hex[41];
        // sha_to_hex(sha_bin, sha_hex);

        printf("%s\n", filename);
    }

    free(decompressed_data);
}

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
        
        if (mkdir(".git", 0755) == -1 || 
            mkdir(".git/objects", 0755) == -1 || 
            mkdir(".git/refs", 0755) == -1) {
            fprintf(stderr, "Failed to create directories: %s\n", strerror(errno));
            return 1;
        }
        
        FILE *headFile = fopen(".git/HEAD", "w");
        if (headFile == NULL) {
            fprintf(stderr, "Failed to create .git/HEAD file: %s\n", strerror(errno));
            return 1;
        }
        fprintf(headFile, "ref: refs/heads/main\n");
        fclose(headFile);
        
        printf("Initialized git directory\n");
    } else if (argc >3 && strcmp(command, "cat-file") == 0 && strcmp(argv[2], "-p") == 0) {
        char *hash = argv[3];
        if (strlen(hash) != 40) {
            fprintf(stderr, "Failed to decompress hash value.");
            return 1;
        }

        char path[256];
        snprintf(path, sizeof(path), ".git/objects/%.2s/%.38s", hash, hash + 2);

        FILE *file = fopen(path, "rb");
        if (!file) {
            perror("Failed to open object file");
            return 1;
        }

        // Get file size
        struct stat st;
        stat(path, &st);
        size_t compressed_size = st.st_size;

        // Read compressed content
        unsigned char *compressed_data = malloc(compressed_size);
        fread(compressed_data, 1, compressed_size, file);
        fclose(file);

        // Decompress using zlib
        unsigned char *decompressed_data = malloc(BUFFER_SIZE);
        z_stream stream;
        memset(&stream, 0, sizeof(stream));
        stream.next_in = compressed_data;
        stream.avail_in = compressed_size;
        stream.next_out = decompressed_data;
        stream.avail_out = BUFFER_SIZE;

        if (inflateInit(&stream) != Z_OK) {
            fprintf(stderr, "inflateInit failed\n");
            return 1;
        }

        if (inflate(&stream, Z_FINISH) != Z_STREAM_END) {
            fprintf(stderr, "inflate failed\n");
            return 1;
        }

        inflateEnd(&stream);
        free(compressed_data);

        // Find the first null byte to separate header and content
        unsigned char *null_byte = memchr(decompressed_data, '\0', stream.total_out);
        if (!null_byte) {
            fprintf(stderr, "Invalid blob format (no null byte found).\n");
            return 1;
        }

        // Print only the content (after null byte)
        size_t header_len = null_byte - decompressed_data;
        size_t content_len = stream.total_out - header_len - 1;

        fwrite(null_byte + 1, 1, content_len, stdout);
        free(decompressed_data);

    } else if(argc > 3 && strcmp(command, "hash-object") == 0 && strcmp(argv[2], "-w") == 0) {
        hash_object(argv[3]);
    } else if(argc > 3 && strcmp(command, "ls-tree") == 0 && strcmp(argv[2], "--name-only") == 0) {
        read_tree_object(argv[3]);
    } else if (argc > 1 && strcmp(command, "write-tree") == 0) {
        entry_count = 0;
        char *root_sha = write_tree(".");
        printf("%s\n", root_sha);
        free(root_sha);
    } else  {
        fprintf(stderr, "Unknown command %s\n, with argc: %d\n", command, argc);
        // printf("%s\n", command);
        // printf("%s\n", argv[2]);
        // printf("%s\n", argv[3]);
        return 1;
    }
    
    return 0;
}
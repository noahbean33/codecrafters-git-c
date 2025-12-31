#include <openssl/sha.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdbool.h>
#include <zconf.h>
#include <zlib.h>
#include <openssl/ssl.h>

bool equal(char* s1, char* s2) {
    return strcmp(s1, s2) == 0;
}

void command_init() {
    if (mkdir(".git", 0755) == -1 || 
        mkdir(".git/objects", 0755) == -1 || 
        mkdir(".git/refs", 0755) == -1) {
        fprintf(stderr, "Failed to create directories: %s\n", strerror(errno));
        exit(1);
    }
    
    FILE *headFile = fopen(".git/HEAD", "w");
    if (headFile == NULL) {
        fprintf(stderr, "Failed to create .git/HEAD file: %s\n", strerror(errno));
        exit(1);
    }
    fprintf(headFile, "ref: refs/heads/main\n");
    fclose(headFile);
    
    printf("Initialized git directory\n");
}

void command_cat_file(char* flag, char* blob_sha) {
    char file_path[128];
    sprintf(file_path, ".git/objects/%.2s/%s", blob_sha, (blob_sha + 2));

    FILE *fp = fopen(file_path, "r");
    if (!fp) {
        fprintf(stderr, "Error opening file!");
        exit(65);
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    rewind(fp);

    Bytef* compressed_data = malloc(file_size);
    fread(compressed_data, 1, file_size, fp);
    fclose(fp);

    uLongf decompressed_size = 4096;
    Bytef* decompressed_data = malloc(decompressed_size);

    int res = uncompress(decompressed_data, &decompressed_size, compressed_data, file_size);
    
    if (res == Z_BUF_ERROR) {
        fprintf(stderr, "Buffer error occurred!");
        exit(70);
    }

    if (res != Z_OK) {
        fprintf(stderr, "Decompression failed!");
        exit(90);
    }

    char* ptr = memchr(decompressed_data, '\0', decompressed_size) + 1;
    size_t content_length = decompressed_size - (ptr - (char*)decompressed_data);

    fwrite(ptr, 1, content_length, stdout);

    free(compressed_data);
    free(decompressed_data);
}

void command_hash_object(char* flag, char* file_path) {
    FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        fprintf(stderr, "Failed to open file");
        exit(78);
    }
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    rewind(fp);

    char *content = malloc(file_size);
    fread(content, 1, file_size, fp);
    fclose(fp);

    char header[64];
    int header_len = snprintf(header, sizeof(header), "blob %ld", file_size);

    int full_len = header_len + 1 + file_size;
    unsigned char* buffer = malloc(full_len);
    memcpy(buffer, header, header_len);
    buffer[header_len] = '\0';
    memcpy(buffer + header_len + 1, content, file_size);

    unsigned char sha1_hash[SHA_DIGEST_LENGTH];
    SHA1(buffer, full_len, sha1_hash);

    char sha1_hex[41];
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
        sprintf(sha1_hex + i * 2, "%02x", sha1_hash[i]);
    }
    sha1_hex[40] = '\0';

    uLong compressed_size = compressBound(full_len);
    Bytef* compressed_data = malloc(compressed_size);

    int status = compress(compressed_data, &compressed_size, (Bytef*)buffer, full_len);
    if (status != Z_OK) {
        fprintf(stderr, "Compression failed!");
        exit(99);
    }

    char dir[64];
    snprintf(dir, sizeof(dir), ".git/objects/%.2s", sha1_hex);
    mkdir(dir, 0755);

    char file[128];
    sprintf(file, ".git/objects/%.2s/%s", sha1_hex, sha1_hex + 2);

    FILE* ffp = fopen(file, "wb");

    fwrite(compressed_data, 1, compressed_size, ffp);
    fclose(ffp);

    printf("%s\n", sha1_hex);

    free(buffer);
    free(compressed_data);
    free(content);
}

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    if (argc < 2) {
        fprintf(stderr, "Usage: ./your_program.sh <command> [<args>]\n");
        return 1;
    }
    
    char *command = argv[1];
    
    if (equal(command, "init")) {
        command_init();
    } else if (equal(command, "cat-file")) {
        command_cat_file(argv[2], argv[3]);
    } else if (equal(command, "hash-object")) {
        command_hash_object(argv[2], argv[3]);
    } else {
        fprintf(stderr, "Unknown command %s\n", command);
        return 1;
    }
    
    return 0;
}
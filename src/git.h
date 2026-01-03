#ifndef GIT_H
#define GIT_H

#include <curl/curl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

// Git object types
#define GIT_COMMIT "commit"
#define GIT_TREE "tree"
#define GIT_BLOB "blob"

// Common paths
#define GIT_DIR ".git"
#define OBJECTS_DIR ".git/objects"
#define REFS_DIR ".git/refs"

// Zlib

#define HASH_LENGTH 40
#define COMPRESSED_CHUNK_SIZE 1024

// SHA-1

#define SHA_DIGEST_LENGTH 20 // Raw bytes
#define GIT_HASH_LENGTH 40   // Hex string length

// Git objects

#define GIT_HEADER_LENGTH 100
#define GIT_MODE_TREE "040000"
#define GIT_MODE_BLOB "100644"

// Pack file constants
#define PACK_SIGNATURE 0x5041434B // "PACK"
#define PACK_VERSION 2
#define PACK_HEADER_SIZE 12

// Object types in pack
#define OBJ_COMMIT 1
#define OBJ_TREE 2
#define OBJ_BLOB 3
#define OBJ_TAG 4
#define OBJ_OFS_DELTA 6
#define OBJ_REF_DELTA 7

// Pack parsing constants
#define TYPE_MASK 0x70
#define TYPE_SHIFT 4
#define SIZE_MASK 0x0F
#define SIZE_SHIFT 7

// Git protocol constants
#define GIT_PROTOCOL_VERSION "Git-Protocol: version=2"
#define GIT_UPLOAD_PACK_SERVICE "git-upload-pack"
#define GIT_INFO_REFS_PATH "/info/refs?service=git-upload-pack"

// Buffer sizes
#define INITIAL_BUFFER_SIZE 8192
#define URL_BUFFER_SIZE 1024

typedef struct {
  char *type;
  size_t size;
  char *content;
} git_object;

typedef struct {
  char mode[7];
  char *name;
  char *hash;
} tree_entry;

typedef struct {
  size_t count;
  tree_entry *entries;
} tree_object;

// SHA-1

char *compute_sha1(const char *data, size_t len);

// Blobs
git_object *read_object(const char *hash);
void free_git_object(git_object *obj);
char *get_object_path(const char *hash);
char *create_blob_from_file(const char *filepath);
int store_object(const char *hash, const char *data, size_t len);
int write_compressed_object(const char *path, const char *data, size_t len);

// Trees

tree_object *parse_tree_object(git_object *obj);
void free_tree_object(tree_object *tree);
int handle_write_tree(void);
char *write_tree_recursive(const char *dir_path);
int should_ignore_path(const char *path);

// Commits
char *create_commit_object(const char *tree_sha, const char *parent_sha,
                           const char *message);

// Clone repo
int checkout_tree(const char *tree_hash, const char *prefix);

struct PackFile {
  char *data;
  size_t size;
  char *commit_hash;
};

int handle_clone(int argc, char *argv[]);
size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata);
char *get_remote_refs(const char *url);
struct PackFile *fetch_pack(const char *url);
int process_pack_file(const char *pack_data, size_t pack_size);

// Function declarations
int handle_init(void);
int handle_cat_file(int argc, char *argv[]);
int handle_hash_object(int argc, char *argv[]);
int handle_ls_tree(int argc, char *argv[]);
int handle_commit_tree(int argc, char *argv[]);

#endif // GIT_H

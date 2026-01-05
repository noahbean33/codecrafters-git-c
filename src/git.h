/**
 * git.h - Git Implementation Header File
 * 
 * This header file contains all constants, data structures, and function
 * declarations for the Git implementation. It defines the core API for
 * Git object manipulation, repository management, and remote operations.
 */

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

/*
 * Git Object Types
 * These strings identify the type of Git objects stored in .git/objects
 */
#define GIT_COMMIT "commit"
#define GIT_TREE "tree"
#define GIT_BLOB "blob"

/*
 * Common Repository Paths
 * Standard directories in a Git repository
 */
#define GIT_DIR ".git"
#define OBJECTS_DIR ".git/objects"
#define REFS_DIR ".git/refs"

/*
 * Compression and Hashing Constants
 */
#define HASH_LENGTH 40              // Length of hex-encoded SHA-1 hash
#define COMPRESSED_CHUNK_SIZE 1024  // Chunk size for zlib operations

/*
 * SHA-1 Hash Sizes
 */
#define SHA_DIGEST_LENGTH 20  // Raw binary hash (20 bytes)
#define GIT_HASH_LENGTH 40    // Hex-encoded hash string (40 chars)

/*
 * Git Object Format Constants
 */
#define GIT_HEADER_LENGTH 100   // Max length of object header
#define GIT_MODE_TREE "040000"  // File mode for directories
#define GIT_MODE_BLOB "100644"  // File mode for regular files

/*
 * Git Pack File Constants
 * Pack files are used for efficient storage and transfer of Git objects
 */
#define PACK_SIGNATURE 0x5041434B  // "PACK" in hex
#define PACK_VERSION 2             // Pack file version
#define PACK_HEADER_SIZE 12        // Size of pack header in bytes

/*
 * Object Types in Pack Files
 * These numeric codes identify object types in compressed pack format
 */
#define OBJ_COMMIT 1      // Commit object
#define OBJ_TREE 2        // Tree object
#define OBJ_BLOB 3        // Blob object
#define OBJ_TAG 4         // Tag object
#define OBJ_OFS_DELTA 6   // Delta with offset to base
#define OBJ_REF_DELTA 7   // Delta with reference to base

/*
 * Pack Parsing Bit Masks
 * Used to extract type and size from pack file headers
 */
#define TYPE_MASK 0x70    // Mask for object type bits
#define TYPE_SHIFT 4      // Bit shift for object type
#define SIZE_MASK 0x0F    // Mask for size bits
#define SIZE_SHIFT 7      // Bit shift for size continuation

/*
 * Git Network Protocol Constants
 * Used for HTTP communication with remote repositories
 */
#define GIT_PROTOCOL_VERSION "Git-Protocol: version=2"
#define GIT_UPLOAD_PACK_SERVICE "git-upload-pack"
#define GIT_INFO_REFS_PATH "/info/refs?service=git-upload-pack"

/*
 * Buffer Size Constants
 */
#define INITIAL_BUFFER_SIZE 8192  // Initial allocation for dynamic buffers
#define URL_BUFFER_SIZE 1024      // Maximum URL length

/**
 * Git Object Structure
 * Represents a decompressed Git object with its type, size, and content.
 */
typedef struct {
  char *type;      // Object type: "blob", "tree", or "commit"
  size_t size;     // Size of content in bytes
  char *content;   // Object content (null-terminated for text objects)
} git_object;

/**
 * Tree Entry Structure
 * Represents a single entry in a Git tree object (file or directory).
 */
typedef struct {
  char mode[7];   // File mode (e.g., "100644" for files, "040000" for dirs)
  char *name;     // Entry name (filename or directory name)
  char *hash;     // SHA-1 hash of the referenced object (40 hex chars)
} tree_entry;

/**
 * Tree Object Structure
 * Represents a parsed Git tree object containing multiple entries.
 */
typedef struct {
  size_t count;        // Number of entries in the tree
  tree_entry *entries; // Array of tree entries
} tree_object;

/*
 * ============================================================================
 * SHA-1 Hashing Functions
 * ============================================================================
 */

/**
 * Compute SHA-1 hash of data and return as hex string.
 */
char *compute_sha1(const char *data, size_t len);

/*
 * ============================================================================
 * Git Object Storage and Retrieval Functions
 * ============================================================================
 */

/** Read and decompress a Git object from the object database. */
git_object *read_object(const char *hash);

/** Free memory allocated for a git_object structure. */
void free_git_object(git_object *obj);

/** Get filesystem path for an object given its hash. */
char *get_object_path(const char *hash);

/** Create a blob object from a file and store it. */
char *create_blob_from_file(const char *filepath);

/** Store a Git object in the object database. */
int store_object(const char *hash, const char *data, size_t len);

/** Write compressed data to a file using zlib. */
int write_compressed_object(const char *path, const char *data, size_t len);

/*
 * ============================================================================
 * Tree Object Functions
 * ============================================================================
 */

/** Parse a git_object into a tree_object structure. */
tree_object *parse_tree_object(git_object *obj);

/** Free memory allocated for a tree_object structure. */
void free_tree_object(tree_object *tree);

/** Handle the write-tree command. */
int handle_write_tree(void);

/** Recursively create tree objects from a directory. */
char *write_tree_recursive(const char *dir_path);

/** Check if a path should be ignored (e.g., .git directory). */
int should_ignore_path(const char *path);

/*
 * ============================================================================
 * Commit Object Functions
 * ============================================================================
 */

/** Create a commit object with tree, parent, and message. */
char *create_commit_object(const char *tree_sha, const char *parent_sha,
                           const char *message);

/*
 * ============================================================================
 * Clone and Checkout Functions
 * ============================================================================
 */

/** Recursively checkout a tree to the working directory. */
int checkout_tree(const char *tree_hash, const char *prefix);

/**
 * Pack File Structure
 * Holds pack file data and associated commit hash for clone operations.
 */
struct PackFile {
  char *data;         // Pack file data
  size_t size;        // Size of pack file
  char *commit_hash;  // Commit hash being cloned
};

/** Handle the clone command. */
int handle_clone(int argc, char *argv[]);

/** libcurl write callback for receiving HTTP data. */
size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata);

/** Fetch remote repository references. */
char *get_remote_refs(const char *url);

/** Fetch pack file from remote repository. */
struct PackFile *fetch_pack(const char *url);

/** Process and extract objects from pack file. */
int process_pack_file(const char *pack_data, size_t pack_size);

/*
 * ============================================================================
 * Git Command Handler Functions
 * ============================================================================
 */

/** Initialize a new Git repository. */
int handle_init(void);

/** Display contents of a Git object. */
int handle_cat_file(int argc, char *argv[]);

/** Compute hash of file and store as blob. */
int handle_hash_object(int argc, char *argv[]);

/** List contents of a tree object. */
int handle_ls_tree(int argc, char *argv[]);

/** Create a commit object. */
int handle_commit_tree(int argc, char *argv[]);

#endif // GIT_H

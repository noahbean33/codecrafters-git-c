/**
 * objects.c - Git Object Storage and Manipulation
 * 
 * This file implements the core Git object model including:
 *   - Blob objects (file content storage)
 *   - Tree objects (directory structure)
 *   - Commit objects (version history)
 * 
 * All objects are stored in .git/objects using zlib compression
 * and identified by their SHA-1 hash.
 * 
 * Object storage format:
 *   - Path: .git/objects/XX/YYYYYY... (XX = first 2 chars of hash)
 *   - Format: <type> <size>\0<content> (zlib compressed)
 */

#include "git.h"
#include <dirent.h> // For DIR, struct dirent
#include <limits.h> // For PATH_MAX
#include <openssl/sha.h>
#include <time.h>

/**
 * Construct the filesystem path for a Git object from its hash.
 * Git objects are stored in .git/objects/XX/YYYYYYYY where XX is the
 * first 2 characters of the hash.
 * 
 * @param hash 40-character SHA-1 hash in hexadecimal
 * @return Dynamically allocated path string (caller must free)
 */
char *get_object_path(const char *hash) {
  char *path = malloc(strlen(OBJECTS_DIR) + HASH_LENGTH + 2);
  sprintf(path, "%s/%.2s/%s", OBJECTS_DIR, hash, hash + 2);
  return path;
}

/**
 * Read and decompress a Git object from the object database.
 * This function handles zlib decompression and parses the object header
 * to extract type, size, and content.
 * 
 * @param hash 40-character SHA-1 hash of the object
 * @return Pointer to git_object structure, or NULL on error (caller must free)
 */
git_object *read_object(const char *hash) {
  // Get the filesystem path for this object
  const char *path = get_object_path(hash);
  if (!path)
    return NULL;

  // Open the compressed object file
  FILE *file = fopen(path, "rb");
  if (!file)
    return NULL;

  // Allocate buffers for compression/decompression
  unsigned char *in = malloc(COMPRESSED_CHUNK_SIZE);
  unsigned char *out = malloc(COMPRESSED_CHUNK_SIZE);
  z_stream strm = {0};

  // Initialize zlib decompression
  if (inflateInit(&strm) != Z_OK) {
    free(in);
    free(out);
    fclose(file);
    return NULL;
  }

  git_object *obj = malloc(sizeof(git_object));
  obj->type = NULL;
  obj->content = NULL;
  obj->size = 0;

  size_t total_size = 0;
  unsigned char *buffer = NULL;

  // Decompress the object data in chunks
  do {
    strm.avail_in = fread(in, 1, COMPRESSED_CHUNK_SIZE, file);
    strm.next_in = in;

    do {
      strm.avail_out = COMPRESSED_CHUNK_SIZE;
      strm.next_out = out;

      // Inflate the next chunk of data
      int ret = inflate(&strm, Z_NO_FLUSH);
      if (ret != Z_OK && ret != Z_STREAM_END) {
        free_git_object(obj);
        free(buffer);
        inflateEnd(&strm);
        fclose(file);
        return NULL;
      }

      // Append decompressed data to buffer
      size_t have = COMPRESSED_CHUNK_SIZE - strm.avail_out;
      buffer = realloc(buffer, total_size + have);
      memcpy(buffer + total_size, out, have);
      total_size += have;

    } while (strm.avail_out == 0);
  } while (strm.avail_in != 0);

  // Parse object header: "<type> <size>\0<content>"
  char *space = memchr(buffer, ' ', total_size);
  char *null = memchr(space + 1, 0, total_size - (space - (char *)buffer));

  // Extract object type (blob, tree, or commit)
  obj->type = strndup((char *)buffer, space - (char *)buffer);
  // Extract object size
  obj->size = atoi(space + 1);

  // Extract object content (everything after the null byte)
  size_t header_size = null - (char *)buffer + 1;
  obj->content = malloc(total_size - header_size);
  memcpy(obj->content, buffer + header_size, total_size - header_size);

  free(buffer);
  inflateEnd(&strm);
  fclose(file);
  free(in);
  free(out);

  return obj;
}

/**
 * Free a git_object structure and all its allocated members.
 * 
 * @param obj Pointer to git_object to free
 */
void free_git_object(git_object *obj) {
  if (obj) {
    free(obj->type);
    free(obj->content);
    free(obj);
  }
}

/**
 * Compute SHA-1 hash of data and return as a 40-character hex string.
 * 
 * @param data Input data to hash
 * @param len Length of input data
 * @return Dynamically allocated hex string (caller must free)
 */
char *sha1_hash(const char *data, size_t len) {
  // Compute SHA-1 hash (20 bytes)
  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1((unsigned char *)data, len, hash);

  // Convert binary hash to 40-character hexadecimal string
  char *hex = malloc(41);
  for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
    sprintf(hex + (i * 2), "%02x", hash[i]);
  }
  return hex;
}

/**
 * Write data to a file with zlib compression.
 * Used to store Git objects in the compressed format.
 * 
 * @param path Filesystem path to write to
 * @param data Data to compress and write
 * @param len Length of data
 * @return 0 on success, 1 on error
 */
int write_compressed_object(const char *path, const char *data, size_t len) {
  FILE *f = fopen(path, "wb");
  if (!f)
    return 1;

  // Initialize zlib compression
  z_stream strm = {0};
  if (deflateInit(&strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
    fclose(f);
    return 1;
  }

  unsigned char out[COMPRESSED_CHUNK_SIZE];
  strm.next_in = (unsigned char *)data;
  strm.avail_in = len;

  // Compress and write data in chunks
  do {
    strm.avail_out = COMPRESSED_CHUNK_SIZE;
    strm.next_out = out;
    deflate(&strm, Z_FINISH);
    size_t have = COMPRESSED_CHUNK_SIZE - strm.avail_out;
    fwrite(out, 1, have, f);
  } while (strm.avail_out == 0);

  deflateEnd(&strm);
  fclose(f);
  return 0;
}

/**
 * Create a Git blob object from a file.
 * Reads file contents, creates blob header, computes SHA-1 hash,
 * and stores the compressed object in .git/objects.
 * 
 * @param filepath Path to the file to create blob from
 * @return SHA-1 hash as 40-char hex string, or NULL on error (caller must free)
 */
char *create_blob_from_file(const char *filepath) {
  FILE *f = fopen(filepath, "rb");
  if (!f) {
    fprintf(stderr, "Cannot open file\n");
    return NULL;
  }

  // Determine file size
  fseek(f, 0, SEEK_END);
  size_t file_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  // Read entire file into memory
  char *content = malloc(file_size);
  fread(content, 1, file_size, f);
  fclose(f);

  // Create Git blob header: "blob <size>\0"
  char header[GIT_HEADER_LENGTH];
  int header_len = sprintf(header, "blob %zu", file_size);
  header[header_len++] = '\0';

  // Combine header and content for hashing and storage
  size_t total_len = header_len + file_size;
  char *data = malloc(total_len);
  memcpy(data, header, header_len);
  memcpy(data + header_len, content, file_size);

  // Calculate SHA-1 hash of the complete blob
  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1((unsigned char *)data, total_len, hash);

  // Convert binary hash to hexadecimal string
  char *hex = malloc(GIT_HASH_LENGTH + 1);
  for (int i = 0; i < 20; i++) {
    sprintf(hex + (i * 2), "%02x", hash[i]);
  }

  // Store the compressed object in .git/objects
  store_object(hex, data, total_len);

  // Cleanup
  free(data);
  free(content);

  return hex;
}

/**
 * Store a Git object in the object database.
 * Creates the necessary directory and writes the compressed object.
 * 
 * @param hash 40-character SHA-1 hash
 * @param data Object data (header + content)
 * @param len Length of data
 * @return 0 on success, non-zero on error
 */
int store_object(const char *hash, const char *data, size_t len) {
  // Create directory .git/objects/XX (where XX = first 2 chars of hash)
  char *dir = malloc(strlen(OBJECTS_DIR) + 4);
  sprintf(dir, "%s/%.2s", OBJECTS_DIR, hash);
  mkdir(dir, 0755); // OK if directory already exists
  free(dir);

  // Write the compressed object file
  char *path = get_object_path(hash);
  int result = write_compressed_object(path, data, len);
  free(path);

  return result;
}

/**
 * Parse a Git tree object into a structured format.
 * Tree format: <mode> <name>\0<20-byte-sha1> (repeated for each entry)
 * 
 * @param obj Git object to parse (must be of type "tree")
 * @return Pointer to tree_object, or NULL on error (caller must free)
 */
tree_object *parse_tree_object(git_object *obj) {
  if (!obj || strcmp(obj->type, GIT_TREE) != 0) {
    return NULL;
  }

  tree_object *tree = malloc(sizeof(tree_object));
  tree->count = 0;
  tree->entries = NULL;

  const unsigned char *content = (const unsigned char *)obj->content;
  size_t pos = 0;
  // Start with space for 16 entries, dynamically expand as needed
  size_t max_entries = 16;
  tree->entries = malloc(sizeof(tree_entry) * max_entries);

  // Parse each tree entry
  while (pos < obj->size) {
    // Dynamically expand array if needed
    if (tree->count == max_entries) {
      max_entries *= 2;
      tree->entries = realloc(tree->entries, sizeof(tree_entry) * max_entries);
    }

    // Read file mode (e.g., "100644" for files, "40000" for directories)
    char mode[7];
    int mode_pos = 0;
    while (content[pos] != ' ' && mode_pos < 6) {
      mode[mode_pos++] = content[pos++];
    }
    mode[mode_pos] = '\0';
    pos++; // Skip space separator

    // Read entry name (null-terminated string)
    char *name_start = (char *)content + pos;
    while (content[pos] != '\0')
      pos++;
    size_t name_len = ((char *)content + pos) - name_start;

    // Allocate and copy name
    tree->entries[tree->count].name = malloc(name_len + 1);
    memcpy(tree->entries[tree->count].name, name_start, name_len);
    tree->entries[tree->count].name[name_len] = '\0';

    // Copy mode to entry
    strcpy(tree->entries[tree->count].mode, mode);

    // Skip null byte separator
    pos++;

    // Read 20-byte binary SHA-1 hash and convert to hex
    tree->entries[tree->count].hash = malloc(41);
    for (int i = 0; i < 20; i++) {
      sprintf(tree->entries[tree->count].hash + (i * 2), "%02x",
              content[pos + i]);
    }
    pos += 20;

    tree->count++;
  }

  return tree;
}

/**
 * Free a tree_object structure and all its allocated members.
 * 
 * @param tree Pointer to tree_object to free
 */
void free_tree_object(tree_object *tree) {
  if (!tree)
    return;

  for (size_t i = 0; i < tree->count; i++) {
    free(tree->entries[i].name);
    free(tree->entries[i].hash);
  }
  free(tree->entries);
  free(tree);
}

/**
 * Determine if a path should be ignored when creating tree objects.
 * Currently ignores .git directory and special entries (. and ..).
 * 
 * @param path Path to check
 * @return 1 if should ignore, 0 otherwise
 */
int should_ignore_path(const char *path) {
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  return strcmp(base, ".git") == 0 || strcmp(base, ".") == 0 ||
         strcmp(base, "..") == 0;
}

/**
 * Recursively create tree objects from a directory.
 * Traverses the directory structure, creates blob objects for files,
 * and recursively creates tree objects for subdirectories.
 * 
 * @param dir_path Path to the directory to process
 * @return SHA-1 hash of the created tree object (caller must free)
 */
char *write_tree_recursive(const char *dir_path) {
  DIR *dir = opendir(dir_path);
  if (!dir)
    return NULL;

  struct dirent *entry;
  size_t entries_capacity = 16;
  size_t entries_count = 0;
  tree_entry *entries = malloc(sizeof(tree_entry) * entries_capacity);

  // First pass: collect all directory entries
  while ((entry = readdir(dir))) {
    char full_path[PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

    // Skip .git and other ignored paths
    if (should_ignore_path(full_path))
      continue;

    // Expand array if needed
    if (entries_count == entries_capacity) {
      entries_capacity *= 2;
      entries = realloc(entries, sizeof(tree_entry) * entries_capacity);
    }

    // Get file/directory metadata
    struct stat st;
    if (lstat(full_path, &st) == -1)
      continue;

    entries[entries_count].name = strdup(entry->d_name);

    // Handle directories recursively
    if (S_ISDIR(st.st_mode)) {
      strcpy(entries[entries_count].mode, "40000");
      entries[entries_count].hash = write_tree_recursive(full_path);
    } else if (S_ISREG(st.st_mode)) {
      // Create blob for regular files
      strcpy(entries[entries_count].mode, "100644");
      entries[entries_count].hash = create_blob_from_file(full_path);
    } else {
      // Skip special files (symlinks, devices, etc.)
      free(entries[entries_count].name);
      continue;
    }

    entries_count++;
  }
  closedir(dir);

  // Sort entries by name (Git requirement)
  for (size_t i = 0; i < entries_count; i++) {
    for (size_t j = i + 1; j < entries_count; j++) {
      if (strcmp(entries[i].name, entries[j].name) > 0) {
        tree_entry temp = entries[i];
        entries[i] = entries[j];
        entries[j] = temp;
      }
    }
  }

  // Calculate total size of tree content
  size_t total_size = 0;
  for (size_t i = 0; i < entries_count; i++) {
    // mode + space + name + null + 20-byte hash
    total_size +=
        strlen(entries[i].mode) + 1 + strlen(entries[i].name) + 1 + 20;
  }

  // Build tree content in binary format
  char *content = malloc(total_size);
  size_t pos = 0;

  for (size_t i = 0; i < entries_count; i++) {
    // Write mode and space separator
    pos += sprintf(content + pos, "%s ", entries[i].mode);

    // Write filename and null separator
    size_t name_len = strlen(entries[i].name);
    memcpy(content + pos, entries[i].name, name_len);
    pos += name_len;
    content[pos++] = '\0';

    // Convert hash from hex string to 20-byte binary and write
    for (int j = 0; j < 40; j += 2) {
      unsigned int byte;
      sscanf(entries[i].hash + j, "%2x", &byte);
      content[pos++] = byte;
    }
  }

  // Create tree object header: "tree <size>\0"
  char header[100];
  int header_len = sprintf(header, "tree %zu", total_size);
  header[header_len++] = '\0';

  // Combine header and tree content
  size_t full_size = header_len + total_size;
  char *full_content = malloc(full_size);
  memcpy(full_content, header, header_len);
  memcpy(full_content + header_len, content, total_size);

  // Calculate hash and store object
  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1((unsigned char *)full_content, full_size, hash);

  char *hex_hash = malloc(41);
  for (int i = 0; i < 20; i++) {
    sprintf(hex_hash + (i * 2), "%02x", hash[i]);
  }

  store_object(hex_hash, full_content, full_size);

  // Cleanup
  free(full_content);
  free(content);
  for (size_t i = 0; i < entries_count; i++) {
    free(entries[i].name);
    free(entries[i].hash);
  }
  free(entries);

  return hex_hash;
}

/**
 * Handler for the write-tree command.
 * Creates a tree object representing the current working directory.
 * 
 * @return 0 on success, 1 on error
 */
int handle_write_tree(void) {
  // Start recursive tree creation from current directory
  char *hash = write_tree_recursive(".");
  if (!hash) {
    fprintf(stderr, "Failed to write tree\n");
    return 1;
  }

  printf("%s\n", hash);
  free(hash);
  return 0;
}

/**
 * Create a Git commit object.
 * Generates a commit with tree reference, optional parent, author info,
 * timestamp, and commit message.
 * 
 * @param tree_sha SHA-1 hash of the tree object
 * @param parent_sha SHA-1 hash of parent commit (NULL for initial commit)
 * @param message Commit message
 * @return SHA-1 hash of created commit (caller must free)
 */
char *create_commit_object(const char *tree_sha, const char *parent_sha,
                           const char *message) {
  // Get current timestamp for commit
  time_t now = time(NULL);

  // Build commit content in Git's format
  char *content = malloc(1024); // Reasonable initial size
  int pos = 0;

  // Write tree reference
  pos += sprintf(content + pos, "tree %s\n", tree_sha);

  // Write parent reference if this is not the initial commit
  if (parent_sha) {
    pos += sprintf(content + pos, "parent %s\n", parent_sha);
  }

  // Write author and committer information
  const char *author = "CodeCrafter <codecrafter@example.com>";
  pos += sprintf(content + pos, "author %s %ld +0000\n", author, now);
  pos += sprintf(content + pos, "committer %s %ld +0000\n", author, now);

  // Write commit message (blank line separator + message)
  pos += sprintf(content + pos, "\n%s\n", message);

  // Create commit object header: "commit <size>\0"
  char header[100];
  int header_len = sprintf(header, "commit %d", pos);
  header[header_len++] = '\0';

  // Combine header and commit content
  size_t total_len = header_len + pos;
  char *full_content = malloc(total_len);
  memcpy(full_content, header, header_len);
  memcpy(full_content + header_len, content, pos);

  // Calculate SHA-1 hash of the complete commit object
  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1((unsigned char *)full_content, total_len, hash);

  // Convert to hex
  char *hex_hash = malloc(41);
  for (int i = 0; i < 20; i++) {
    sprintf(hex_hash + (i * 2), "%02x", hash[i]);
  }

  // Store object
  store_object(hex_hash, full_content, total_len);

  // Cleanup
  free(content);
  free(full_content);

  return hex_hash;
}

/**
 * Recursively checkout a tree object to the working directory.
 * Extracts all files and subdirectories from a tree object,
 * creating the directory structure and writing file contents.
 * 
 * @param tree_hash SHA-1 hash of the tree to checkout
 * @param prefix Directory prefix for relative paths
 * @return 0 on success, 1 on error
 */
int checkout_tree(const char *tree_hash, const char *prefix) {
  git_object *tree_obj = read_object(tree_hash);
  if (!tree_obj)
    return 1;

  tree_object *tree = parse_tree_object(tree_obj);
  if (!tree) {
    free_git_object(tree_obj);
    return 1;
  }

  // Process each entry in the tree
  for (size_t i = 0; i < tree->count; i++) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", prefix, tree->entries[i].name);

    if (strcmp(tree->entries[i].mode, "40000") == 0) {
      // Recursively checkout subdirectory
      mkdir(path, 0755);
      checkout_tree(tree->entries[i].hash, path);
    } else {
      // Extract file from blob object
      git_object *blob = read_object(tree->entries[i].hash);
      if (blob) {
        FILE *f = fopen(path, "wb");
        if (f) {
          fwrite(blob->content, 1, blob->size, f);
          fclose(f);
        }
        free_git_object(blob);
      }
    }
  }

  free_tree_object(tree);
  free_git_object(tree_obj);
  return 0;
}

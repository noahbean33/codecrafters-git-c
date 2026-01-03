
#include "git.h"
#include <dirent.h> // For DIR, struct dirent
#include <limits.h> // For PATH_MAX
#include <openssl/sha.h>
#include <time.h>

char *get_object_path(const char *hash) {
  char *path = malloc(strlen(OBJECTS_DIR) + HASH_LENGTH + 2);
  sprintf(path, "%s/%.2s/%s", OBJECTS_DIR, hash, hash + 2);
  return path;
}

git_object *read_object(const char *hash) {
  const char *path = get_object_path(hash);
  if (!path)
    return NULL;

  FILE *file = fopen(path, "rb");
  if (!file)
    return NULL;

  unsigned char *in = malloc(COMPRESSED_CHUNK_SIZE);
  unsigned char *out = malloc(COMPRESSED_CHUNK_SIZE);
  z_stream strm = {0};

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

  do {
    strm.avail_in = fread(in, 1, COMPRESSED_CHUNK_SIZE, file);
    strm.next_in = in;

    do {
      strm.avail_out = COMPRESSED_CHUNK_SIZE;
      strm.next_out = out;

      int ret = inflate(&strm, Z_NO_FLUSH);
      if (ret != Z_OK && ret != Z_STREAM_END) {
        free_git_object(obj);
        free(buffer);
        inflateEnd(&strm);
        fclose(file);
        return NULL;
      }

      size_t have = COMPRESSED_CHUNK_SIZE - strm.avail_out;
      buffer = realloc(buffer, total_size + have);
      memcpy(buffer + total_size, out, have);
      total_size += have;

    } while (strm.avail_out == 0);
  } while (strm.avail_in != 0);

  // Parse header
  char *space = memchr(buffer, ' ', total_size);
  char *null = memchr(space + 1, 0, total_size - (space - (char *)buffer));

  obj->type = strndup((char *)buffer, space - (char *)buffer);
  obj->size = atoi(space + 1);

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

void free_git_object(git_object *obj) {
  if (obj) {
    free(obj->type);
    free(obj->content);
    free(obj);
  }
}

char *sha1_hash(const char *data, size_t len) {
  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1((unsigned char *)data, len, hash);

  char *hex = malloc(41);
  for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
    sprintf(hex + (i * 2), "%02x", hash[i]);
  }
  return hex;
}

int write_compressed_object(const char *path, const char *data, size_t len) {
  FILE *f = fopen(path, "wb");
  if (!f)
    return 1;

  z_stream strm = {0};
  if (deflateInit(&strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
    fclose(f);
    return 1;
  }

  unsigned char out[COMPRESSED_CHUNK_SIZE];
  strm.next_in = (unsigned char *)data;
  strm.avail_in = len;

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

char *create_blob_from_file(const char *filepath) {
  FILE *f = fopen(filepath, "rb");
  if (!f) {
    fprintf(stderr, "Cannot open file\n");
    return NULL;
  }

  // Get file size
  fseek(f, 0, SEEK_END);
  size_t file_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  // Read content
  char *content = malloc(file_size);
  fread(content, 1, file_size, f);
  fclose(f);

  // Create header
  char header[GIT_HEADER_LENGTH];
  int header_len = sprintf(header, "blob %zu", file_size);
  header[header_len++] = '\0';

  // Combine header and content
  size_t total_len = header_len + file_size;
  char *data = malloc(total_len);
  memcpy(data, header, header_len);
  memcpy(data + header_len, content, file_size);

  // Calculate hash
  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1((unsigned char *)data, total_len, hash);

  // Convert to hex
  char *hex = malloc(GIT_HASH_LENGTH + 1);
  for (int i = 0; i < 20; i++) {
    sprintf(hex + (i * 2), "%02x", hash[i]);
  }

  // Store object
  store_object(hex, data, total_len);

  // Cleanup
  free(data);
  free(content);

  return hex;
}

int store_object(const char *hash, const char *data, size_t len) {
  // Create object directory
  char *dir = malloc(strlen(OBJECTS_DIR) + 4);
  sprintf(dir, "%s/%.2s", OBJECTS_DIR, hash);
  mkdir(dir, 0755);
  free(dir);

  // Write compressed object
  char *path = get_object_path(hash);
  int result = write_compressed_object(path, data, len);
  free(path);

  return result;
}

tree_object *parse_tree_object(git_object *obj) {
  if (!obj || strcmp(obj->type, GIT_TREE) != 0) {
    return NULL;
  }

  tree_object *tree = malloc(sizeof(tree_object));
  tree->count = 0;
  tree->entries = NULL;

  const unsigned char *content = (const unsigned char *)obj->content;
  size_t pos = 0;
  size_t max_entries = 16; // Start with space for 16 entries
  tree->entries = malloc(sizeof(tree_entry) * max_entries);

  while (pos < obj->size) {
    // Expand array if needed
    if (tree->count == max_entries) {
      max_entries *= 2;
      tree->entries = realloc(tree->entries, sizeof(tree_entry) * max_entries);
    }

    // Read mode
    char mode[7];
    int mode_pos = 0;
    while (content[pos] != ' ' && mode_pos < 6) {
      mode[mode_pos++] = content[pos++];
    }
    mode[mode_pos] = '\0';
    pos++; // Skip space

    // Read name
    char *name_start = (char *)content + pos;
    while (content[pos] != '\0')
      pos++;
    size_t name_len = ((char *)content + pos) - name_start;

    // Allocate and copy name
    tree->entries[tree->count].name = malloc(name_len + 1);
    memcpy(tree->entries[tree->count].name, name_start, name_len);
    tree->entries[tree->count].name[name_len] = '\0';

    // Copy mode
    strcpy(tree->entries[tree->count].mode, mode);

    // Skip null byte
    pos++;

    // Read SHA-1 hash
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

int should_ignore_path(const char *path) {
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  return strcmp(base, ".git") == 0 || strcmp(base, ".") == 0 ||
         strcmp(base, "..") == 0;
}

char *write_tree_recursive(const char *dir_path) {
  DIR *dir = opendir(dir_path);
  if (!dir)
    return NULL;

  struct dirent *entry;
  size_t entries_capacity = 16;
  size_t entries_count = 0;
  tree_entry *entries = malloc(sizeof(tree_entry) * entries_capacity);

  // First pass: collect and sort entries
  while ((entry = readdir(dir))) {
    char full_path[PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

    if (should_ignore_path(full_path))
      continue;

    if (entries_count == entries_capacity) {
      entries_capacity *= 2;
      entries = realloc(entries, sizeof(tree_entry) * entries_capacity);
    }

    struct stat st;
    if (lstat(full_path, &st) == -1)
      continue;

    entries[entries_count].name = strdup(entry->d_name);

    if (S_ISDIR(st.st_mode)) {
      strcpy(entries[entries_count].mode, "40000");
      entries[entries_count].hash = write_tree_recursive(full_path);
    } else if (S_ISREG(st.st_mode)) {
      strcpy(entries[entries_count].mode, "100644");
      entries[entries_count].hash = create_blob_from_file(full_path);
    } else {
      free(entries[entries_count].name);
      continue;
    }

    entries_count++;
  }
  closedir(dir);

  // Sort entries by name
  for (size_t i = 0; i < entries_count; i++) {
    for (size_t j = i + 1; j < entries_count; j++) {
      if (strcmp(entries[i].name, entries[j].name) > 0) {
        tree_entry temp = entries[i];
        entries[i] = entries[j];
        entries[j] = temp;
      }
    }
  }

  // Calculate total size needed
  size_t total_size = 0;
  for (size_t i = 0; i < entries_count; i++) {
    total_size +=
        strlen(entries[i].mode) + 1 + strlen(entries[i].name) + 1 + 20;
  }

  // Create tree content
  char *content = malloc(total_size);
  size_t pos = 0;

  for (size_t i = 0; i < entries_count; i++) {
    // Write mode and space
    pos += sprintf(content + pos, "%s ", entries[i].mode);

    // Write filename and null byte
    size_t name_len = strlen(entries[i].name);
    memcpy(content + pos, entries[i].name, name_len);
    pos += name_len;
    content[pos++] = '\0';

    // Convert hash from hex to binary and write
    for (int j = 0; j < 40; j += 2) {
      unsigned int byte;
      sscanf(entries[i].hash + j, "%2x", &byte);
      content[pos++] = byte;
    }
  }

  // Create tree object header
  char header[100];
  int header_len = sprintf(header, "tree %zu", total_size);
  header[header_len++] = '\0';

  // Combine header and content
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

int handle_write_tree(void) {
  char *hash = write_tree_recursive(".");
  if (!hash) {
    fprintf(stderr, "Failed to write tree\n");
    return 1;
  }

  printf("%s\n", hash);
  free(hash);
  return 0;
}

char *create_commit_object(const char *tree_sha, const char *parent_sha,
                           const char *message) {
  // Get current timestamp
  time_t now = time(NULL);

  // Format commit content
  char *content = malloc(1024); // Reasonable initial size
  int pos = 0;

  // Write tree
  pos += sprintf(content + pos, "tree %s\n", tree_sha);

  // Write parent if provided
  if (parent_sha) {
    pos += sprintf(content + pos, "parent %s\n", parent_sha);
  }

  // Write author and committer (using hardcoded values)
  const char *author = "CodeCrafter <codecrafter@example.com>";
  pos += sprintf(content + pos, "author %s %ld +0000\n", author, now);
  pos += sprintf(content + pos, "committer %s %ld +0000\n", author, now);

  // Write message
  pos += sprintf(content + pos, "\n%s\n", message);

  // Create header
  char header[100];
  int header_len = sprintf(header, "commit %d", pos);
  header[header_len++] = '\0';

  // Combine header and content
  size_t total_len = header_len + pos;
  char *full_content = malloc(total_len);
  memcpy(full_content, header, header_len);
  memcpy(full_content + header_len, content, pos);

  // Calculate hash
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

int checkout_tree(const char *tree_hash, const char *prefix) {
  git_object *tree_obj = read_object(tree_hash);
  if (!tree_obj)
    return 1;

  tree_object *tree = parse_tree_object(tree_obj);
  if (!tree) {
    free_git_object(tree_obj);
    return 1;
  }

  for (size_t i = 0; i < tree->count; i++) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", prefix, tree->entries[i].name);

    if (strcmp(tree->entries[i].mode, "40000") == 0) {
      // Directory
      mkdir(path, 0755);
      checkout_tree(tree->entries[i].hash, path);
    } else {
      // File
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

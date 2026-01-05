/**
 * clone.c - Git Clone and Pack File Processing
 * 
 * This file implements the Git clone functionality including:
 *   - HTTP communication with remote repositories
 *   - Git smart protocol implementation
 *   - Pack file parsing and object extraction
 *   - Working directory checkout
 * 
 * The Git pack file format is a compressed representation of multiple
 * Git objects, used for efficient network transfer during clone/fetch operations.
 */

#include "git.h"
#include <arpa/inet.h>
#include <curl/curl.h>
#include <openssl/sha.h>
#include <stdint.h>

/**
 * Structure to hold HTTP response data from libcurl.
 */
struct ResponseData {
  char *data;   // Response data buffer
  size_t size;  // Size of response data
};

/**
 * Callback function for libcurl to write received data.
 * Called by libcurl whenever data is received from the server.
 * 
 * @param ptr Pointer to received data
 * @param size Size of each data element
 * @param nmemb Number of data elements
 * @param userdata User-provided pointer (ResponseData struct)
 * @return Number of bytes processed
 */
size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
  size_t realsize = size * nmemb;
  struct ResponseData *resp = (struct ResponseData *)userdata;

  // Expand buffer to accommodate new data
  resp->data = realloc(resp->data, resp->size + realsize);
  if (!resp->data)
    return 0;

  // Append new data to buffer
  memcpy(resp->data + resp->size, ptr, realsize);
  resp->size += realsize;

  printf("Received %zu bytes of data\n", realsize);
  return realsize;
}

/**
 * Read a variable-length size from pack file data.
 * Git uses a variable-length encoding where the MSB indicates
 * if more bytes follow.
 * 
 * @param data Pack file data
 * @param pos Current position (updated by this function)
 * @param size Output size value
 * @return The decoded size
 */
size_t read_size(const char *data, size_t *pos, size_t *size) {
  size_t shift = 0;
  unsigned char byte;
  *size = 0;

  // Read bytes until we find one without the continuation bit (0x80)
  do {
    byte = data[*pos];
    *size |= ((byte & 0x7f) << shift);  // Take lower 7 bits
    shift += 7;
    (*pos)++;
  } while (byte & 0x80);  // Continue if MSB is set

  return *size;
}

/**
 * Process a Git pack file and extract all objects.
 * Pack files contain a header followed by compressed Git objects.
 * Format: "PACK" + version(4) + num_objects(4) + objects...
 * 
 * @param pack_data Pack file data
 * @param pack_size Size of pack file
 * @return 0 on success, 1 on error
 */
int process_pack_file(const char *pack_data, size_t pack_size) {
  printf("Processing pack file of size %zu\n", pack_size);

  // Locate the pack file signature
  const char *pack_start = strstr(pack_data, "PACK");
  if (!pack_start) {
    printf("Invalid pack signature\n");
    return 1;
  }

  // Parse pack header (network byte order)
  uint32_t version = ntohl(*(uint32_t *)(pack_start + 4));
  uint32_t num_objects = ntohl(*(uint32_t *)(pack_start + 8));
  size_t pos = 12;  // Start after header

  // Process each object in the pack file
  for (uint32_t i = 0; i < num_objects; i++) {
    // Read object type and initial size from first byte
    unsigned char type_byte = pack_start[pos];
    unsigned char type = (type_byte >> 4) & 7;  // Bits 4-6: object type
    size_t obj_size = type_byte & 0x0f;         // Bits 0-3: initial size
    pos++;

    // Read remaining size bytes (variable-length encoding)
    int shift = 4;
    while (type_byte & 0x80) {  // MSB indicates more bytes
      type_byte = pack_start[pos++];
      obj_size |= ((type_byte & 0x7f) << shift);
      shift += 7;
    }

    // Initialize zlib for decompression
    z_stream strm = {0};
    if (inflateInit(&strm) != Z_OK) {
      continue;
    }

    // Handle different Git object types
    switch (type) {
    case OBJ_COMMIT:
    case OBJ_TREE:
    case OBJ_BLOB:
    case OBJ_TAG: {
      // Decompress the object data using zlib
      unsigned char *obj_data = malloc(obj_size);
      strm.next_in = (unsigned char *)pack_start + pos;
      strm.avail_in = pack_size - pos;
      strm.next_out = obj_data;
      strm.avail_out = obj_size;

      // Decompress the object
      if (inflate(&strm, Z_FINISH) != Z_STREAM_END) {
        free(obj_data);
        inflateEnd(&strm);
        continue;
      }

      // Map pack object type to Git object type string
      const char *type_str = type == OBJ_COMMIT ? "commit"
                             : type == OBJ_TREE ? "tree"
                             : type == OBJ_BLOB ? "blob"
                                                : "tag";

      char header[100];
      int header_len = sprintf(header, "%s %zu", type_str, obj_size);
      header[header_len++] = '\0';

      // Combine header and content (standard Git object format)
      size_t total_len = header_len + obj_size;
      char *full_data = malloc(total_len);
      memcpy(full_data, header, header_len);
      memcpy(full_data + header_len, obj_data, obj_size);

      // Calculate SHA-1 hash for object identification
      unsigned char hash[SHA_DIGEST_LENGTH];
      SHA1((unsigned char *)full_data, total_len, hash);
      char hex_hash[41];
      for (int j = 0; j < 20; j++) {
        sprintf(hex_hash + (j * 2), "%02x", hash[j]);
      }
      hex_hash[40] = '\0';

      // Store the object in .git/objects
      store_object(hex_hash, full_data, total_len);

      free(full_data);
      free(obj_data);
      break;
    }

    case OBJ_REF_DELTA:
      // Delta objects reference another object for compression
      pos += 20; // Skip 20-byte base object SHA-1
      break;
    }

    // Clean up zlib stream
    inflateEnd(&strm);
    pos += strm.total_in;  // Advance position by compressed size
  }

  return 0;
}

/**
 * Fetch remote repository references (branches, tags) via HTTP.
 * Uses the Git smart protocol to query available refs.
 * 
 * @param url Repository URL
 * @return Response data containing refs, or NULL on error (caller must free)
 */
char *get_remote_refs(const char *url) {
  printf("Getting refs from: %s\n", url);

  // Initialize libcurl for HTTP request
  CURL *curl = curl_easy_init();
  if (!curl) {
    printf("Failed to initialize curl\n");
    return NULL;
  }

  // Build the info/refs endpoint URL (Git smart protocol)
  char full_url[URL_BUFFER_SIZE];
  snprintf(full_url, sizeof(full_url), "%s/info/refs?service=git-upload-pack",
           url);
  printf("Full URL: %s\n", full_url);

  // Set up response buffer and curl options
  struct ResponseData response = {NULL, 0};
  curl_easy_setopt(curl, CURLOPT_URL, full_url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

  printf("Sending HTTP request...\n");
  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    printf("Curl request failed: %s\n", curl_easy_strerror(res));
    free(response.data);
    return NULL;
  }

  printf("Successfully received refs\n");
  return response.data;
}

/**
 * Extract the commit hash from the refs response.
 * Parses the Git protocol response to find the master/HEAD commit.
 * 
 * @param refs Refs response data
 * @return 40-character commit hash, or NULL if not found (caller must free)
 */
char *get_commit_hash_from_refs(const char *refs) {
  // Search for master branch or HEAD reference
  const char *line = strstr(refs, "refs/heads/master");
  if (!line) {
    // Fallback to HEAD if master not found
    line = strstr(refs, "HEAD");
    if (!line)
      return NULL;
    line -= 41; // Hash is 40 chars + space before ref name
  } else {
    line -= 41; // Hash is 40 chars + space before ref name
  }

  // Extract the 40-character hash
  char *hash = malloc(41);
  strncpy(hash, line, 40);
  hash[40] = '\0';

  return hash;
}

/**
 * Fetch a pack file from a remote repository.
 * Performs a two-step process:
 *   1. Get remote refs to find the commit hash
 *   2. Request pack file for that commit
 * 
 * @param url Repository URL
 * @return PackFile structure with data and commit hash, or NULL on error
 */
struct PackFile *fetch_pack(const char *url) {
  printf("Starting fetch from %s\n", url);

  // Step 1: Get remote references to find the commit to clone
  char *refs = get_remote_refs(url);
  if (!refs)
    return NULL;

  // Extract the commit hash from refs response
  char *commit_hash = get_commit_hash_from_refs(refs);
  free(refs);

  if (!commit_hash)
    return NULL;

  // Initialize curl for pack file request
  CURL *curl = curl_easy_init();
  if (!curl) {
    free(commit_hash);
    return NULL;
  }

  // Build upload-pack endpoint URL
  struct ResponseData resp = {NULL, 0};
  char upload_pack_url[URL_BUFFER_SIZE];
  snprintf(upload_pack_url, sizeof(upload_pack_url), "%s/git-upload-pack", url);

  // Set HTTP headers for Git protocol
  struct curl_slist *headers = curl_slist_append(
      NULL, "Content-Type: application/x-git-upload-pack-request");

  // Build Git protocol request: "want <hash>" + "done"
  char post_data[100];
  snprintf(post_data, sizeof(post_data), "0032want %s\n00000009done\n",
           commit_hash);

  // Configure curl for POST request with pack file request
  curl_easy_setopt(curl, CURLOPT_URL, upload_pack_url);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);

  CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    free(resp.data);
    free(commit_hash);
    return NULL;
  }

  // Package the response into a PackFile structure
  struct PackFile *pack = malloc(sizeof(struct PackFile));
  pack->data = resp.data;
  pack->size = resp.size;
  pack->commit_hash = commit_hash;

  return pack;
}

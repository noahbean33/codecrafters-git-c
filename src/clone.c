#include "git.h"
#include <arpa/inet.h>
#include <curl/curl.h>
#include <openssl/sha.h>
#include <stdint.h>

struct ResponseData {
  char *data;
  size_t size;
};

size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
  size_t realsize = size * nmemb;
  struct ResponseData *resp = (struct ResponseData *)userdata;

  resp->data = realloc(resp->data, resp->size + realsize);
  if (!resp->data)
    return 0;

  memcpy(resp->data + resp->size, ptr, realsize);
  resp->size += realsize;

  printf("Received %zu bytes of data\n", realsize);
  return realsize;
}

size_t read_size(const char *data, size_t *pos, size_t *size) {
  size_t shift = 0;
  unsigned char byte;
  *size = 0;

  do {
    byte = data[*pos];
    *size |= ((byte & 0x7f) << shift);
    shift += 7;
    (*pos)++;
  } while (byte & 0x80);

  return *size;
}

int process_pack_file(const char *pack_data, size_t pack_size) {
  printf("Processing pack file of size %zu\n", pack_size);

  const char *pack_start = strstr(pack_data, "PACK");
  if (!pack_start) {
    printf("Invalid pack signature\n");
    return 1;
  }

  uint32_t version = ntohl(*(uint32_t *)(pack_start + 4));
  uint32_t num_objects = ntohl(*(uint32_t *)(pack_start + 8));
  size_t pos = 12;

  for (uint32_t i = 0; i < num_objects; i++) {
    unsigned char type_byte = pack_start[pos];
    unsigned char type = (type_byte >> 4) & 7;
    size_t obj_size = type_byte & 0x0f;
    pos++;

    // Read variable length size
    int shift = 4;
    while (type_byte & 0x80) {
      type_byte = pack_start[pos++];
      obj_size |= ((type_byte & 0x7f) << shift);
      shift += 7;
    }

    z_stream strm = {0};
    if (inflateInit(&strm) != Z_OK) {
      continue;
    }

    // Handle different object types
    switch (type) {
    case OBJ_COMMIT:
    case OBJ_TREE:
    case OBJ_BLOB:
    case OBJ_TAG: {
      // Decompress object data
      unsigned char *obj_data = malloc(obj_size);
      strm.next_in = (unsigned char *)pack_start + pos;
      strm.avail_in = pack_size - pos;
      strm.next_out = obj_data;
      strm.avail_out = obj_size;

      if (inflate(&strm, Z_FINISH) != Z_STREAM_END) {
        free(obj_data);
        inflateEnd(&strm);
        continue;
      }

      // Create header
      const char *type_str = type == OBJ_COMMIT ? "commit"
                             : type == OBJ_TREE ? "tree"
                             : type == OBJ_BLOB ? "blob"
                                                : "tag";

      char header[100];
      int header_len = sprintf(header, "%s %zu", type_str, obj_size);
      header[header_len++] = '\0';

      // Combine header and content
      size_t total_len = header_len + obj_size;
      char *full_data = malloc(total_len);
      memcpy(full_data, header, header_len);
      memcpy(full_data + header_len, obj_data, obj_size);

      // Calculate hash
      unsigned char hash[SHA_DIGEST_LENGTH];
      SHA1((unsigned char *)full_data, total_len, hash);
      char hex_hash[41];
      for (int j = 0; j < 20; j++) {
        sprintf(hex_hash + (j * 2), "%02x", hash[j]);
      }
      hex_hash[40] = '\0';

      // Store object
      store_object(hex_hash, full_data, total_len);

      free(full_data);
      free(obj_data);
      break;
    }

    case OBJ_REF_DELTA:
      pos += 20; // Skip base object reference
      break;
    }

    inflateEnd(&strm);
    pos += strm.total_in;
  }

  return 0;
}

char *get_remote_refs(const char *url) {
  printf("Getting refs from: %s\n", url);

  CURL *curl = curl_easy_init();
  if (!curl) {
    printf("Failed to initialize curl\n");
    return NULL;
  }

  char full_url[URL_BUFFER_SIZE];
  snprintf(full_url, sizeof(full_url), "%s/info/refs?service=git-upload-pack",
           url);
  printf("Full URL: %s\n", full_url);

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

char *get_commit_hash_from_refs(const char *refs) {
  // Find first line after service info
  const char *line = strstr(refs, "refs/heads/master");
  if (!line) {
    // Try finding HEAD reference
    line = strstr(refs, "HEAD");
    if (!line)
      return NULL;
    line -= 41; // Move back to start of hash
  } else {
    line -= 41; // Move back to start of hash
  }

  // Extract the 40-character hash
  char *hash = malloc(41);
  strncpy(hash, line, 40);
  hash[40] = '\0';

  return hash;
}

struct PackFile *fetch_pack(const char *url) {
  printf("Starting fetch from %s\n", url);

  char *refs = get_remote_refs(url);
  if (!refs)
    return NULL;

  char *commit_hash = get_commit_hash_from_refs(refs);
  free(refs);

  if (!commit_hash)
    return NULL;

  CURL *curl = curl_easy_init();
  if (!curl) {
    free(commit_hash);
    return NULL;
  }

  struct ResponseData resp = {NULL, 0};
  char upload_pack_url[URL_BUFFER_SIZE];
  snprintf(upload_pack_url, sizeof(upload_pack_url), "%s/git-upload-pack", url);

  struct curl_slist *headers = curl_slist_append(
      NULL, "Content-Type: application/x-git-upload-pack-request");

  char post_data[100];
  snprintf(post_data, sizeof(post_data), "0032want %s\n00000009done\n",
           commit_hash);

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

  struct PackFile *pack = malloc(sizeof(struct PackFile));
  pack->data = resp.data;
  pack->size = resp.size;
  pack->commit_hash = commit_hash;

  return pack;
}

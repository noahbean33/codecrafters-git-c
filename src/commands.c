#include "git.h"
#include <dirent.h> // For DIR, struct dirent
#include <limits.h> // For PATH_MAX

int handle_init(void) {
  if (mkdir(GIT_DIR, 0755) == -1 || mkdir(OBJECTS_DIR, 0755) == -1 ||
      mkdir(REFS_DIR, 0755) == -1) {
    fprintf(stderr, "Failed to create git directories: %s\n", strerror(errno));
    return 1;
  }

  FILE *head = fopen(".git/HEAD", "w");
  if (!head) {
    fprintf(stderr, "Failed to create HEAD file: %s\n", strerror(errno));
    return 1;
  }
  fprintf(head, "ref: refs/heads/main\n");
  fclose(head);

  printf("Initialized git directory\n");
  return 0;
}

int handle_cat_file(int argc, char *argv[]) {
  if (argc < 3 || strcmp(argv[1], "-p") != 0) {
    fprintf(stderr, "Usage: cat-file -p <object>\n");
    return 1;
  }

  git_object *obj = read_object(argv[2]);
  if (!obj) {
    fprintf(stderr, "Object not found\n");
    return 1;
  }

  // Print content without newline
  printf("%s", obj->content);

  free_git_object(obj);
  return 0;
}

int handle_hash_object(int argc, char *argv[]) {
  if (argc < 3 || strcmp(argv[1], "-w") != 0) {
    fprintf(stderr, "Usage: hash-object -w <file>\n");
    return 1;
  }

  char *hash = create_blob_from_file(argv[2]);
  if (!hash) {
    return 1;
  }

  printf("%s\n", hash);
  free(hash);
  return 0;
}

int handle_ls_tree(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: ls-tree [--name-only] <tree-ish>\n");
    return 1;
  }

  const char *hash;
  int name_only = 0;

  if (strcmp(argv[1], "--name-only") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Tree hash required\n");
      return 1;
    }
    name_only = 1;
    hash = argv[2];
  } else {
    hash = argv[1];
  }

  git_object *obj = read_object(hash);
  if (!obj) {
    fprintf(stderr, "Failed to read tree object\n");
    return 1;
  }

  tree_object *tree = parse_tree_object(obj);
  if (!tree) {
    fprintf(stderr, "Failed to parse tree object\n");
    free_git_object(obj);
    return 1;
  }

  for (size_t i = 0; i < tree->count; i++) {
    if (name_only) {
      printf("%s\n", tree->entries[i].name);
    } else {
      printf("%s %s %s\t%s\n", tree->entries[i].mode,
             strcmp(tree->entries[i].mode, GIT_MODE_TREE) == 0 ? "tree"
                                                               : "blob",
             tree->entries[i].hash, tree->entries[i].name);
    }
  }

  free_tree_object(tree);
  free_git_object(obj);
  return 0;
}

int handle_commit_tree(int argc, char *argv[]) {
  if (argc < 4) {
    fprintf(stderr, "Usage: commit-tree <tree> -p <parent> -m <message>\n");
    return 1;
  }

  const char *tree_sha = argv[1];
  const char *parent_sha = NULL;
  const char *message = NULL;

  // Parse arguments
  for (int i = 2; i < argc; i += 2) {
    if (i + 1 >= argc) {
      fprintf(stderr, "Missing value for %s\n", argv[i]);
      return 1;
    }

    if (strcmp(argv[i], "-p") == 0) {
      parent_sha = argv[i + 1];
    } else if (strcmp(argv[i], "-m") == 0) {
      message = argv[i + 1];
    }
  }

  if (!message) {
    fprintf(stderr, "Message is required\n");
    return 1;
  }

  char *commit_sha = create_commit_object(tree_sha, parent_sha, message);
  if (!commit_sha) {
    fprintf(stderr, "Failed to create commit object\n");
    return 1;
  }

  printf("%s\n", commit_sha);
  free(commit_sha);
  return 0;
}

void print_directory_tree(const char *basePath, int level) {
  DIR *dir;
  struct dirent *entry;

  dir = opendir(basePath);
  if (!dir)
    return;

  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.')
      continue;

    for (int i = 0; i < level; i++)
      printf("  ");

    printf("├── %s\n", entry->d_name);

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", basePath, entry->d_name);

    if (entry->d_type == DT_DIR) {
      print_directory_tree(path, level + 1);
    }
  }

  closedir(dir);
}

int handle_clone(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Usage: clone <url> <directory>\n");
    return 1;
  }

  const char *url = argv[1];
  const char *dir = argv[2];

  if (mkdir(dir, 0755) == -1 || chdir(dir) == -1) {
    fprintf(stderr, "Directory setup failed\n");
    return 1;
  }

  if (handle_init() != 0) {
    return 1;
  }

  struct PackFile *pack = fetch_pack(url);
  if (!pack) {
    fprintf(stderr, "Failed to fetch pack\n");
    return 1;
  }

  if (process_pack_file(pack->data, pack->size) != 0) {
    fprintf(stderr, "Failed to write pack file\n");
    free(pack->data);
    free(pack->commit_hash);
    free(pack);
    return 1;
  }

  git_object *commit_obj = read_object(pack->commit_hash);
  if (!commit_obj) {
    fprintf(stderr, "Failed to read commit object\n");
    return 1;
  }

  char tree_hash[41];
  sscanf(commit_obj->content, "tree %40s", tree_hash);

  checkout_tree(tree_hash, ".");

  free(pack->data);
  free(pack->commit_hash);
  free(pack);

  return 0;
}

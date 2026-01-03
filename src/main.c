

#include "git.h"

int main(int argc, char *argv[]) {
  setbuf(stdout, NULL);

  if (argc < 2) {
    fprintf(stderr, "Usage: %s <command> [<args>]\n", argv[0]);
    return 1;
  }

  const char *command = argv[1];

  if (strcmp(command, "init") == 0) {
    return handle_init();
  } else if (strcmp(command, "cat-file") == 0) {
    return handle_cat_file(argc - 1, argv + 1);
  } else if (strcmp(command, "hash-object") == 0) {
    return handle_hash_object(argc - 1, argv + 1);
  } else if (strcmp(command, "ls-tree") == 0) {
    return handle_ls_tree(argc - 1, argv + 1);
  } else if (strcmp(command, "write-tree") == 0) {
    return handle_write_tree();
  } else if (strcmp(command, "commit-tree") == 0) {
    return handle_commit_tree(argc - 1, argv + 1);
  } else if (strcmp(command, "clone") == 0) {
    return handle_clone(argc - 1, argv + 1);
  }

  fprintf(stderr, "Unknown command: %s\n", command);
  return 1;
}

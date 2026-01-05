/**
 * main.c - Git Implementation Entry Point
 * 
 * This file serves as the command-line interface entry point for the Git implementation.
 * It routes user commands to their respective handler functions.
 * 
 * Supported commands:
 *   - init: Initialize a new Git repository
 *   - cat-file: Display contents of a Git object
 *   - hash-object: Compute and store object hash
 *   - ls-tree: List contents of a tree object
 *   - write-tree: Create a tree object from the working directory
 *   - commit-tree: Create a new commit object
 *   - clone: Clone a remote repository
 */

#include "git.h"

/**
 * Main entry point for the Git implementation.
 * 
 * @param argc Argument count
 * @param argv Argument vector containing command and parameters
 * @return 0 on success, 1 on error
 */
int main(int argc, char *argv[]) {
  // Disable output buffering for immediate console feedback
  setbuf(stdout, NULL);

  // Validate command-line arguments
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <command> [<args>]\n", argv[0]);
    return 1;
  }

  // Extract the command from arguments
  const char *command = argv[1];

  // Route commands to their respective handler functions
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

  // Handle unknown commands
  fprintf(stderr, "Unknown command: %s\n", command);
  return 1;
}

# Git Implementation in C

A functional implementation of core Git version control system commands, built from scratch in C. This project demonstrates proficiency in systems programming, file I/O, data compression, cryptographic hashing, and network protocols.

## Project Overview

This project is a custom implementation of Git that replicates the fundamental functionality of the industry-standard version control system. It handles object storage, compression, SHA-1 hashing, tree structures, and remote repository operations—all written in C without relying on the official Git libraries.

## Features Implemented

### Core Git Operations
- **`init`** - Initialize a new Git repository with proper directory structure (.git/objects, .git/refs, HEAD)
- **`hash-object`** - Compute SHA-1 hash of files and store them as blob objects
- **`cat-file`** - Read and display blob object contents from the object database
- **`write-tree`** - Recursively create tree objects representing directory structures
- **`ls-tree`** - List contents of tree objects with metadata
- **`commit-tree`** - Create commit objects with tree references, parent commits, and messages
- **`clone`** - Clone remote repositories using the Git protocol (HTTP)

### Technical Highlights

**Compression & Encoding:**
- Implements zlib compression/decompression for Git object storage
- Handles variable-length encoding in Git pack files
- Binary data manipulation for tree object parsing

**Cryptography:**
- SHA-1 hashing using OpenSSL for object identification
- 40-character hex digest generation for object addressing

**Network Programming:**
- HTTP client implementation using libcurl
- Git smart protocol communication
- Pack file fetching and processing

**Data Structures:**
- Custom object parsing (blobs, trees, commits)
- Recursive tree traversal algorithms
- Dynamic memory management with proper cleanup

## Technical Stack

- **Language:** C (C99 standard)
- **Build System:** CMake
- **Dependencies:**
  - zlib - Compression library
  - OpenSSL - SHA-1 cryptographic hashing
  - libcurl - HTTP client for network operations
- **Package Manager:** vcpkg

## Project Structure

```
src/
├── main.c       - Entry point and command routing
├── git.h        - Header file with function declarations and constants
├── commands.c   - Implementation of Git command handlers
├── objects.c    - Git object manipulation (read, write, hash, compress)
└── clone.c      - Remote repository cloning and pack file processing
```

## Building & Running

```bash
# Build the project
cmake -B build -S .
cmake --build build

# Run commands
./your_program.sh init
./your_program.sh hash-object -w <file>
./your_program.sh cat-file -p <hash>
./your_program.sh write-tree
./your_program.sh ls-tree --name-only <tree-hash>
./your_program.sh commit-tree <tree> -m "message"
./your_program.sh clone <url> <directory>
```

**Built as part of the CodeCrafters "Build Your Own Git" challenge.**
# FileSystem-API

Author: Hrishikesha Kyathsandra: rishihk@iastate.edu

## Overview

This document outlines the modifications made to the RSFS (Ridiculously Simple File System) as part of the COM-S-352 Project 2 for Spring 2024. The primary changes were implemented in the `api.c` file to implement the file system's functionality and API methods. `application.c` was modified to include more test cases and debug statements, but has been brought back to its original state for sample output comparision.

## Modified Files

### `api.c`

#### **RSFS_open(char file_name, int access_flag)**
- **Purpose**: Opens a file with specified access flags, handling concurrent access appropriately.
- **Implementation**:
  - Checks the validity of the access flag and searches for the file's directory entry.
  - If found, it locks the corresponding inode based on the access flag to ensure mutual exclusivity or shared access depending on the read or write mode.
  - Allocates an open file entry and returns a descriptor for the opened file. If the allocation fails, it unlocks the inode and returns an error.

#### **RSFS_read(int fd, void buf, int size)**
- **Purpose**: Reads data from a file starting from the current position up to the specified size.
- **Implementation**:
  - Validates the file descriptor and checks if the size is appropriate.
  - Calculates the block index and offset based on the current file position.
  - Reads data from the appropriate blocks into the buffer until the requested size is met or the end of the file is reached, adjusting the file position accordingly.

#### **RSFS_write(int fd, void buf, int size)**
- **Purpose**: Writes data to a file at the current position, potentially expanding the file size.
- **Implementation**:
  - Validates the file descriptor and ensures the file is open with write access.
  - Determines the necessary data blocks to write to and allocates new blocks if necessary.
  - Writes data block-by-block from the buffer into the file, updating the file's length and position.

#### **RSFS_append(int fd, void buf, int size)**
- **Purpose**: Appends data to the end of a file.
- **Implementation**:
  - Checks for valid file descriptor and write access.
  - Starts writing at the current end of the file, allocating new blocks if the existing blocks are insufficient.
  - Updates the file's length and the open file entry's position after appending.

#### **RSFS_fseek(int fd, int offset)**
- **Purpose**: Changes the current file position to the specified offset, if within file bounds.
- **Implementation**:
  - Validates the file descriptor and ensures the offset is within the file's current length.
  - Updates the position in the open file entry to reflect the new offset.

#### **RSFS_close(int fd)**
- **Purpose**: Closes an open file and frees associated resources.
- **Implementation**:
  - Validates the file descriptor and checks that the file is currently in use.
  - Depending on the access flag, releases read or write locks on the inode and decrements reader counts as necessary.
  - Frees the open file entry, marking it as unused.

#### **RSFS_delete(char file_name)**
- **Purpose**: Deletes a file, freeing all associated data blocks and inodes.
- **Implementation**:
  - Searches for the file's directory entry. If not found, returns an error.
  - Frees all data blocks associated with the file's inode, resets the inode entries, and updates the inode bitmap.
  - Deletes the directory entry, updating the root directory structure.

#### **RSFS_cut(int fd, int size)**
- **Purpose**: Removes up to a specified size of data from the current position in the file, compacting the file's content.
- **Implementation**:
  - Validates the file descriptor and checks if the operation is permissible (write access and valid size).
  - Calculates the necessary adjustments to the file's data blocks, moving subsequent data forward to fill the gap created by the cut.
  - Adjusts the file's length and updates inode and data block information as needed.

## Compilation and Execution

Compile the system with the following commands:

```bash
make clean
make
```

Run tests using:

```bash
./app
```

Alternatively, you can just do run the following command in the root directory:

```bash
./run.sh
```


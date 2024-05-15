#include <fuse.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h> 
#include <time.h>
#include <utime.h>

int dm510fs_getattr( const char *, struct stat * );
int dm510fs_readdir( const char *, void *, fuse_fill_dir_t, off_t, struct fuse_file_info * );
int dm510fs_open( const char *, struct fuse_file_info * );
int dm510fs_read( const char *, char *, size_t, off_t, struct fuse_file_info * );
int dm510fs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int dm510fs_release(const char *path, struct fuse_file_info *fi);
int dm510fs_mkdir(const char *path, mode_t mode);
int dm510fs_mknod(const char *path, mode_t  mode, dev_t rdev);
int dm510fs_unlink(const char *path);
int dm510fs_rmdir(const char *path);
int dm510fs_utime(const char *path, struct utimbuf *ubuf);
int dm510fs_rename(const char *oldpath, const char *newpath);
int dm510fs_truncate(const char *path, off_t size);
void* dm510fs_init();
void dm510fs_destroy(void *private_data);

/*
 * See descriptions in fuse source code usually located in /usr/include/fuse/fuse.h
 * Notice: The version on Github is a newer version than installed at IMADA
 */
static struct fuse_operations dm510fs_oper = {
	.getattr	= dm510fs_getattr,
	.readdir	= dm510fs_readdir,
	.mknod = dm510fs_mknod,
	.mkdir = dm510fs_mkdir,
	.unlink = dm510fs_unlink,
	.rmdir = dm510fs_rmdir,
	.truncate = dm510fs_truncate,
	.open	= dm510fs_open,
	.read	= dm510fs_read,
	.release = dm510fs_release,
	.write = dm510fs_write,
	.rename = dm510fs_rename,
	.utime = dm510fs_utime,
	.init = dm510fs_init,
	.destroy = dm510fs_destroy
};

// Define constrants for limts within filesystem 
#define MAX_POINTERS 12 
#define MAX_PATH_LENGTH  256
#define MAX_NAME_LENGTH  256
#define MAX_INODES  4
#define BLOCK_SIZE 4096     // 4KB per block 
#define TOTAL_BLOCKS 1024    // 1024 blocks of 4KB each 
// Path to save filesystem state
#define FS_STATE_FILE "/home/dm510/dm510/myfilesystem/fs_state.dat"

/* The Inode for the filesystem*/
typedef struct Inode {
    bool is_active;                 // Indicates whether the inode is in use
    bool is_dir;                    // Indicates whether the inode is a directory
    int data_blocks[MAX_POINTERS];  // Array of data block indices
    char path[MAX_PATH_LENGTH];     // Full path of the file/directory
    char name[MAX_NAME_LENGTH];     // Name of the file/directory
    mode_t mode;                    // File type and permissions
    nlink_t nlink;                  // Link count
    off_t size;                     // Total size of the file in bytes
    time_t atime;                   // Last access time
    time_t mtime;                   // Last modification time
    time_t ctime;                   // Last status change time
} Inode;

char blocks[TOTAL_BLOCKS][BLOCK_SIZE];  //Simulating block storage 

//Function to get a pointer to the block data
char* get_block_data(int block_number) {
    if (block_number >= 0 && block_number < TOTAL_BLOCKS) {
        return blocks[block_number];
    }
    return NULL;
}

// Array that represent the entire filesystem 
Inode filesystem[MAX_INODES];


// Prints the path of the inode
void debug_inode(int i) {
	Inode inode = filesystem[i];

	printf("=============================================\n");
	printf("      Path: %s\n", inode.path);
	printf("=============================================\n");
}

// Our block bitmap is an array where each bit represnts the status (used or free) of a block in the filesystem 
uint8_t block_bitmap[TOTAL_BLOCKS / 8]; //Each bit represents a block

void init_block_bitmap() {
    memset(block_bitmap, 0, sizeof(block_bitmap));
}

/*
Functions to allocate and free blocks using the bitmap, 
the functions search for a free bit in the bitmap, 
set it when a block is allocated, and clear it when a block is freed 
*/
int allocate_block() {
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        int byteIndex = i / 8;
        int bitIndex = i % 8;

        if (!(block_bitmap[byteIndex] & (1 << bitIndex))) {  // Check if the bit is 0
            block_bitmap[byteIndex] |= (1 << bitIndex);  // Set the bit to 1
            return i;  // Return the block index
        }
    }
    return -1;  // No free blocks available
}

void free_block(int block_index) {
    int byteIndex = block_index / 8;
    int bitIndex = block_index % 8;
    block_bitmap[byteIndex] &= ~(1 << bitIndex);  // Set the bit to 0
}


void save_fs_state() {
    FILE *fp = fopen(FS_STATE_FILE, "wb");
    if (fp == NULL) {
        perror("Failed to open file for writing filesystem state");
        return;
    }
    fwrite(block_bitmap, sizeof(block_bitmap), 1, fp);
    fwrite(filesystem, sizeof(Inode), MAX_INODES, fp);
    fclose(fp);
}

void load_fs_state() {
    FILE *fp = fopen(FS_STATE_FILE, "rb");
    if (fp == NULL) {
        perror("Failed to open file for reading filesystem state");
        return;
    }
    fread(block_bitmap, sizeof(block_bitmap), 1, fp);
    fread(filesystem, sizeof(Inode), MAX_INODES, fp);
    fclose(fp);
}



/*
 * Return file attributes.
 * The "stat" structure is described in detail in the stat(2) manual page.
 * For the given pathname, this should fill in the elements of the "stat" structure.
 * If a field is meaningless or semi-meaningless (e.g., st_ino) then it should be set to 0 or given a "reasonable" value.
 * This call is pretty much required for a usable filesystem.
*/
int dm510fs_getattr( const char *path, struct stat *stbuf ) {
	printf("getattr: (path=%s)\n", path);

	memset(stbuf, 0, sizeof(struct stat));
	for( int i = 0; i < MAX_INODES; i++) {
		printf("===> %s  %s \n", path, filesystem[i].path);
		if( filesystem[i].is_active && strcmp(filesystem[i].path, path) == 0 ) {
			printf("Found inode for path %s at location %i\n", path, i);
			stbuf->st_mode = filesystem[i].mode;
			stbuf->st_nlink = filesystem[i].nlink;
			stbuf->st_size = filesystem[i].size;
            stbuf->st_atime = filesystem[i].atime; // Access time
            stbuf->st_mtime = filesystem[i].mtime; // Modification time
			stbuf->st_ctime = filesystem[i].ctime; // Set the change time
			return 0;
		}
	}

	return -ENOENT;
}

/*
 * Return one or more directory entries (struct dirent) to the caller. This is one of the most complex FUSE functions.
 * Required for essentially any filesystem, since it's what makes ls and a whole bunch of other things work.
 * The readdir function is somewhat like read, in that it starts at a given offset and returns results in a caller-supplied buffer.
 * However, the offset not a byte offset, and the results are a series of struct dirents rather than being uninterpreted bytes.
 * To make life easier, FUSE provides a "filler" function that will help you put things into the buffer.
 *
 * The general plan for a complete and correct readdir is:
 *
 * 1. Find the first directory entry following the given offset (see below).
 * 2. Optionally, create a struct stat that describes the file as for getattr (but FUSE only looks at st_ino and the file-type bits of st_mode).
 * 3. Call the filler function with arguments of buf, the null-terminated filename, the address of your struct stat
 *    (or NULL if you have none), and the offset of the next directory entry.
 * 4. If filler returns nonzero, or if there are no more files, return 0.
 * 5. Find the next file in the directory.
 * 6. Go back to step 2.
 * From FUSE's point of view, the offset is an uninterpreted off_t (i.e., an unsigned integer).
 * You provide an offset when you call filler, and it's possible that such an offset might come back to you as an argument later.
 * Typically, it's simply the byte offset (within your directory layout) of the directory entry, but it's really up to you.
 *
 * It's also important to note that readdir can return errors in a number of instances;
 * in particular it can return -EBADF if the file handle is invalid, or -ENOENT if you use the path argument and the path doesn't exist.
*/
int dm510fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    (void) offset; // Offset is handled by FUSE itself
    (void) fi;     // Not used

    printf("readdir: (path=%s)\n", path);

    int path_len = strlen(path);

    // Fill the current directory and the parent directory entries
    filler(buf, ".", NULL, 0);  // Current directory
    filler(buf, "..", NULL, 0); // Parent directory

    // Loop through all inodes to find directories and files under the specified 'path'
    for (int i = 0; i < MAX_INODES; i++) {
        if (filesystem[i].is_active) {
            if (strncmp(filesystem[i].path, path, path_len) == 0) {
                if (filesystem[i].path[path_len] == '/' || path_len == 1) { // Ensure it's a direct child
                    // Skip any additional '/' that might not be part of the immediate directory name
                    const char *sub_path = filesystem[i].path + path_len;
                    if (*sub_path == '/') sub_path++;

                    // Ensure we're only listing direct children (no '/' in the rest of the string)
                    const char *slash_pos = strchr(sub_path, '/');
                    if (slash_pos == NULL || slash_pos == sub_path) {
                        filler(buf, sub_path, NULL, 0);
                    }
                }
            }
        }
    }

    return 0;
}


/*
 * Open a file.
 * If you aren't using file handles, this function should just check for existence and permissions and return either success or an error code.
 * If you use file handles, you should also allocate any necessary structures and set fi->fh.
 * In addition, fi has some other fields that an advanced filesystem might find useful; see the structure definition in fuse_common.h for very brief commentary.
 * Link: https://github.com/libfuse/libfuse/blob/0c12204145d43ad4683136379a130385ef16d166/include/fuse_common.h#L50
*/
int dm510fs_open( const char *path, struct fuse_file_info *fi ) {
    printf("open: (path=%s)\n", path);
	return 0;
}

/*
 * Read size bytes from the given file into the buffer buf, beginning offset bytes into the file. See read(2) for full details.
 * Returns the number of bytes transferred, or 0 if offset was at or beyond the end of the file. Required for any sensible filesystem.
*/
int dm510fs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    printf("read: (path=%s)\n", path);

    for (int i = 0; i < MAX_INODES; i++) {
        if (strcmp(filesystem[i].path, path) == 0) {
            printf("Read: Found inode for path %s at location %i\n", path, i);
            off_t end = offset + size > filesystem[i].size ? filesystem[i].size : offset + size;
            off_t current_offset = offset;
            size_t bytes_read = 0;

            while (current_offset < end) {
                int block_index = current_offset / BLOCK_SIZE;
                int block_offset = current_offset % BLOCK_SIZE;
                size_t to_read = end - current_offset;
                if (to_read > (BLOCK_SIZE - block_offset)) {
                    to_read = BLOCK_SIZE - block_offset;
                }
                char *block_data = get_block_data(filesystem[i].data_blocks[block_index]);
                if (block_data == NULL) {
                    break;
                }
                memcpy(buf + bytes_read, block_data + block_offset, to_read);
                bytes_read += to_read;
                current_offset += to_read;
            }
            return bytes_read;
        }
    }
    return 0;
}


int dm510fs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) fi;  // Not using file handles in this example.

    // Find the inode corresponding to the path.
    for (int i = 0; i < MAX_INODES; i++) {
        if (filesystem[i].is_active && strcmp(filesystem[i].path, path) == 0) {
            // Calculate start block and end block within the inode's data_blocks array
            int start_block = offset / BLOCK_SIZE;
            int end_block = (offset + size - 1) / BLOCK_SIZE;

            // Check if we need to allocate new blocks
            for (int b = start_block; b <= end_block; b++) {
                if (filesystem[i].data_blocks[b] == 0) {  // Assuming 0 means unallocated
                    int new_block = allocate_block();
                    if (new_block == -1) return -ENOSPC;  // No space left
                    filesystem[i].data_blocks[b] = new_block;
                }
            }

            // Write data to blocks
            off_t current_offset = offset;
            size_t remaining_size = size;
            while (remaining_size > 0) {
                int block_index = current_offset / BLOCK_SIZE;
                int block_offset = current_offset % BLOCK_SIZE;
                int write_size = BLOCK_SIZE - block_offset < remaining_size ? BLOCK_SIZE - block_offset : remaining_size;
                
                // Find physical block index from inode's block array
                int block_number = filesystem[i].data_blocks[block_index];
                char* block_data = get_block_data(block_number);  // Assuming function to map block number to memory
                
                memcpy(block_data + block_offset, buf + (size - remaining_size), write_size);

                remaining_size -= write_size;
                current_offset += write_size;
            }

            // Update the size of the file if necessary
            if (offset + size > filesystem[i].size) {
                filesystem[i].size = offset + size;
            }

            return size;
        }
    }

    // If the file was not found, return an error.
    return -ENOENT;
}



int dm510fs_mkdir(const char *path, mode_t mode) {
    printf("mkdir: (path=%s)\n", path);

    // Check if the directory already exists
    for (int i = 0; i < MAX_INODES; i++) {
        if (filesystem[i].is_active && strcmp(filesystem[i].path, path) == 0) {
            printf("mkdir: Directory or file with the same path already exists\n");
            return -EEXIST; // Directory already exists
        }
    }

    // Locate the first unused Inode in the filesystem
    for (int i = 0; i < MAX_INODES; i++) {
        if (!filesystem[i].is_active) { // Found an unused inode
            printf("mkdir: Found unused inode at location %i\n", i);
            
            // Initialize the inode as a directory
            filesystem[i].is_active = true;
            filesystem[i].is_dir = true;
            filesystem[i].mode = S_IFDIR | mode; // Respect the mode specified in the function call
            filesystem[i].nlink = 2; // One link from the parent directory, one from itself (".")
            strncpy(filesystem[i].path, path, MAX_PATH_LENGTH); // Ensure no buffer overflow

            debug_inode(i);
            return 0; // Success
        }
    }

    printf("mkdir: No free inodes available\n");
    return -ENOSPC; // No space left to create new directory
}


// Make files 
int dm510fs_mknod(const char *path, mode_t mode, dev_t rdev) {
    printf("mknod: (path=%s, mode=%o)\n", path, mode);

    // Check if the path already exists
    for (int i = 0; i < MAX_INODES; i++) {
        if (filesystem[i].is_active && strcmp(filesystem[i].path, path) == 0) {
            printf("mknod: File already exists\n");
            return -EEXIST;
        }
    }

    // Find an inactive inode
    int inode_index = -1;
    for (int i = 0; i < MAX_INODES; i++) {
        if (!filesystem[i].is_active) {
            inode_index = i;
            break;
        }
    }

    if (inode_index == -1) {
        printf("mknod: No available inode\n");
        return -ENOSPC;  // No space left on device
    }

    // Initialize the inode
    Inode *inode = &filesystem[inode_index];
    inode->is_active = true;
    inode->is_dir = false;
    inode->mode = mode | S_IFREG; // Ensure the mode explicitly sets this as a regular file
    inode->nlink = 1;
    inode->size = 0;  // Initially, the size is 0 because no data is written yet
    strncpy(inode->path, path, MAX_PATH_LENGTH);

    // Clear block pointers
    memset(inode->data_blocks, 0, sizeof(inode->data_blocks));  // Ensure all data block pointers are initialized to zero

    printf("mknod: File created at inode %d\n", inode_index);
    return 0;
}




// Remove a file.
int dm510fs_unlink(const char *path) {
    printf("unlink: (path=%s)\n", path);

    // Loop through the inode array to find and remove the file
    for (int i = 0; i < MAX_INODES; i++) {
        if (filesystem[i].is_active && strcmp(filesystem[i].path, path) == 0) {
            // File found, deactivate the inode
            filesystem[i].is_active = false;
            filesystem[i].size = 0;

            // Free all allocated blocks
            for (int j = 0; j < MAX_POINTERS; j++) {
                if (filesystem[i].data_blocks[j] != 0) { // Assuming 0 means unallocated
                    free_block(filesystem[i].data_blocks[j]); // Free each allocated block
                    filesystem[i].data_blocks[j] = 0; // Reset block pointer to 0
                }
            }

            printf("Unlink: Successfully removed %s at location %i\n", path, i);
            return 0;  // Success
        }
    }

    // If we finish the loop without finding the file, it doesn't exist
    printf("Unlink: File %s not found\n", path);
    return -ENOENT;  // No such file
}



// Remove a directory.
int dm510fs_rmdir(const char *path) {
    printf("rmdir: (path=%s)\n", path);

    // First, checks if the directory is at the root and is "/", which should not be removed
    if (strcmp(path, "/") == 0) {
        printf("Cannot remove root directory\n");
        return -EBUSY; // Root directory cannot be removed
    }

    // Locates the inode representing the directory
    for (int i = 0; i < MAX_INODES; i++) {
        if (filesystem[i].is_active && strcmp(filesystem[i].path, path) == 0 && filesystem[i].is_dir) {
            // Check if the directory is empty
            bool is_empty = true;
            for (int j = 0; j < MAX_INODES; j++) {
                if (filesystem[j].is_active && j != i && strncmp(filesystem[j].path, path, strlen(path)) == 0) {
                    if (strlen(filesystem[j].path) > strlen(path)) { // Ensures it checks within the directory
                        if (filesystem[j].path[strlen(path)] == '/' && filesystem[j].path[strlen(path) + 1] != '\0') {
                            is_empty = false;
                            break;
                        }
                    }
                }
            }

            if (!is_empty) {
                printf("Directory is not empty\n");
                return -ENOTEMPTY; // Directory not empty
            }

            // If the directory is empty, deactivate the inode
            filesystem[i].is_active = false;
            memset(&filesystem[i], 0, sizeof(Inode)); // Clear the inode to reset it
            printf("Directory removed successfully\n");
            return 0; // Success
        }
    }

    // If no matching directory is found
    printf("No such directory\n");
    return -ENOENT; // No such directory
}



// Renames a file or directory.
int dm510fs_rename(const char *oldpath, const char *newpath) {
    printf("rename: (oldpath=%s, newpath=%s)\n", oldpath, newpath);

    int i, target_idx = -1;

    // Check if the new path already exists
    for (i = 0; i < MAX_INODES; i++) {
        if (filesystem[i].is_active && strcmp(filesystem[i].path, newpath) == 0) {
            target_idx = i;
            break;
        }
    }

    // Finds the inode for the old path
    for (i = 0; i < MAX_INODES; i++) {
        if (filesystem[i].is_active && strcmp(filesystem[i].path, oldpath) == 0) {
            // If the new path exists and is a non-empty directory, return error
            if (target_idx != -1 && filesystem[target_idx].is_dir) {
                // Check if the directory is empty
                bool is_empty = true;
                for (int j = 0; j < MAX_INODES; j++) {
                    if (filesystem[j].is_active && j != target_idx && strncmp(filesystem[j].path, newpath, strlen(newpath)) == 0) {
                        if (strlen(filesystem[j].path) > strlen(newpath)) {
                            if (filesystem[j].path[strlen(newpath)] == '/' && filesystem[j].path[strlen(newpath) + 1] != '\0') {
                                is_empty = false;
                                break;
                            }
                        }
                    }
                }
                if (!is_empty) {
                    printf("rename: Target directory is not empty\n");
                    return -ENOTEMPTY;
                }
            }

            // If it's an existing file or an empty directory, deactivate it
            if (target_idx != -1) {
                filesystem[target_idx].is_active = false;
            }

            // Update the inode with the new path
            strncpy(filesystem[i].path, newpath, MAX_PATH_LENGTH);
            filesystem[i].path[MAX_PATH_LENGTH - 1] = '\0'; // Ensure null termination
            printf("rename: Successfully renamed %s to %s\n", oldpath, newpath);
            return 0;
        }
    }

    printf("rename: Old path does not exist\n");
    return -ENOENT;
}




// Updates the access and modification times of a file with nanosecond precision.
int dm510fs_utime(const char *path, struct utimbuf *ubuf) {
    fprintf(stderr, "utime: (path=%s)\n", path);

    // Find the inode corresponding to the path
    for (int i = 0; i < MAX_INODES; i++) {
        if (filesystem[i].is_active && strcmp(filesystem[i].path, path) == 0) {
            time_t now = time(NULL); // Get current time

            // If times are provided, update to those times
            if (ubuf != NULL) {
                filesystem[i].atime = ubuf->actime;
                filesystem[i].mtime = ubuf->modtime;
            } else {
                // If no times are provided, set to current time
                filesystem[i].atime = now;
                filesystem[i].mtime = now;
            }
            
            // Update the change time to the current time
            filesystem[i].ctime = now;

            printf("utime: Updated times for %s\n", path);
            return 0; // Success
        }
    }

    fprintf(stderr, "utime: File not found %s\n", path);
    return -ENOENT; // No such file
}


// Changes size of a file 
int dm510fs_truncate(const char *path, off_t new_size) {
    printf("truncate: (path=%s, new_size=%lld)\n", path, (long long)new_size);

    // Finds the inode for the given path
    for (int i = 0; i < MAX_INODES; i++) {
        if (filesystem[i].is_active && strcmp(filesystem[i].path, path) == 0) {
            // Shrinking or expanding the file requires different handling
            if (new_size < filesystem[i].size) {
                // Shrinks the file
                int new_blocks_needed = (new_size + BLOCK_SIZE - 1) / BLOCK_SIZE; // Calculate number of blocks needed
                for (int j = new_blocks_needed; j < MAX_POINTERS && filesystem[i].data_blocks[j] != 0; j++) {
                    free_block(filesystem[i].data_blocks[j]); // Free each block no longer needed
                    filesystem[i].data_blocks[j] = 0;
                }
            } else if (new_size > filesystem[i].size) {
                // Extending the file
                int current_blocks = (filesystem[i].size + BLOCK_SIZE - 1) / BLOCK_SIZE;
                int needed_blocks = (new_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
                for (int j = current_blocks; j < needed_blocks; j++) {
                    int new_block = allocate_block();
                    if (new_block == -1) return -ENOSPC; // No space left
                    filesystem[i].data_blocks[j] = new_block;
                    char* block_data = get_block_data(new_block); // Assuming a function to fetch block data
                    memset(block_data, 0, BLOCK_SIZE); // Zero out new block
                }
            }

            // Set the new file size
            filesystem[i].size = new_size;

            // Update the inode modification and change time to the current time
            time_t now = time(NULL);
            filesystem[i].mtime = now;
            filesystem[i].ctime = now;

            return 0; // Success
        }
    }

    return -ENOENT; // File not found
}



/*
 * This is the only FUSE function that doesn't have a directly corresponding system call, although close(2) is related.
 * Release is called when FUSE is completely done with a file; at that point, you can free up any temporarily allocated data structures.
 */
int dm510fs_release(const char *path, struct fuse_file_info *fi) {
	printf("release: (path=%s)\n", path);
	return 0;
}

/**
 * Initialize filesystem
 *
 * The return value will passed in the `private_data` field of
 * `struct fuse_context` to all file operations, and as a
 * parameter to the destroy() method. It overrides the initial
 * value provided to fuse_main() / fuse_new().
 */
void* dm510fs_init() {
    printf("init filesystem\n");
    FILE *fp = fopen(FS_STATE_FILE, "rb");
    if (fp) {
        // File exists, load state
        printf("Loading existing filesystem state.\n");
        fread(filesystem, sizeof(Inode), MAX_INODES, fp);
        fclose(fp);
    } else {
        // File does not exist, initialize default state
        printf("No existing state, initializing new filesystem.\n");
    
        // Loop through all inodes - set them inactive
        for (int i = 0; i < MAX_INODES; i++) {
            filesystem[i].is_active = false;
            memset(filesystem[i].data_blocks, 0, sizeof(filesystem[i].data_blocks)); // Ensure all data blocks are zeroed
        }

        // Initialize root directory inode
        filesystem[0].is_active = true;
        filesystem[0].is_dir = true;
        filesystem[0].mode = S_IFDIR | 0755;
        filesystem[0].nlink = 2;
        strncpy(filesystem[0].path, "/", MAX_PATH_LENGTH);

        // Optionally add a 'hello' file to the filesystem
        filesystem[1].is_active = true;
        filesystem[1].is_dir = false;
        filesystem[1].mode = S_IFREG | 0777;
        filesystem[1].nlink = 1;
        filesystem[1].size = 12; // Assuming 'Hello World!' fits within the first block
        strncpy(filesystem[1].path, "/hello", MAX_PATH_LENGTH);
        
        // Allocate initial block for the hello file if necessary
        int block_index = allocate_block();
        if (block_index != -1) {
            filesystem[1].data_blocks[0] = block_index;
            char* block_data = get_block_data(block_index);
            if (block_data) {
                strcpy(block_data, "Hello World!");
            }
        }
    }
    return NULL;
}


/**
 * Clean up filesystem
 * Called on filesystem exit.
 */
void dm510fs_destroy(void *private_data) {
    printf("destroy filesystem\n");
    save_fs_state(); // Save the filesystem state to the file
}


int main( int argc, char *argv[] ) {
	return fuse_main( argc, argv, &dm510fs_oper );
}

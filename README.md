## This is a filechecker for xv6 filesystem image. For more information on xv6 visit https://pdos.csail.mit.edu/6.828/2017/xv6.html.

## Implementation of the xv6 filesystem (borrowed from xv6 documentation)

### Data Structures

Root inode number: 1
Block size: 512 bytes

#### Superblock

```c
// File system super block
struct superblock {
    uint size;         // Size of file system image (blocks)
    uint nblocks;      // Number of data blocks
    uint ninodes;      // Number of inodes.
};
```
#### Inodes

The direct pointers are listed in the array addrs (count = NDIRECT). The indirect pointers (count = NINDIRECT) are listed in the data block called indirect block. The last entry in the addrs array is the pointer to the indirect block. The first NDIRECT * 512 bytes (6KB) are stored in the idnode while next NINDIRECT * 512 bytes (64KB) are stored in the indirect pointers. An addr = 0 means the block is not allocated.

```c
NDIRECT: 12 // Number of direct pointers to data blocks       

// On-disk inode structure
struct dinode {
    short type;           	// Distinguishes b/w files, directories and special files 
    short major;          	// Major device number (T_DEV only)
    short minor;          	// Minor device number (T_DEV only)
    short nlink;          	// Number of directories linking to this inode
    uint size;            	// Size of file (bytes)
    uint addrs[NDIRECT+1];   	// Data block addresses
};
```
#### Directories

The type = T_DIR in the idnode indicates that the inode is for the file. File is represented by the following structure. inum field represent the inode number for the directory. If the inode number = 0, then the entry is free. Directory names can only be 14 characters long. If the name is shorter than 14 characters then the name is null terminated.

```c
struct dirent {
    ushort inum;
    char name[DIRSIZ];
};
```
#### Files

Field ref keeps track of number of references to this file.

```c
struct file {
    enum { FD_NONE, FD_PIPE, FD_INODE } type;
    int ref; // reference count
    char readable;
    char writable;
    struct pipe *pipe;
    struct inode *ip;
    uint off;
};
```
### Checks
This FileSystem checker looks for the following inconsistencies and throws an appropriate error

* Each inode is either unallocated or one of the valid types (T_FILE, T_DIR, T_DEV). If not, print ERROR: bad inode.

* For in-use inodes, each address that is used by inode is valid (points to a valid datablock address within the image). If the direct block is used and is invalid, print ERROR: bad direct address in inode.; if the indirect block is in use and is invalid, print ERROR: bad indirect address in inode.

* Root directory exists, its inode number is 1, and the parent of the root directory is itself. If not, print ERROR: root directory does not exist.

* Each directory contains . and .. entries, and the . entry points to the directory itself. If not, print ERROR: directory not properly formatted.

* For in-use inodes, each address in use is also marked in use in the bitmap. If not, print ERROR: address used by inode but marked free in bitmap.

* For blocks marked in-use in bitmap, the block should actually be in-use in an inode or indirect block somewhere. If not, print ERROR: bitmap marks block in use but it is not in use.

* For in-use inodes, each direct address in use is only used once. If not, print ERROR: direct address used more than once.

* For in-use inodes, each indirect address in use is only used once. If not, print ERROR: indirect address used more than once.

* For all inodes marked in use, each must be referred to in at least one directory. If not, print ERROR: inode marked use but not found in a directory.

* For each inode number that is referred to in a valid directory, it is actually marked in use. If not, print ERROR: inode referred to in directory but marked free.

* Reference counts (number of links) for regular files match the number of times file is referred to in directories (i.e., hard links work correctly). If not, print ERROR: bad reference count for file.

* No extra links allowed for directories (each directory only appears in one other directory). If not, print ERROR: directory appears more than once in file system.

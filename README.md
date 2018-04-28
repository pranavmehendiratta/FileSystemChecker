## This is the filechecker for xv6 filesystem image. For more information on xv6 visit https://pdos.csail.mit.edu/6.828/2017/xv6.html.

### Implementation of the xv6 filesystem (borrowed from xv6 documentation)

#### Data Structures

Root inode number: 1
Block size: 512 bytes



##### Superblock

```c
// File system super block
struct superblock {
    uint size;         // Size of file system image (blocks)
    uint nblocks;      // Number of data blocks
    uint ninodes;      // Number of inodes.
};
```
##### Inodes

The direct pointers are listed in the array addrs (count = NDIRECT). The indirect 
pointers (count = NINDIRECT) are listed in the data block called indirect block.
The last entry in the addrs array is the pointer to the indirect block. The first 
NDIRECT * 512 bytes (6KB) are stored in the idnode while next NINDIRECT * 512 bytes 
(64KB) are stored in the indirect pointers. An addr = 0 means the block is not 
allocated.

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
##### Directories

The type = T_DIR in the idnode indicates that the inode is for the file. File is 
represented by the following structure. inum field represent the inode number for
the directory. If the inode number = 0, then the entry is free. Directory names
can only be 14 characters long. If the name is shorter than 14 characters then
the name is null terminated.

```c
struct dirent {
    ushort inum;
    char name[DIRSIZ];
};
```
##### Files

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

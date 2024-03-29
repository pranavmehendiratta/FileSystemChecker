#include<stdio.h>
#include<stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h> // mmap is in this header
#include <sys/stat.h> // struct stat is in this header file
#include "fs.h" // File System structs

int fsfd;
int inodeBlks;
int bmstart;
int bmend;
int dbstart;
int dbend;
int *bitmapInfo;
int *indirectPtrs;
int *inodesInUse;
int *refCount;
int *refCountDir;

#define T_UNALLOC 0
#define T_DIR 1
#define T_FILE 2
#define T_DEV 3

void printError(char* msg) {
    fprintf(stderr, "%s\n", msg);
    free(bitmapInfo);
    free(indirectPtrs);
    free(inodesInUse);
    free(refCount);
    free(refCountDir);
    exit(1);
}

char* mmap_helper(int fd, off_t offset, size_t length, struct stat sb) {
    char *addr;
    off_t pa_offset;

    pa_offset = offset & ~(sysconf(_SC_PAGE_SIZE) - 1);
    /* offset for mmap() must be page aligned */

    if (offset >= sb.st_size) {
	fprintf(stderr, "offset is past end of file\n");
	exit(EXIT_FAILURE);
    }

    addr = mmap(NULL, length + offset - pa_offset, PROT_READ,
	    MAP_PRIVATE, fd, pa_offset);
    
    if (addr == MAP_FAILED) {
	printf("cannot mmap the file - mmap error\n");
    }

    return addr;
}

int
i2b(uint inum)
{
    return (inum / IPB) + 2;
}

void
rsect(uint sec, void *buf)
{
    if(lseek(fsfd, sec * 512L, 0) != sec * 512L){
	perror("lseek");
	exit(1);
    }
    if(read(fsfd, buf, 512) != 512){
	perror("read");
	exit(1);
    }
}

// Helper for reading the inodes
void
rinode(uint inum, struct dinode *ip)
{
    char buf[512];
    uint bn; 
    struct dinode *dip;

    bn = i2b(inum);
    rsect(bn, buf);
    dip = ((struct dinode*)buf) + (inum % IPB);
    *ip = *dip;
}

int isNthBitTrue(unsigned char c, int n) {
    static unsigned char mask[] = {1, 2, 4, 8, 16, 32, 64, 128};
    return ((c & mask[n]) != 0);
}

void checkBitmap(int block, char* msg, int bmstart) {
    char buf[BSIZE];
    rsect(bmstart + (block / (BSIZE * 8)), buf);	

    int byte = block / 8;
    int bit = block % 8;

    if (isNthBitTrue(buf[byte], bit) == 0) {
	printError(msg);
    }
}

void checkInodeBlock(uint inum, char *name, int* refCount, int* refCountDir) {
    struct dinode din;
    rinode(inum, &din);
   
    //if (din.type != T_DIR && din.type != T_FILE && din.type != T_DEV) {
    if (din.type <= 0) {
	char* msg = "ERROR: inode referred to in directory but marked free.";
	printError(msg);
    }
    
    if (din.type == T_FILE) {
        refCount[inum]--;
    }
    
    if (din.type == T_DIR && strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
        refCountDir[inum]--;
    }
}

void processDirDataBlock(struct superblock *sblk, char *buf, int *inodesInUse, 
			    int startIndex, int* refCount, int* refCountDir) {
    int max_dirent = BSIZE / sizeof(struct dirent);

    for (int dir = startIndex; dir < max_dirent; dir++) {
	struct dirent *entry;
	entry = (struct dirent*)(buf + (dir * sizeof(struct dirent)));
	if (entry->inum != 0) {
	    inodesInUse[entry->inum]--;
	    checkInodeBlock(entry->inum, entry->name, refCount, refCountDir);
	}
    }
}

void processDirectory (int inum, int* inodesInUse, struct superblock *sblk, uint addr, 
		    int proc_dot_dot, int* refCount, int* refCountDir) {

    // Getting the data block for the directory
    char buf[BSIZE];
    rsect(addr, buf);

    if (proc_dot_dot == 1) {
	// Getting the . directory
	struct dirent *curDir;
	curDir = (struct dirent*)buf;

	// Checking the parent directory
	struct dirent *parentDir;
	parentDir = (struct dirent*)(buf + sizeof(struct dirent));

	// Checking . and .. for root directory
	if (inum == 1) {
	    if (curDir->inum != parentDir->inum || curDir->inum != 1 || parentDir->inum != 1) {
		char* msg = "ERROR: root directory does not exist.";
		printError(msg);
	    }
	}

	// Checking that . directory refers to inode itself and its name is correct
	if (inum != curDir->inum || strcmp(curDir->name, ".") != 0) {
	    char* msg = "ERROR: directory not properly formatted.";
	    printError(msg);
	}

	if (strcmp(parentDir->name, "..") != 0) {
	    char* msg = "ERROR: directory not properly formatted.";
	    printError(msg);
	}

	// Updating the inodes in use
	if (curDir->inum != 0) {
	    inodesInUse[curDir->inum]--;
	}	

	if (parentDir->inum != 0) {
	    inodesInUse[parentDir->inum]--;
	}	

	// Checking that the inodes . and .. refers to are marked valid in the inode block
	checkInodeBlock(curDir->inum, curDir->name, refCount, refCountDir);
	checkInodeBlock(parentDir->inum, parentDir->name, refCount, refCountDir);

    }
    // Updating the inodes used
    int startIndex = proc_dot_dot == 1 ? 2 : 0;
    processDirDataBlock(sblk, buf, inodesInUse, startIndex, refCount, refCountDir);
}


void checkInodes(struct superblock *sblk, char* fsstart, int* bitmapInfo, 
	int* indirectPtrs, int* inodesInUse, int* refCount, int* refCountDir) {

    for (uint inum = 1; inum < sblk->ninodes; inum++) {
	struct dinode din;
	rinode(inum, &din);

	if (din.type < 0 || din.type > 3) {
	    char* msg = "ERROR: bad inode.";
	    printError(msg);
	}

	// Checking the inuse inodes
	if (din.type != T_UNALLOC) {

	    // Checking root inode
	    if (inum == 1) {
		if (din.type != T_DIR) {
		    char* msg = "ERROR: root directory does not exist.";
		    printError(msg);
		} 
	    }

	    // Checking the Direct pointers
	    for (int i = 0; i < NDIRECT; i++) {
		if (din.addrs[i] != 0) {
		    if (din.addrs[i] >= dbstart && din.addrs[i] <= dbend) {

			char* msg = "ERROR: address used by inode but marked free in bitmap.";
			checkBitmap(din.addrs[i], msg, bmstart);

			// Updating the bitmap reference
			bitmapInfo[din.addrs[i] - dbstart]--; 

			// Performing all the checks for directories
			if (din.type == T_DIR) {
			    uint direct_addr = din.addrs[i];

			    int proc_dot_dot = i == 0 ? 1 : 0;
			    processDirectory(inum, inodesInUse, sblk, direct_addr, proc_dot_dot, refCount, refCountDir); 
			}
		    } else {
			char *msg = "ERROR: bad direct address in inode.";
			printError(msg);
		    }
		} 
	    }

	    // Checking the indirect pointers
	    uint indirect = din.addrs[NDIRECT];
	    if (indirect != 0) {
		if (indirect >= dbstart && indirect <= dbend) {
		    char* msg = "ERROR: address used by inode but marked free in bitmap.";
		    checkBitmap(indirect, msg, bmstart);

		    // Saving the indirect pointer
		    indirectPtrs[indirect - dbstart]++;

		    // Updating the bitmap reference
		    bitmapInfo[indirect - dbstart]--; 

		    // Get the address to data block inside indirect pointer block
		    char buf[512];
		    rsect(indirect, &buf);

		    for (int i = 0; i < 128; i++) {
			uint datablk = (uint)(*((int*)(buf + (i * 4))));
			if (datablk != 0) {
			    if (datablk >= dbstart && datablk <= dbend) {

				char* msg = "ERROR: address used by inode but marked free in bitmap.";
				checkBitmap(datablk, msg, bmstart);

				// Updating indirect pointers
				indirectPtrs[datablk - dbstart]++;
				
				// Updating the bitmap reference
				bitmapInfo[datablk - dbstart]--; 

				// Performing all the checks for directories
				if (din.type == T_DIR) {
				    processDirectory(inum, inodesInUse, sblk, datablk, 0, refCount, refCountDir); 
				}
			    } else {
				char *msg = "ERROR: bad indirect address in inode.";
				printError(msg);
			    }
			}
		    }
		} else {
		    char *msg = "ERROR: bad indirect address in inode.";
		    printError(msg);
		}
	    }
	}
    }
}

void getSuperBlock(char* addr, struct superblock *sblk) {
    char* spaddr = addr + BSIZE;
    struct superblock *temp;
    temp = ((struct superblock*)spaddr); 
    *sblk = *temp;
}

void getBitmapInfo(int* bitmapInfo, struct superblock *sblk) {
    char buf[BSIZE];
    rsect(bmstart, buf);	

    for (int j = 0, i = dbstart; i < dbend; i++, j++) {
	int byte = i / 8;
	int bit = i % 8;
	bitmapInfo[j] = isNthBitTrue(buf[byte], bit);
    }
} 

void getInodesInUse(int *inodesInUse, struct superblock *sblk) {
    for (uint inum = 1; inum < sblk->ninodes; inum++) {
	struct dinode din;
	rinode(inum, &din);
	if (din.type == T_DIR || din.type == T_FILE || din.type == T_DEV) {
	    inodesInUse[inum] = 1;
	}
    }
}

void getRefCount(int *refCount, struct superblock *sblk, int* refCountDir) {
    for (uint inum = 1; inum < sblk->ninodes; inum++) {
	struct dinode din;
	rinode(inum, &din);
	if (din.type == T_FILE) {
	    refCount[inum] = din.nlink;
	} else if (din.type == T_DIR) {
	    refCountDir[inum] = 1;
	}
    }
    
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
	printf("Usage: xcheck <file_system_image>\n");
	exit(1);
    }

    //ssize_t s;
    char *fsstart;
    struct stat sb;
    off_t offset;//, pa_offset;
    size_t length;

    fsfd = open(argv[1], O_RDONLY);
    if (fsfd == -1) {
	char *msg = "image not found.";
	printError(msg);
    }


    if (fstat(fsfd, &sb) == -1) {      // To obtain file size 
	printf("cannot get file size - fstat error\n");
    }

    length = sb.st_size;
    offset = 0;

    fsstart = mmap_helper(fsfd, offset, length, sb);

    // Get the superblock
    struct superblock sblk;
    getSuperBlock(fsstart, &sblk);

    inodeBlks = (sblk.ninodes / (BSIZE / sizeof(struct dinode)));
    bmstart = 3 + inodeBlks; // 1st bitmap block
    bmend = bmstart + (sblk.nblocks / (BSIZE * 8));
    dbstart = bmend + 1; // 1st data block number
    dbend = dbstart + sblk.nblocks;

    bitmapInfo = malloc(sizeof(int) * sblk.nblocks);

    if (bitmapInfo == 0) {
	fprintf(stderr, "Unable to allocate memory");
    }

    indirectPtrs = malloc(sizeof(int) * sblk.nblocks);

    if (indirectPtrs == 0) {
	fprintf(stderr, "Unable to allocate memory");
    }

    inodesInUse = malloc(sizeof(int) * sblk.ninodes);

    if (inodesInUse == 0) {
	fprintf(stderr, "Unable to allocate memory");
    }
    
    refCount = malloc(sizeof(int) * sblk.ninodes);

    if (refCount == 0) {
	fprintf(stderr, "Unable to allocate memory");
    }

    refCountDir = malloc(sizeof(int) * sblk.ninodes);

    if (refCountDir == 0) {
	fprintf(stderr, "Unable to allocate memory");
    }
    
    // Getting the bitmap info
    getBitmapInfo(bitmapInfo, &sblk);

    // Getting the inodes status
    getInodesInUse(inodesInUse, &sblk); 

    // Get inodes reference links
    getRefCount(refCount, &sblk, refCountDir);

    // Check the inodes
    checkInodes(&sblk, fsstart, bitmapInfo, indirectPtrs, inodesInUse, refCount, refCountDir);

    for (int i = 0; i < sblk.nblocks; i++) {
	if (bitmapInfo[i] == 1) {
	    char *msg = "ERROR: bitmap marks block in use but it is not in use.";
	    printError(msg);
	} else if (bitmapInfo[i] < 0) {
	    // Checking if the block used twice is indirect
	    if (indirectPtrs[i] > 0) {
		char* msg = "ERROR: indirect address used more than once.";
		printError(msg);
	    }
	    // Checking if the block used twice is indirect
	    char* msg = "ERROR: direct address used more than once.";
	    printError(msg);
	}
    }

    // Checking if all the inodes are refered in some directory
    for (int i = 0; i < sblk.ninodes; i++) {
        if (inodesInUse[i] == 1) {
            char *msg = "ERROR: inode marked use but not found in a directory.";
            printError(msg);
        }

	if (refCount[i] >= 1 || refCount[i] < 0) {
            char *msg = "ERROR: bad reference count for file.";
            printError(msg);
	}
    
	if (refCountDir[i] < 0) {
            char *msg = "ERROR: directory appears more than once in file system.";
            printError(msg);
	}
    
    }
    
    // Freeing all the memory
    int ret = munmap(fsstart, length);
    if (ret != 0) {
	fprintf(stderr, "Unable to munmap. Return value: %d\n", ret);
    }

    // Closing the image file and free the memory
    close(fsfd);

    free(bitmapInfo);
    free(indirectPtrs);
    free(inodesInUse);
    free(refCount);
    free(refCountDir);
    return 0;
}

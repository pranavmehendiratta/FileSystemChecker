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

#define T_UNALLOC 0
#define T_DIR 1
#define T_FILE 2
#define T_DEV 3

void printError(char* msg) {
    printf("INSIDE PRINTERROR\n");
    fprintf(stderr, "%s\n", msg);
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
    //printf("valid addr found: %d\n", block);

    char buf[BSIZE];
    rsect(bmstart + (block / (BSIZE * 8)), buf);	

    int byte = block / 8;
    int bit = block % 8;

    if (isNthBitTrue(buf[byte], bit) == 0) {
	printError(msg);
    }
}

void checkInodeBlock(uint inum) {
    struct dinode din;
    rinode(inum, &din);
   
    printf("Inside checkInodeBlock -> inum: %d, type: %d\n", inum, din.type);

    //if (din.type != T_DIR && din.type != T_FILE && din.type != T_DEV) {
    if (din.type <= 0) {
	char* msg = "ERROR: inode referred to in directory but marked free.";
	printError(msg);
    }
}

void updateInodesInUse(struct superblock *sblk, char *buf, int *inodesInUse, int startIndex) {
    printf("Inside UpdateInodesInUse startIndex: %d\n", startIndex);
    int max_dirent = BSIZE / sizeof(struct dirent);
    printf("# of max_dirent: %d\n", max_dirent);

    for (int dir = startIndex; dir < max_dirent; dir++) {
	struct dirent *entry;
	entry = (struct dirent*)(buf + (dir * sizeof(struct dirent)));
	if (entry->inum != 0) {
	    printf("inum: %d, name: %s\n", entry->inum, entry->name);
	    inodesInUse[entry->inum]--;
	    checkInodeBlock(entry->inum);
	}
    }
}

void processDirectory (int inum, int* inodesInUse, struct superblock *sblk, uint addr, int proc_dot_dot) {

    printf("Inside process directory inum: %d, proc_dot_dot: %d\n", inum, proc_dot_dot);

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

	printf("curDir->name: %s, curDir->inum: %d\n", curDir->name, curDir->inum);
	printf("parentDir->name: %s, parentDir->inum: %d\n", parentDir->name, parentDir->inum);

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
	checkInodeBlock(curDir->inum);
	checkInodeBlock(parentDir->inum);

    }
    // Updating the inodes used
    int startIndex = proc_dot_dot == 1 ? 2 : 0;
    updateInodesInUse(sblk, buf, inodesInUse, startIndex);
}


void checkInodes(struct superblock *sblk, char* fsstart, int* bitmapInfo, 
	int* indirectPtrs, int* inodesInUse) {

    printf("\n\n");

    printf("File system start: %p\n", fsstart);
    printf("Dinode size: %zu\n", sizeof(struct dinode));

    for (uint inum = 1; inum < sblk->ninodes; inum++) {
	struct dinode din;
	rinode(inum, &din);

	if (din.type < 0 || din.type > 3) {
	    char* msg = "ERROR: bad inode.";
	    printError(msg);
	}

	// Checking the inuse inodes
	if (din.type != T_UNALLOC) {
	    printf("---- inum: %d, type: %hu ----\n", inum, din.type);

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
			printf("Direct Ptr: %d\n", din.addrs[i]);

			// Updating the bitmap reference
			bitmapInfo[din.addrs[i] - dbstart]--; 

			// Performing all the checks for directories
			if (din.type == T_DIR) {
			    uint direct_addr = din.addrs[i];

			    int proc_dot_dot = i == 0 ? 1 : 0;
			    processDirectory(inum, inodesInUse, sblk, direct_addr, proc_dot_dot); 
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
		    printf("-- Starting indirect pointers --\n");
		    printf("Indirect Ptr: %d\n", indirect);


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

				printf("Indirect -> direct Ptr: %d\n", datablk);

				// Updating indirect pointers
				indirectPtrs[datablk - dbstart]++;
				
				// Updating the bitmap reference
				bitmapInfo[datablk - dbstart]--; 

				// Performing all the checks for directories
				if (din.type == T_DIR) {
				    printf("--> Dir inside indirect pointer\n");
				    processDirectory(inum, inodesInUse, sblk, datablk, 0); 
				}
				//printf("Block #: %d\n", datablk);
				// TODO: Should this error be 'indirect' instead of 'direct'?
			    } else {
				char *msg = "ERROR: bad direct address in inode.";
				printError(msg);
			    }
			}
		    }
		} else {
		    char *msg = "ERROR: bad indirect address in inode.";
		    printError(msg);
		}
	    }
	    printf("\n");
	}
    }
}

void getSuperBlock(char* addr, struct superblock *sblk) {
    char* spaddr = addr + BSIZE;
    struct superblock *temp;
    temp = ((struct superblock*)spaddr); 
    *sblk = *temp;
    printf("size of filesystem: %d\n", sblk->size);
    printf("# of data blocks: %d\n", sblk->nblocks);
    printf("# of inodes: %d\n", sblk->ninodes);
}

void getBitmapInfo(int* bitmapInfo, struct superblock *sblk) {
    char buf[BSIZE];
    rsect(bmstart, buf);	

    printf("Bitmap start: %d\n", bmstart);

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
    printf("# of inode blocks:  %d\n", inodeBlks);

    bmstart = 3 + inodeBlks; // 1st bitmap block
    bmend = bmstart + (sblk.nblocks / (BSIZE * 8));

    printf("Bitmap start #: %d\n", bmstart); 
    printf("Bitmap end #: %d\n", bmend); 

    dbstart = bmend + 1; // 1st data block number
    dbend = dbstart + sblk.nblocks;

    printf("Data Block start #: %d\n", dbstart); 
    printf("Data Block end #: %d\n", dbend); 

    int *bitmapInfo = (int*)malloc(sizeof(int) * sblk.nblocks);

    if (bitmapInfo == 0) {
	fprintf(stderr, "Unable to allocate memory");
    }

    int *indirectPtrs = (int*)malloc(sizeof(int) * sblk.nblocks);

    if (indirectPtrs == 0) {
	fprintf(stderr, "Unable to allocate memory");
    }

    int *inodesInUse = (int*)malloc(sizeof(int) * sblk.ninodes);

    if (inodesInUse == 0) {
	fprintf(stderr, "Unable to allocate memory");
    }

    printf("bitmapInfo: %p\n", bitmapInfo);
    printf("indirectPtrs: %p\n", indirectPtrs);
    
    // Getting the bitmap info
    getBitmapInfo(bitmapInfo, &sblk);

    printf("--- Checking bitmapInfo ---\n");
    for (int i = 0; i < sblk.nblocks; i++) {
	printf("Data Block: %d, in-use: %d\n", (i + dbstart), bitmapInfo[i]);
    }

    // Getting the inodes status
    getInodesInUse(inodesInUse, &sblk); 

    //printf("--- Checking the inodes in use array ---\n"); 
    //for (int inum = 0; inum < sblk.ninodes; inum++) {
    //    printf("inum: %d, inuse: %d\n", inum, inodesInUse[inum]);
    //}

    // Check the inodes
    checkInodes(&sblk, fsstart, bitmapInfo, indirectPtrs, inodesInUse);

    printf("\n\n");

    printf("--- Bitmap Info ----\n");


    for (int i = 0; i < sblk.nblocks; i++) {
	//printf("Bit: %d, value: %d\n", i, bitmapInfo[i]);
	if (bitmapInfo[i] == 1) {
	    //printf("Block: %d not used\n", i);

	    char *msg = "ERROR: bitmap marks block in use but it is not in use.";
	    printError(msg);
	} else if (bitmapInfo[i] < 0) {
	    // Checking if the block used twice is indirect
	    if (indirectPtrs[i] > 0) {
		//printf("Indirect ptr used more than once: %d \n", i);
		char* msg = "ERROR: indirect address used more than once.";
		printError(msg);
	    }
	    //printf("inum: %d, indirectPtr: %d\n", i, indirectPtrs[i]);

	    printf("ptr: %d\n", (i + dbstart));

	    // Checking if the block used twice is indirect
	    //printf("Direct ptr used more than once: %d \n", i);
	    char* msg = "ERROR: direct address used more than once.";
	    printError(msg);
	}
    }

    // Checking if all the inodes are refered in some directory
    //for (int i = 0; i < sblk.ninodes; i++) {
    //    if (inodesInUse[i] == 1) {
    //        printf("inum: %d\n", i);
    //        char *msg = "ERROR: inode marked use but not found in a directory.";
    //        printError(msg);
    //    }
    //}

    printf("Done with everything\n");

    printf("bitmapInfo: %p\n", bitmapInfo);
    printf("indirectPtrs: %p\n", indirectPtrs);

    // Freeing all the memory
    int ret = munmap(fsstart, length);
    if (ret != 0) {
	fprintf(stderr, "Unable to munmap. Return value: %d\n", ret);
    }

    printf("\n\n");

    //printf("--- Checking the inodes in use array ---\n"); 
    //for (int inum = 0; inum < sblk.ninodes; inum++) {
    //    printf("inum: %d, inuse: %d\n", inum, inodesInUse[inum]);
    //}

    printf("--- Checking bitmapInfo ---\n");
    for (int i = 0; i < sblk.nblocks; i++) {
	printf("Data Block: %d, in-use: %d\n", (i + dbstart),  bitmapInfo[i]);
    }

    // Closing the image file and free the memory
    close(fsfd);

    free(bitmapInfo);
    free(indirectPtrs);
    free(inodesInUse);
    return 0;
}

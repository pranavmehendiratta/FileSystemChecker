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

void checkRootInode(int dbstart, int dbend, int bmstart, int bmend, int* bitmapInfo, int* indirectPtrs) {
    uint inum = 1;
    struct dinode din;
    rinode(inum, &din);

    // Make sure that the type is directory
    if (din.type == T_DIR) {

	// Checking the Direct pointers
	for (int i = 0; i < NDIRECT; i++) {
	    
	    if (din.addrs[i] != 0) {
	
		printf("Direct Ptr: %d\n", din.addrs[i]);

		if (din.addrs[i] >= dbstart && din.addrs[i] <= dbend) {
		    
		    // Test 5
		    char* msg = "ERROR: address used by inode but marked free in bitmap.";
		    checkBitmap(din.addrs[i], msg, bmstart);
	
		    // Set the data block address = (value - 1)
		    bitmapInfo[din.addrs[i] - dbstart]--; 

		    char buf[BSIZE];
		    rsect(din.addrs[i], buf);

		    struct dirent *curDir;
		    curDir = (struct dirent*)buf;
		    //printf("child inum: %d, child name: %s\n", curDir->inum, curDir->name);

		    struct dirent *parentDir;
		    parentDir = (struct dirent*)(buf + 16);
		    
		    if (inum != curDir->inum && strcmp(curDir->name, ".") == 0) {
			char *msg = "ERROR: directory not properly formatted";
			printError(msg);
		    }
		    
		    //printf("parent inum: %d, parent name: %s\n", parentDir->inum, parentDir->name);
		    if (curDir->inum != parentDir->inum || curDir->inum != 1 || parentDir->inum != 1) {
			char* msg = "ERROR: root directory does not exist.";
			printError(msg);
		    }
		} else {
		    char* msg = "ERROR: bad direct address in inode.";
		    printError(msg);
		}
	    }
	}

	// Checking the indirect pointers
	uint indirect = din.addrs[NDIRECT];
	if (indirect != 0) { 

	    if (indirect >= dbstart && indirect <= dbend) {
		// Test 5
		char* msg = "ERROR: address used by inode but marked free in bitmap.";
		checkBitmap(indirect, msg, bmstart);
		
		printf("Indirect Ptr: %d\n", indirect);
		   
		// Saving the indirect pointer
		indirectPtrs[inum] = indirect;

		// Set the data block address = (value - 1)
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
		
			    printf("Indirect -> Direct Ptr: %d\n", datablk);

			    bitmapInfo[datablk - dbstart]--; 
			    
			    //printf("Block #: %d\n", datablk);
			    // TODO: Should this error be 'indirect' instead of 'direct'?
			} else {
			    char *msg = "ERROR: bad direct address in inode.";
			    printError(msg);
			}
		    }
		}
	    } else {
		char* msg = "ERROR: bad indirect address in inode.";
		printError(msg);
	    }
	}
    } else { 
	char* msg = "ERROR: root directory does not exist.";
	printError(msg);
    }
}

void checkInodes(struct superblock *sblk, char* fsstart, int* bitmapInfo, int* indirectPtrs) {

    printf("\n\n");

    printf("File system start: %p\n", fsstart);
    printf("Dinode size: %zu\n", sizeof(struct dinode));


    checkRootInode(dbstart, dbend, bmstart, bmend, bitmapInfo, indirectPtrs);

    for (uint inum = 2; inum < sblk->ninodes; inum++) {
	struct dinode din;
	rinode(inum, &din);

	if (din.type < 0 || din.type > 3) {
	    char* msg = "ERROR: bad inode.";
	    printError(msg);
	}

	// Checking the inuse inodes
	if (din.type != T_UNALLOC) {
	    printf("---- inum: %d, type: %hu ----\n", inum, din.type);

	    // Checking the Direct pointers
	    for (int i = 0; i < NDIRECT; i++) {
		if (din.addrs[i] != 0) {
		    if (din.addrs[i] >= dbstart && din.addrs[i] <= dbend) {
			
			printf("Direct Ptr: %d\n", din.addrs[i]);

			char* msg = "ERROR: address used by inode but marked free in bitmap.";
			checkBitmap(din.addrs[i], msg, bmstart);

			bitmapInfo[din.addrs[i] - dbstart]--; 
			
			// Getting the . directory
			if (din.type == T_DIR) {
			    char buf[BSIZE];
			    rsect(din.addrs[i], buf);
			    struct dirent *curDir;
			    curDir = (struct dirent*)buf;

			    printf("Dir name: '%s'\n", curDir->name);

			    if (inum != curDir->inum && strcmp(curDir->name, ".") == 0) {
				char* msg = "ERROR: directory not properly formatted.";
				printError(msg);
			    }
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
		    indirectPtrs[inum] = indirect;

		    // Set the data block address = (value - 1)
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
				
				bitmapInfo[datablk - dbstart]--; 

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

// Test 6
void updateBitmapInfo(int* bitmapInfo, struct superblock *sblk) {
    char buf[BSIZE];
    rsect(bmstart, buf);	
    
    printf("Bitmap start: %d\n", bmstart);

    for (int j = 0, i = dbstart; i < dbend; i++, j++) {
	int byte = i / 8;
	int bit = i % 8;
	bitmapInfo[j] = isNthBitTrue(buf[byte], bit);
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
    
    int *bitmapInfo;
    bitmapInfo = malloc(sizeof(int) * sblk.nblocks);
    
    if (bitmapInfo == 0) {
	fprintf(stderr, "Unable to allocate memory");
    }
    
    int *indirectPtrs;
    indirectPtrs = malloc(sizeof(int) * sblk.ninodes);

    if (indirectPtrs == 0) {
	fprintf(stderr, "Unable to allocate memory");
    }
    
    printf("bitmapInfo: %p\n", bitmapInfo);
    printf("indirectPtrs: %p\n", indirectPtrs);
    
    updateBitmapInfo(bitmapInfo, &sblk);
    //free(bitmapInfo);
    //free(indirectPtrs);

    //exit(0);
    

    //printf("--- Bitmap Info ----\n");
    //for (int i = 0; i < sblk.nblocks; i++) {
    //    printf("Bit: %d, value: %d\n", i, bitmapInfo[i]);
    //}

    // Check the inodes
    checkInodes(&sblk, fsstart, bitmapInfo, indirectPtrs);

    printf("\n\n");

    printf("--- Bitmap Info ----\n");
    
    
    for (int i = 0; i < sblk.nblocks; i++) {
	//printf("Bit: %d, value: %d\n", i, bitmapInfo[i]);
	if (bitmapInfo[i] == 1) {
	    //printf("Block: %d not used\n", i);
	    
	    char *msg = "ERROR: bitmap marks block in use but it is not in use.";
	    printError(msg);
	} else if (bitmapInfo < 0) {
	    // Checking if the block used twice is indirect
	    for (int ind = 0; ind < sblk.ninodes; ind++) {
		if ((i + dbstart) == indirectPtrs[ind]) {
		    //printf("Indirect ptr used more than once: %d \n", i);
		    char* msg = "ERROR: indirect address used more than once.";
		    printError(msg);
		}
		//printf("inum: %d, indirectPtr: %d\n", i, indirectPtrs[i]);
	    }

	    // Checking if the block used twice is indirect
	    //printf("Direct ptr used more than once: %d \n", i);
	    char* msg = "ERROR: direct address used more than once.";
	    printError(msg);
	}
    }

    printf("Done with everything\n");
    
    close(fsfd);

    printf("bitmapInfo: %p\n", bitmapInfo);
    printf("indirectPtrs: %p\n", indirectPtrs);

    // Freeing all the memory
    int ret = munmap(fsstart, length);
    if (ret != 0) {
        fprintf(stderr, "Unable to munmap. Return value: %d\n", ret);
    }
    
    free(bitmapInfo);
    free(indirectPtrs);

    exit(0);
}

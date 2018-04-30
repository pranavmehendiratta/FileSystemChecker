#include<stdio.h>
#include<stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h> // mmap is in this header
#include <sys/stat.h> // struct stat is in this header file
#include "fs.h" // File System structs

int fsfd;

#define handle_error(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

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
	handle_error("mmap");
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
    printf("valid addr found: %d\n", block);

    char buf[BSIZE];
    rsect(bmstart + (block / (BSIZE * 8)), buf);	

    int byte = block / 8;
    int bit = block % 8;

    if (isNthBitTrue(buf[byte], bit) == 0) {
	fprintf(stderr, "%s\n", msg);
	exit(1);
    }
}

void checkInodes(struct superblock *sblk, char* fsstart) {

    printf("\n\n");

    printf("File system start: %p\n", fsstart);
    printf("Dinode size: %zu\n", sizeof(struct dinode));

    int inodeBlks = (sblk->ninodes / (BSIZE / sizeof(struct dinode)));
    printf("# of inode blocks:  %d\n", inodeBlks);

    int bmstart = 3 + inodeBlks; // 1st bitmap block
    int bmend = bmstart + (sblk->nblocks / (BSIZE * 8));

    printf("Bitmap start #: %d\n", bmstart); 
    printf("Bitmap end #: %d\n", bmend); 

    int dbstart = bmend + 1; // 1st data block number
    int dbend = dbstart + sblk->nblocks;

    printf("Data Block start #: %d\n", dbstart); 
    printf("Data Block end #: %d\n", dbend); 

    for (uint inum = 1; inum < sblk->ninodes; inum++) {
	struct dinode din;
	rinode(inum, &din);

	if (din.type < 0 || din.type > 3) {
	    fprintf(stderr, "ERROR: bad inode.");
	    exit(1);
	}

	// Checking the inuse inodes
	if (din.type != 0) {
	    printf("---- inum: %d, type: %hu ----\n", inum, din.type);

	    // Checking the Direct pointers
	    for (int i = 0; i < NDIRECT; i++) {
		if (din.addrs[i] != 0 && din.addrs[i] >= dbstart && din.addrs[i] <= dbend) {
		    char *msg = "ERROR: bad direct address in inode.";
		    checkBitmap(din.addrs[i], msg, bmstart);
		} 
	    }

	    // Checking the indirect pointers
	    uint indirect = din.addrs[NDIRECT];
	    if (indirect != 0 && indirect >= dbstart && indirect <= dbend) {
		
		// Whether bitmap says its allocated
		char *msg = "ERROR: bad indirect address in inode.";
		checkBitmap(indirect, msg, bmstart);
		
		printf("-- Starting indirect pointers --\n");

		// Get the address to data block inside indirect pointer block
		char buf[512];
		rsect(indirect, &buf);
		for (int i = 0; i < 128; i++) {
		    uint datablk = (uint)(*((int*)(buf + (i * 4))));
		    if (datablk != 0 && datablk >= dbstart && datablk <= dbend) {
			//printf("Block #: %d\n", datablk);
			char *msg = "ERROR: bad direct address in inode.";
			checkBitmap(din.addrs[i], msg, bmstart);
		    } 
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
	handle_error("open");
    }


    if (fstat(fsfd, &sb) == -1) {      // To obtain file size 
	handle_error("fstat");
    }

    length = sb.st_size;
    offset = 0;

    fsstart = mmap_helper(fsfd, offset, length, sb);

    // Get the superblock
    struct superblock sblk;
    getSuperBlock(fsstart, &sblk);

    // Check the inodes
    checkInodes(&sblk, fsstart);

    close(fsfd);
}

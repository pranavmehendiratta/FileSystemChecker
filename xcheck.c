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

void checkInodes(struct superblock *sblk) {
    struct dinode din;
    for (uint inum = 1; inum <= sblk->ninodes; inum++) {
	rinode(inum, &din);
	
	if (din.type < 0 || din.type > 3) {
	    fprintf(stderr, "ERROR: bad inode.");
	    exit(1);
	}
	//printf("inum: %d, type: %hu\n", inum, din.type);
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
    char *start;
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

    start = mmap_helper(fsfd, offset, length, sb);
    
    // Get the superblock
    struct superblock sblk;
    getSuperBlock(start, &sblk);
    
    // Check the inodes
    checkInodes(&sblk);
    
    close(fsfd);
}

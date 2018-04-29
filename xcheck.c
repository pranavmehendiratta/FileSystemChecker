#include<stdio.h>
#include<stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h> // mmap is in this header
#include <sys/stat.h> // struct stat is in this header file
#include "fs.h" // File System structs

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

int main(int argc, char *argv[]) {
    if (argc < 2) {
	printf("Usage: xcheck <file_system_image>\n");
	exit(1);
    }
    
    ssize_t s;
    char *addr;
    int fd;
    struct stat sb;
    off_t offset, pa_offset;
    size_t length;

    fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
	handle_error("open");
    }
    
    if (fstat(fd, &sb) == -1) {      /* To obtain file size */
	handle_error("fstat");
    }

    length = sb.st_size;
    offset = 0;

    pa_offset = offset & ~(sysconf(_SC_PAGE_SIZE) - 1);
    /* offset for mmap() must be page aligned */

    addr = mmap_helper(fd, offset, length, sb);
    
    s = write(STDOUT_FILENO, addr + offset - pa_offset, length);
    if (s != length) {
	if (s == -1)
	    handle_error("write");

	fprintf(stderr, "partial write");
    }
}

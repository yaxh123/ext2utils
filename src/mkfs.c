#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "common.h"
#include "mkfs.h"

int main(int argc, char *argv[]) {
    int c;
    int block_size = 1024;
    int blocks_per_group = 8192;
    char *volume_label = NULL;
    
    // Parse command line options
    while ((c = getopt(argc, argv, "b:g:L:")) != -1) {
        switch (c) {
            case 'b':
                block_size = atoi(optarg);
            
                if (block_size != 1024 && block_size != 2048 && block_size != 4096) {
                    fprintf(stderr, "Invalid block size. Must be 1024, 2048, or 4096 bytes.\n");
                    return 1;
                }
                break;
            case 'g':
                blocks_per_group = atoi(optarg);
            
                break;
            case 'L':
                volume_label = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s [-b block-size] [-g blocks-per-group] [-L volume-label] device\n", argv[0]);
                return 1;
        }
    }
    
  
    if (optind >= argc) {
        fprintf(stderr, "Expected device argument\n");
        return 1;
    }
    
    char *device = argv[optind];
    

    int fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }
    

    if (format_ext2_fs(fd, block_size, blocks_per_group, volume_label) != 0) {
        fprintf(stderr, "Failed to format device\n");
        close(fd);
        return 1;
    }
    
    close(fd);
    printf("mke2fs: Filesystem created successfully\n");
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/types.h>
#include <errno.h>
#include "common.h"
#include "mkfs.h"

/* Define ext2 structure constants */
#define EXT2_SUPER_MAGIC    0xEF53
#define EXT2_GOOD_OLD_FIRST_INO 11

/* ext2 superblock structure */
struct ext2_super_block {
    __u32   s_inodes_count;         /* Inodes count */
    __u32   s_blocks_count;         /* Blocks count */
    __u32   s_r_blocks_count;       /* Reserved blocks count */
    __u32   s_free_blocks_count;    /* Free blocks count */
    __u32   s_free_inodes_count;    /* Free inodes count */
    __u32   s_first_data_block;     /* First Data Block */
    __u32   s_log_block_size;       /* Block size */
    __u32   s_log_frag_size;        /* Fragment size */
    __u32   s_blocks_per_group;     /* # Blocks per group */
    __u32   s_frags_per_group;      /* # Fragments per group */
    __u32   s_inodes_per_group;     /* # Inodes per group */
    __u32   s_mtime;                /* Mount time */
    __u32   s_wtime;                /* Write time */
    __u16   s_mnt_count;            /* Mount count */
    __u16   s_max_mnt_count;        /* Maximal mount count */
    __u16   s_magic;                /* Magic signature */
    __u16   s_state;                /* File system state */
    __u16   s_errors;               /* Behaviour when detecting errors */
    __u16   s_minor_rev_level;      /* Minor revision level */
    __u32   s_lastcheck;            /* Last check time */
    __u32   s_checkinterval;        /* Check interval */
    __u32   s_creator_os;           /* OS */
    __u32   s_rev_level;            /* Revision level */
    __u16   s_def_resuid;           /* Default uid for reserved blocks */
    __u16   s_def_resgid;           /* Default gid for reserved blocks */
    /* EXT2_DYNAMIC_REV superblocks */
    __u32   s_first_ino;            /* First non-reserved inode */
    __u16   s_inode_size;           /* Size of inode structure */
    __u16   s_block_group_nr;       /* Block group # of this superblock */
    __u32   s_feature_compat;       /* Compatible feature set */
    __u32   s_feature_incompat;     /* Incompatible feature set */
    __u32   s_feature_ro_compat;    /* Readonly-compatible feature set */
    __u8    s_uuid[16];             /* 128-bit uuid for volume */
    char    s_volume_name[16];      /* Volume name */
    char    s_last_mounted[64];     /* Directory where last mounted */
    __u32   s_algorithm_usage_bitmap; /* For compression */
    /* Performance hints */
    __u8    s_prealloc_blocks;      /* Nr of blocks to preallocate */
    __u8    s_prealloc_dir_blocks;  /* Nr to preallocate for dirs */
    __u16   s_padding1;
    /* Journaling support */
    __u8    s_journal_uuid[16];     /* UUID of journal superblock */
    __u32   s_journal_inum;         /* Inode number of journal file */
    __u32   s_journal_dev;          /* Device number of journal file */
    __u32   s_last_orphan;          /* Start of list of inodes to delete */
    __u32   s_padding[197];         /* Padding to the end of the block */
};

/* ext2 group descriptor */
struct ext2_group_desc {
    __u32   bg_block_bitmap;        /* Blocks bitmap block */
    __u32   bg_inode_bitmap;        /* Inodes bitmap block */
    __u32   bg_inode_table;         /* Inodes table block */
    __u16   bg_free_blocks_count;   /* Free blocks count */
    __u16   bg_free_inodes_count;   /* Free inodes count */
    __u16   bg_used_dirs_count;     /* Directories count */
    __u16   bg_pad;
    __u32   bg_reserved[3];
};

/* ext2 inode structure */
struct ext2_inode {
    __u16   i_mode;         /* File mode */
    __u16   i_uid;          /* Low 16 bits of Owner Uid */
    __u32   i_size;         /* Size in bytes */
    __u32   i_atime;        /* Access time */
    __u32   i_ctime;        /* Creation time */
    __u32   i_mtime;        /* Modification time */
    __u32   i_dtime;        /* Deletion Time */
    __u16   i_gid;          /* Low 16 bits of Group Id */
    __u16   i_links_count;  /* Links count */
    __u32   i_blocks;       /* Blocks count */
    __u32   i_flags;        /* File flags */
    __u32   i_reserved1;
    __u32   i_block[15];    /* Pointers to blocks */
    __u32   i_generation;   /* File version (for NFS) */
    __u32   i_file_acl;     /* File ACL */
    __u32   i_dir_acl;      /* Directory ACL */
    __u32   i_faddr;        /* Fragment address */
    __u8    i_frag;         /* Fragment number */
    __u8    i_fsize;        /* Fragment size */
    __u16   i_pad1;
    __u16   i_uid_high;     /* High 16 bits of Owner Uid */
    __u16   i_gid_high;     /* High 16 bits of Group Id */
    __u32   i_reserved2;
};

/* Mode bits */
#define S_IFDIR  0x4000      /* Directory */
#define S_IFREG  0x8000      /* Regular file */
#define S_IFLNK  0xA000      /* Symbolic link */

/* Directory entry structure */
struct ext2_dir_entry {
    __u32   inode;          /* Inode number */
    __u16   rec_len;        /* Directory entry length */
    __u8    name_len;       /* Name length */
    __u8    file_type;
    char    name[];         /* File name */
};

/* Write a block to disk */
static int write_block(int fd, char *buffer, int block_size, int block_no) {
    off_t offset = (off_t)block_no * block_size;
    if (lseek(fd, offset, SEEK_SET) != offset) {
        perror("lseek");
        return -1;
    }
    if (write(fd, buffer, block_size) != block_size) {
        perror("write");
        return -1;
    }
    return 0;
}

/* Create a new ext2 filesystem */
int format_ext2_fs(int fd, int block_size, int blocks_per_group, char *volume_label) {
    struct ext2_super_block sb;
    struct ext2_group_desc group_desc;
    struct ext2_inode inode;
    struct stat st;
    char *buffer;
    int i, inodes_per_group, block_groups, first_data_block;
    int inode_table_blocks, inode_size;
    time_t current_time;
    
    /* Get device size */
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        return -1;
    }
    
    /* Allocate block buffer */
    buffer = calloc(1, block_size);
    if (!buffer) {
        perror("calloc");
        return -1;
    }
    
    /* Calculate filesystem parameters */
    current_time = time(NULL);
    
    /* For block_size, we need log2(block_size) - 10 */
    int log_block_size;
    switch(block_size) {
        case 1024: log_block_size = 0; break;
        case 2048: log_block_size = 1; break;
        case 4096: log_block_size = 2; break;
        default:
            fprintf(stderr, "Invalid block size: %d\n", block_size);
            free(buffer);
            return -1;
    }
    
    /* Block 0 is the boot sector for 1024 byte blocks */
    /* For larger blocks, block 0 is the superblock */
    first_data_block = (block_size == 1024) ? 1 : 0;
    
    /* Set reasonable defaults for inodes per group */
    inodes_per_group = block_size * 8; /* One inode per byte */
    
    /* Calculate the inode table size */
    inode_size = 128; /* Standard inode size */
    inode_table_blocks = (inodes_per_group * inode_size) / block_size;
    if ((inodes_per_group * inode_size) % block_size != 0)
        inode_table_blocks++;
    
    /* Calculate number of block groups */
    long long total_blocks = st.st_size / block_size;
    block_groups = total_blocks / blocks_per_group;
    if (total_blocks % blocks_per_group != 0)
        block_groups++;
    
    /* Initialize superblock */
    memset(&sb, 0, sizeof(sb));
    sb.s_inodes_count = inodes_per_group * block_groups;
    sb.s_blocks_count = total_blocks;
    sb.s_r_blocks_count = total_blocks / 20; /* 5% reserved blocks */
    sb.s_free_blocks_count = total_blocks - (block_groups * (2 + inode_table_blocks)) - 1; /* Exclude superblock, group desc, inode table, etc */
    sb.s_free_inodes_count = sb.s_inodes_count - 10; /* First 10 inodes are reserved */
    sb.s_first_data_block = first_data_block;
    sb.s_log_block_size = log_block_size;
    sb.s_log_frag_size = log_block_size;
    sb.s_blocks_per_group = blocks_per_group;
    sb.s_frags_per_group = blocks_per_group;
    sb.s_inodes_per_group = inodes_per_group;
    sb.s_mtime = 0;
    sb.s_wtime = current_time;
    sb.s_mnt_count = 0;
    sb.s_max_mnt_count = 20;
    sb.s_magic = EXT2_SUPER_MAGIC;
    sb.s_state = 1; /* Clean */
    sb.s_errors = 1; /* Continue on errors */
    sb.s_minor_rev_level = 0;
    sb.s_lastcheck = current_time;
    sb.s_checkinterval = 24 * 60 * 60 * 90; /* Check every 90 days */
    sb.s_creator_os = 0; /* Linux */
    sb.s_rev_level = 0; /* Original revision */
    sb.s_def_resuid = 0;
    sb.s_def_resgid = 0;
    sb.s_first_ino = EXT2_GOOD_OLD_FIRST_INO;
    sb.s_inode_size = inode_size;
    
    /* Set volume label if provided */
    if (volume_label) {
        strncpy(sb.s_volume_name, volume_label, sizeof(sb.s_volume_name));
    }
    
    /* Write superblock */
    memset(buffer, 0, block_size);
    memcpy(buffer, &sb, sizeof(sb));
    if (write_block(fd, buffer, block_size, first_data_block) < 0) {
        free(buffer);
        return -1;
    }
    
    /* Now for each block group, write group descriptor, block bitmap, inode bitmap, and inode table */
    for (i = 0; i < block_groups; i++) {
        int group_base = first_data_block + 1 + i * blocks_per_group;
        int block_bitmap = group_base;
        int inode_bitmap = group_base + 1;
        int inode_table = group_base + 2;
        
        /* Initialize group descriptor */
        memset(&group_desc, 0, sizeof(group_desc));
        group_desc.bg_block_bitmap = block_bitmap;
        group_desc.bg_inode_bitmap = inode_bitmap;
        group_desc.bg_inode_table = inode_table;
        group_desc.bg_free_blocks_count = blocks_per_group - (2 + inode_table_blocks);
        group_desc.bg_free_inodes_count = inodes_per_group - (i == 0 ? 10 : 0); /* First 10 inodes reserved in first group */
        group_desc.bg_used_dirs_count = 1; /* Root directory */
        
        /* Write group descriptor */
        memset(buffer, 0, block_size);
        memcpy(buffer, &group_desc, sizeof(group_desc));
        if (write_block(fd, buffer, block_size, first_data_block + 1 + i) < 0) {
            free(buffer);
            return -1;
        }
        
        /* Write block bitmap - first few blocks are used */
        memset(buffer, 0, block_size);
        
        /* Mark superblock, group desc, block bitmap, inode bitmap, and inode table as used */
        for (int j = 0; j < 2 + inode_table_blocks; j++) {
            buffer[j/8] |= (1 << (j % 8));
        }
        
        if (write_block(fd, buffer, block_size, block_bitmap) < 0) {
            free(buffer);
            return -1;
        }
        
        /* Write inode bitmap - first few inodes are used */
        memset(buffer, 0, block_size);
        
        /* Mark first 10 inodes as used in first group */
        if (i == 0) {
            for (int j = 0; j < 10; j++) {
                buffer[j/8] |= (1 << (j % 8));
            }
            /* Mark root directory inode as used (inode 2) */
            buffer[2/8] |= (1 << (2 % 8));
        }
        
        if (write_block(fd, buffer, block_size, inode_bitmap) < 0) {
            free(buffer);
            return -1;
        }
        
        /* Write inode table */
        memset(buffer, 0, block_size);
        
        /* Setup root directory inode */
        if (i == 0) {
            memset(&inode, 0, sizeof(inode));
            inode.i_mode = S_IFDIR | 0755;
            inode.i_uid = 0;
            inode.i_size = block_size;
            inode.i_atime = current_time;
            inode.i_ctime = current_time;
            inode.i_mtime = current_time;
            inode.i_dtime = 0;
            inode.i_gid = 0;
            inode.i_links_count = 2; /* . and .. */
            inode.i_blocks = block_size / 512; /* Count in 512-byte sectors */
            inode.i_block[0] = inode_table + inode_table_blocks; /* First data block after inode table */
            
            /* Write root directory inode */
            memcpy(buffer + (2 * inode_size), &inode, sizeof(inode));
            
            /* Write inode table block */
            if (write_block(fd, buffer, block_size, inode_table) < 0) {
                free(buffer);
                return -1;
            }
            
            /* Now create root directory entries */
            memset(buffer, 0, block_size);
            struct ext2_dir_entry *dir_entry = (struct ext2_dir_entry *)buffer;
            
            /* "." entry */
            dir_entry->inode = 2;
            dir_entry->rec_len = 12;
            dir_entry->name_len = 1;
            dir_entry->file_type = 2; /* Directory */
            dir_entry->name[0] = '.';
            
            /* ".." entry */
            dir_entry = (struct ext2_dir_entry *)(buffer + dir_entry->rec_len);
            dir_entry->inode = 2; 
            dir_entry->rec_len = block_size - 12;
            dir_entry->name_len = 2;
            dir_entry->file_type = 2; 
            dir_entry->name[0] = '.';
            dir_entry->name[1] = '.';
            
            
            if (write_block(fd, buffer, block_size, inode_table + inode_table_blocks) < 0) {
                free(buffer);
                return -1;
            }
        } else {
           
            for (int j = 0; j < inode_table_blocks; j++) {
                if (write_block(fd, buffer, block_size, inode_table + j) < 0) {
                    free(buffer);
                    return -1;
                }
            }
        }
    }
    
    free(buffer);
    return 0;
}

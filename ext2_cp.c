#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <memory.h>
#include "ext2.h"

#define SINGLE_INDIRECT 12

unsigned char *disk;

/*
 * This program copies the file on local file system on to the the specified
 * location on the disk. The program works similar to cp.
 */
int main (int argc, char **argv) {
    // Absolute path could be path to directory or path to a file.
    // If target path is a directory, new file with same name generated
    // in such directory.

    // Check valid command line arguments
    if (argc != 4) {
        printf("Usage: ext2_cp <virtual_disk> <source_file> <absolute_path>\n");
        exit(1);
    }

    // Check valid disk
    // Open the disk image file.
    unsigned char *disk = get_disk_loc(argv[1]);

    // Find group descriptor.
    struct ext2_group_desc *gd = get_group_descriptor_loc(disk);

    // Find super block of the disk.
    struct ext2_super_block *sb = get_superblock_loc(disk);

    // Inode table
    struct ext2_inode *i_table = get_inode_table_loc(disk);

    // Inode bitmap and block bitmap
    unsigned char *i_bitmap = get_inode_bitmap_loc(disk);
    unsigned char *b_bitmap = get_block_bitmap_loc(disk);


    // Check valid path on native file system
    // Open source file
    int fd;
    if ((fd = open(argv[2], O_RDONLY)) < 0) {
        fprintf(stderr, "Cannot open source file path.\n");
        exit(1);
    }
    // Get source file size.
    struct stat st;
    fstat(fd, &st);
    int file_size = (int)st.st_size;
    int blocks_needed = file_size / EXT2_BLOCK_SIZE + (file_size % EXT2_BLOCK_SIZE != 0);

    char *name_var = NULL;
    struct ext2_inode *dir_inode = NULL;

    // Check valid target absolute_path
    // If not valid -> ENOENT
    // Get the inode of the given path
    char *parent_path = get_dir_path(argv[3]);
    struct ext2_inode *target_inode = trace_path(argv[3], disk);
    struct ext2_inode *parent_inode = trace_path(parent_path, disk);
    if (target_inode == NULL) { // Target path does not exist
        if (parent_path == NULL) { // Directory of file/dir DNE
            printf("ext2_cp: %s :Invalid path.\n", argv[3]);
            return ENOENT;
        } else {   // File path with file DNE (yet) in a valid directory path.
            name_var = get_file_name(argv[3]);
            dir_inode = parent_inode;
        }
    } else {
        // If source path is a directory
        if (target_inode->i_mode & EXT2_S_IFDIR) {
            name_var = get_file_name(argv[2]);
            dir_inode = target_inode;

            // If such file exist -> EEXIST, no overwrite
        } else { // Source path is a file or a link
            printf("ext2_cp: %s :File exists.\n", argv[3]);
            return EEXIST;
        }
    }

    if (strlen(name_var) > EXT2_NAME_LEN) {
        printf("ext2_cp: Target file with name too long: %s\n", name_var);
        return ENOENT;
    }

    // Check if there is enough inode (require 1)
    if (sb->s_free_inodes_count <= 0) {
        printf("ext2_cp: File system does not have enough free inodes.\n");
        return ENOSPC;
    }

    // Check if there is enough blocks for Data
    if ((blocks_needed <= 12 && sb->s_free_blocks_count < blocks_needed) ||
        (blocks_needed > 12 && sb->s_free_blocks_count < blocks_needed + 1)) {
        printf("ext2_cp: File system does not have enough free blocks.\n");
        return ENOSPC;
    } // If indirected block needed, one more indirect block is required to store pointers

    // Require a free inode
    int i_num = get_free_inode(disk, i_bitmap);
    struct ext2_inode *tar_inode = &(i_table[i_num - 1]);

    // Init the inode
    tar_inode->i_mode = EXT2_S_IFREG;
    tar_inode->i_size = (unsigned int) file_size;
    tar_inode->i_links_count = 1;
    tar_inode->i_blocks = 0;

    // Write into target file (data blocks
    // Requires enough blocks
    char buf[EXT2_BLOCK_SIZE];
    int block_index = 0;
    int indirect_num = -1;

    while (read(fd, buf, EXT2_BLOCK_SIZE) != 0) {
        int b_num;
        if (block_index < SINGLE_INDIRECT) {
            b_num = get_free_block(disk, b_bitmap);
            tar_inode->i_block[block_index] = (unsigned int) b_num;
        } else {
            if (block_index == SINGLE_INDIRECT) { // First time access indirect blocks
                indirect_num = get_free_block(disk, b_bitmap);
                tar_inode->i_block[SINGLE_INDIRECT] = (unsigned int)indirect_num;
                tar_inode->i_blocks += 2;
            }
            b_num = get_free_block(disk, b_bitmap);
            unsigned int *indirect_block = (unsigned int *) (disk + indirect_num * EXT2_BLOCK_SIZE);
            indirect_block[block_index - SINGLE_INDIRECT] = (unsigned int)b_num;
        }
        unsigned char *block = disk + b_num * EXT2_BLOCK_SIZE;
        // printf("%c\n", source_path[block_index * EXT2_BLOCK_SIZE]);
        strncpy((char *)block, buf, EXT2_BLOCK_SIZE);
        tar_inode->i_blocks += 2;
        block_index++;
    }

    // Create a new entry in directory
    printf("%d, %s\n", i_num, name_var);
    if (add_new_entry(disk, dir_inode, (unsigned int)i_num, name_var, 'f') == -1) {
        printf("ext2_cp: Fail to add new directory entry in directory: %s\n", argv[3]);
        exit(0);
    }
    return 0;
}

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"
#include "helper.h"

#define SINGLE_INDIRECT 12

/*
 * Return the disk location.
 */
unsigned char *get_disk_loc(char *disk_name) {
    int fd = open(disk_name, O_RDWR);

    // Map disk image file into memory
    unsigned char *disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    return disk;
}

/*
 * Return the super block location.
 */
struct ext2_super_block *get_superblock_loc(unsigned char *disk) {
    return (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
}

/*
 * Return the group descriptor location.
 */
struct ext2_group_desc *get_group_descriptor_loc(unsigned char *disk) {
    return (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
}

/*
 * Return the block bitmap (16*8 bits) location.
 */
unsigned char *get_block_bitmap_loc(unsigned char *disk, struct ext2_group_desc *gd) {
    return disk + EXT2_BLOCK_SIZE * (gd->bg_block_bitmap);
}

/*
 * Return the inode bitmap (4*8 bits) location.
 */
unsigned char *get_inode_bitmap_loc(unsigned char *disk, struct ext2_group_desc *gd) {
    return disk + EXT2_BLOCK_SIZE * (gd->bg_inode_bitmap);
}

/*
 * Return the inode table location.
 */
struct ext2_inode  *get_inode_table_loc(unsigned char *disk, struct ext2_group_desc *gd) {
    return (struct ext2_inode *)(disk + EXT2_BLOCK_SIZE * gd->bg_inode_table);
}

/*
 * Return the indirect block location.
 */
unsigned int *get_indirect_block_loc(unsigned char *disk, struct ext2_inode  *inode) {
    return (unsigned int *) (disk + EXT2_BLOCK_SIZE * inode->i_block[SINGLE_INDIRECT]);
}

/*
 * Return the directory location.
 */
struct ext2_dir_entry_2 *get_dir_entry(unsigned char *disk, int block_num) {
    return (struct ext2_dir_entry_2 *) (disk + EXT2_BLOCK_SIZE * block_num);
}

/*
 * Return the inode of the root directory.
 */
struct ext2_inode *get_root_inode(struct ext2_inode  *inode_table) {
    return &(inode_table[EXT2_ROOT_INO - 1]);
}

/*
 * Return the file name of the given valid path.
 */
char *get_file_name(char *path) {
    char *filter = "/";
    char *file_name = malloc(sizeof(char) * (strlen(path) + 1));;

    char *full_path = malloc(sizeof(char) * (strlen(path) + 1));
    strncpy(full_path, path, strlen(path));

    char *token = strtok(full_path, filter);
    while (token != NULL) {
        strncpy(file_name, token, strlen(token));
        token = strtok(NULL, filter);
    }

    return file_name;
}

/*
 * Trace the given path. Return the inode of the given path.
 */
struct ext2_inode *trace_path(char *path, unsigned char *disk) {
    char *filter = "/";
    unsigned int block_number = 0;
    struct ext2_dir_entry_2 *dir_entry = NULL;
    int size = 0;

    struct ext2_super_block *sb = get_superblock_loc(disk);
    struct ext2_group_desc *gd = get_group_descriptor_loc(disk);
    struct ext2_inode  *inode_table = get_inode_table_loc(disk, gd);
    unsigned char *inode_bitmap = get_inode_bitmap_loc(disk, gd);

    // Get the inode of the root
    struct ext2_inode *current_inode = get_root_inode(inode_table);

    // Get the copy of the path
    char *full_path = malloc(sizeof(char) * (strlen(path) + 1));
    strncpy(full_path, path, strlen(path));

    char *token = strtok(full_path, filter);
    while (token != NULL) {
        for (int i = 0; i < SINGLE_INDIRECT; i++) { // loop through the direct block pointer
            // current_inode is a directory and there is data in its block
            if ((current_inode->i_mode & EXT2_S_IFDIR) && (current_inode->i_block[i])) {

            }

            // Get the entries of the current directory, if exist
            struct ext2_dir_entry_2 *entry = &(current_inode[i]);

        }

        token = strtok(NULL, filter);
    }

    return NULL;

}

/*
 * Return the inode of the directory with particular name
 * in a given parent directory
 */
struct ext2_inode *get_entry_with_name(unsigned char *disk, char *name, struct ext2_inode *parent) {
    // get the inode of the parent directory
    // loop the i_blocks[15] of the parent directory inode:
    // i_block[i] 1. has data 2. while loop the block to get the content
    struct ext2_group_desc *gd = get_group_descriptor_loc(disk);
    struct ext2_inode  *inode_table = get_inode_table_loc(disk, gd);
    struct ext2_inode *parent_dir_inode = &(inode_table[parent->inode]);
    struct ext2_inode *target_entry = NULL;

    // case1: direct block pointer
    for (int i = 0; i < SINGLE_INDIRECT; i++) {

        if (parent_dir_inode->i_block[i]) { // check block number != 0
            struct ext2_dir_entry_2 *dir = get_dir_entry(disk, parent_dir_inode, i);

            int curr_pos = 0;

            while (curr_pos < EXT2_BLOCK_SIZE) {
//                printf("Inode: %u ", dir->inode);
//                printf("rec_len: %hu ", dir->rec_len);
//                printf("name_len: %u ", dir->name_len);
                char *current_name = malloc(sizeof(char) * dir->name_len + 1);
                for (int u = 0; u < dir->name_len; u++) {
                    current_name[u] = dir->name[u];
                }
                current_name[dir->name_len] = '\0';

                // check whether this name is wanted
                if (strcmp(current_name, name) == 0) {
                    target_entry = dir->
                }

                printf("name=%s\n", print_name);
                free(print_name);
                /* Moving to the next directory */
                curr_pos = curr_pos + dir->rec_len;
                dir = (void*) dir + dir->rec_len;


                printf("curr_pos: %d\n", curr_pos);
                //printf("dir->inode: %d\n", dir->inode);
                //printf("size of struct: %lu\n", sizeof(struct ext2_dir_entry_2));
                //printf("size of int: %lu, sizeof short: %lu, size of char: %lu", sizeof(unsigned int), sizeof(unsigned short), sizeof(unsigned char));
            }
        }
    }

    // case2: indirect block pointer
}

/*
 * Print all the entries of a given directory.
 */
void print_entries(unsigned char *disk, struct ext2_inode *directory, char *flag) {
    // Print all the entries of a given directory in direct blocks.
    for (int i = 0; i < SINGLE_INDIRECT; i++) {
        if (directory->i_block[i]) {
            print_one_block_entries(get_dir_entry(disk, directory->i_block[i]), flag);
        }
    }

    // Print all the entries of given directory in indirect blocks.
    if (directory->i_block[SINGLE_INDIRECT]) {
        unsigned int *indirect = get_indirect_block_loc(disk, directory);

        for (int i = 0; i < EXT2_BLOCK_SIZE / sizeof(unsigned int); i++) {
            if (indirect[i]) {
                print_one_block_entries(get_dir_entry(disk, indirect[i]), flag);
            }
        }
    }
}

/*
 * Print the entries in one block
 */
void print_one_block_entries(struct ext2_dir_entry_2 *dir, char *flag) {
    int curr_pos = 0; // used to keep track of the dir entry in each block
    while (curr_pos < EXT2_BLOCK_SIZE) {
        char *entry_name = malloc(sizeof(char) * dir->name_len + 1);

        for (int u = 0; u < dir->name_len; u++) {
            entry_name[u] = dir->name[u];
        }
        entry_name[dir->name_len] = '\0';

        if (strcmp(flag, "-a") == 0) {
            printf("%s\n", entry_name);
        } else { // Refrain from printing the . and ..
            if (strcmp(entry_name, ".") != 0 || strcmp(entry_name, "..") != 0) {
                printf("%s\n", entry_name);
            }
        }

        free(entry_name);

        /* Moving to the next directory */
        curr_pos = curr_pos + dir->rec_len;
        dir = (void*) dir + dir->rec_len;
    }
}
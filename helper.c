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
struct ext2_inode *get_inode_table_loc(unsigned char *disk, struct ext2_group_desc *gd) {
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
 * Return the path except the final path name. With assumption that the path is
 * a valid path and it is a path of a file.
 *
 * If path is /a/bb/ccc/ this function will return /a/bb/ccc then will still
 * gives an invalid path.
 * If path is /a/bb/ccc function will give /a/bb
 */
char *get_dir_path(char *path) {
  char *file_name = NULL;
  char *parent = NULL;
  file_name = strrchr(path, '/');
  parent = strndup(path, strlen(path) - strlen(file_name) + 1);
  return parent;
}

/*
 * Return the first inode number that is free.
 */
int get_free_inode(struct ext2_super_block *sb, unsigned char *inode_bitmap) {
  // Loop over the inodes that are not reserved
  for (int i = EXT2_GOOD_OLD_FIRST_INO; i < sb->s_inodes_count; i++) {
    int bitmap_byte = i / 8;
    int bit_order = i % 8;
    if ((inode_bitmap[bitmap_byte] >> bit_order) ^ 1) {
      // Such bit is 0, which is a free inode
      inode_bitmap[bitmap_byte] |= 1 << (7 - bit_order);
      return i + 1;
    }
  }
}

/*
 * Return the first block number that is free.
 */
int get_free_block(struct ext2_super_block *sb, unsigned char *block_bitmap) {
  // Loop over the inodes that are not reserved
  for (int i = 0; i < sb->s_blocks_count; i++) {
    int bitmap_byte = i / 8;
    int bit_order = i % 8;
    if ((block_bitmap[bitmap_byte] >> bit_order) ^ 1) {
      // Such bit is 0, which is a free block
      block_bitmap[bitmap_byte] |= 1 << (7 - bit_order);
      return i;
    }
  }
}

/*
 * Trace the given path. Return the inode of the given path.
 */
struct ext2_inode *trace_path(char *path, unsigned char *disk) {
    char *filter = "/";

    struct ext2_group_desc *gd = get_group_descriptor_loc(disk);
    struct ext2_inode  *inode_table = get_inode_table_loc(disk, gd);

    // Get the inode of the root
    struct ext2_inode *current_inode = get_root_inode(inode_table);

    // Get the copy of the path
    char *full_path = malloc(sizeof(char) * (strlen(path) + 1));
    strncpy(full_path, path, strlen(path) + 1);

    char *token = strtok(full_path, filter);
    while (token != NULL) {
        if (current_inode->i_mode & EXT2_S_IFDIR) {
            current_inode = get_entry_with_name(disk, token, current_inode);
        }

        token = strtok(NULL, filter);

        // handle case: in the middle of the path is not a directory
        if (token != NULL && !(current_inode->i_mode & EXT2_S_IFDIR)) {
            return NULL;
        }
    }

    // handle the case like: /a/bb/ccc/, ccc need to be a directory
    if ((full_path[strlen(full_path) - 1] == '/')
        && !(current_inode->i_mode & EXT2_S_IFDIR)) {
        return NULL;
    }

    return current_inode;
}

/*
 * Return the inode of the directory/file/link with a particular name if it is in the given
 * parent directory, otherwise, return NULL.
 */
struct ext2_inode *get_entry_with_name(unsigned char *disk, char *name, struct ext2_inode *parent) {
    struct ext2_inode *target = NULL;
    // Search through the direct blocks
    for (int i = 0; i < SINGLE_INDIRECT; i++) {
        if (parent->i_block[i]) {
            target = get_entry_in_block(disk, name, parent->i_block[i]);
        }
    }

    // Search through the indirect blocks if not find the target inode
    if (target == NULL && parent->i_block[SINGLE_INDIRECT]) {
        target = get_entry_in_block(disk, name, parent->i_block[SINGLE_INDIRECT]);
    }

    return target;
}

/*
 * Return the inode of a directory/file/link with a particular name if it is in a block,
 * otherwise return NULL.
 */
struct ext2_inode *get_entry_in_block(unsigned char *disk, char *name, int block_num) {
    struct ext2_dir_entry_2 *dir = get_dir_entry(disk, block_num);
    struct ext2_inode *target = NULL;

    struct ext2_group_desc *gd = get_group_descriptor_loc(disk);
    struct ext2_inode  *inode_table = get_inode_table_loc(disk, gd);

    int curr_pos = 0; // used to keep track of the dir entry in each block
    while (curr_pos < EXT2_BLOCK_SIZE) {
        char *entry_name = malloc(sizeof(char) * dir->name_len + 1);

        for (int u = 0; u < dir->name_len; u++) {
            entry_name[u] = dir->name[u];
        }
        entry_name[dir->name_len] = '\0';

        if (strcmp(entry_name, name) == 0) {
            target = &inode_table[dir->inode];
        }

        free(entry_name);

        /* Moving to the next directory */
        curr_pos = curr_pos + dir->rec_len;
        dir = (void*) dir + dir->rec_len;
    }

    return target;
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

/*
 * Create a new directory entry for the new enter file, link or directory.
 */
struct ext2_dir_entry_2 *setup_entry(unsigned int new_inode, char *f_name, char type) {
    struct ext2_dir_entry_2 *new_entry = malloc(sizeof(struct ext2_dir_entry_2));
    new_entry->inode = new_inode;
    new_entry->name_len = (unsigned char) strlen(f_name);
    memcpy(new_entry->name, f_name, new_entry->name_len);
    new_entry->rec_len = sizeof(struct ext2_dir_entry_2 *) + sizeof(char) * new_entry->name_len;

    if (type == 'd') {
        new_entry->file_type = EXT2_FT_DIR;
    } else if (type == 'f') {
        new_entry->file_type = EXT2_FT_REG_FILE;
    } else if (type == 'l') {
        new_entry->file_type = EXT2_FT_SYMLINK;
    } else {
        new_entry->file_type = EXT2_FT_UNKNOWN;
    }

    // Make entry length power of 4
    while (new_entry->rec_len % 4 != 0) {
        new_entry->rec_len ++;
    }

    return new_entry;
}

/*
 * Add new entry into the directory.
 */
int add_new_entry(unsigned char *disk, struct ext2_inode *dir_inode, struct ext2_dir_entry_2 *new_entry) {
    // Recalls that there are 12 direct blocks.
    for (int k = 0; k < 12; k++) {
        // If the block exists i.e. not 0.
        int block_num = dir_inode->i_block[k];
        struct ext2_dir_entry_2 *dir = get_dir_entry(disk, block_num);
        int curr_pos = 0;

        /* Total size of the directories in a block cannot exceed a block size */
        while (curr_pos < EXT2_BLOCK_SIZE) {
            if ((curr_pos + dir->rec_len) == EXT2_BLOCK_SIZE) { // last block
                int true_len = sizeof(struct ext2_dir_entry_2 *) + sizeof(char) * new_entry->name_len;
                if ((dir->rec_len - true_len) >= new_entry->rec_len) {
                    dir->rec_len = (unsigned short) true_len;
                    dir = (void *) dir + dir->rec_len;

                    dir->inode = new_entry->inode;
                    dir->name_len = new_entry->name_len;
                    dir->file_type = new_entry->file_type;
                    dir->rec_len = (unsigned short) (EXT2_BLOCK_SIZE - curr_pos);
                    return 0;
                }
            }
            // Moving to the next directory
            curr_pos = curr_pos + dir->rec_len;
            dir = (void *) dir + dir->rec_len;
        }
    }

    return 0;
}

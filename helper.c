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
    strncpy(full_path, path, strlen(path) + 1);

    char *token = strtok(full_path, filter);
    while (token != NULL) {
        strncpy(file_name, token, strlen(token) + 1);
        token = strtok(NULL, filter);
    }

    return file_name;
}

/*
 * Return the path except the final path name. With assumption that the path is
 * a valid path and it is a path of a file.
 *
 * If path is /a/bb/ccc/ this function will return /a/bb/ccc/ it will still
 * an invalid path.
 * If path is /a/bb/ccc function will give /a/bb/
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
    // Loop over the inodes that are not reserved, index i
    for (int i = EXT2_GOOD_OLD_FIRST_INO; i < sb->s_inodes_count; i++) {
        int bitmap_byte = i / 8;
        int bit_order = i % 8;
        if ((inode_bitmap[bitmap_byte] >> bit_order) ^ 1) {
          // Such bit is 0, which is a free inode
          if ((i + 1) % 8 == 0) {
            inode_bitmap[((i + 1) / 8) - 1] |= 1 << bit_order;
          } else {
            inode_bitmap[(i + 1) / 8] |= 1 << bit_order;
          }
          return i + 1;
        }
    }

    return -1;
}

/*
 * Return the first block number that is free.
 */
int get_free_block(struct ext2_super_block *sb, unsigned char *block_bitmap) {
    // Loop over the inodes that are not reserved
    for (int i = 1; i < sb->s_blocks_count; i++) {
        int bitmap_byte = i / 8;
        int bit_order = i % 8;
        if ((block_bitmap[bitmap_byte] >> bit_order) ^ 1) {
            // Such bit is 0, which is a free block
            if ((i + 1) % 8 == 0) {
              block_bitmap[((i + 1) / 8) - 1] |= 1 << bit_order;
            } else {
              block_bitmap[(i + 1) / 8] |= 1 << bit_order;
            }
            return i;
        }
    }

    return -1;
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
        if (current_inode != NULL && (current_inode->i_mode & EXT2_S_IFDIR)) {
            current_inode = get_entry_with_name(disk, token, current_inode);
        }

        token = strtok(NULL, filter);

        // handle case: in the middle of the path is not a directory
        if (current_inode != NULL && token != NULL
            && !(current_inode->i_mode & EXT2_S_IFDIR)) {
            return NULL;
        }
    }

    // handle the case like: /a/bb/ccc/, ccc need to be a directory
    if (current_inode != NULL
        && (path[strlen(path) - 1] == '/')
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
        unsigned int *indirect = get_indirect_block_loc(disk, parent);

        for (int i = 0; i < EXT2_BLOCK_SIZE / sizeof(unsigned int); i++) {
            if (indirect[i]) {
                target = get_entry_in_block(disk, name, indirect[i]);
            }
        }
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
            target = &(inode_table[dir->inode - 1]);
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

        // Print all the entries of given directory in indirect blocks.
        if (i == SINGLE_INDIRECT) {
            unsigned int *indirect = get_indirect_block_loc(disk, directory);

            for (int j = 0; j < EXT2_BLOCK_SIZE / sizeof(unsigned int); j++) {
                if (indirect[j]) {
                    print_one_block_entries(get_dir_entry(disk, indirect[j]), flag);
                }
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

        if (flag == NULL) { // Refrain from printing the . and ..
            if (strcmp(entry_name, ".") != 0 && strcmp(entry_name, "..") != 0) {
                printf("%s\n", entry_name);
            }
        } else if (strcmp(flag, "-a") == 0) {
            printf("%s\n", entry_name);
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
    struct ext2_super_block *sb = get_superblock_loc(disk);
    struct ext2_group_desc *gd = get_group_descriptor_loc(disk);
    unsigned char *block_bitmap = get_block_bitmap_loc(disk, gd);

    // Recalls that there are 12 direct blocks.
    for (int k = 0; k < 12; k++) {
        // If the block exists i.e. not 0.
        int block_num = dir_inode->i_block[k];
        if (block_num == 0) { // What is init number for block?
          int b_num = get_free_block(sb, block_bitmap);
          if (b_num == -1) {
            return -1;
          }
            struct ext2_dir_entry_2 *block = get_dir_entry(disk, b_num);
            struct ext2_dir_entry_2 *dir = &(block[0]);
            dir->inode = new_entry->inode;
            dir->name_len = new_entry->name_len;
            dir->file_type = new_entry->file_type;
            dir->rec_len = EXT2_BLOCK_SIZE;
            return 0;
        }

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

    return -1;
}

/*
 * Zero block [inode / block] bitmap of given block number.
 */
void zero_bitmap(unsigned char *block, int block_num) {
    int bitmap_byte = block_num/ 8;
    int bit_order = block_num % 8;
    if (bit_order != 0) {
        block[bitmap_byte] &= ~(1 << (bit_order - 1));
    } else { // bit_order == 0
        block[bitmap_byte - 1] &= ~(1 << (8 - 1));
    }
}


/*
 * Clear all the entries in the blocks of given inode and
 * zero the block bitmap of given inode.
 */
void clear_block_bitmap(unsigned char *disk, struct ext2_inode *remove, char *path) {
    struct ext2_group_desc *gd = get_group_descriptor_loc(disk);
    unsigned char *block_bitmap = get_block_bitmap_loc(disk, gd);

    // zero through the direct blocks
    for (int i = 0; i < SINGLE_INDIRECT; i++) {
        if (remove->i_block[i]) { // check has data, not points to 0
            clear_one_block(disk, remove->i_block[i]);
            zero_bitmap(block_bitmap, remove->i_block[i]);
            remove->i_block[i] = 0; // points to "boot" block

        }

        // zero through the single indirect block's blocks
        if (i == SINGLE_INDIRECT) {
            unsigned int *indirect = get_indirect_block_loc(disk, remove);

            for (int j = 0; j < EXT2_BLOCK_SIZE / sizeof(unsigned int); j++) {
                if (indirect[j]) {
                    clear_one_block(disk, indirect[j]);
                    zero_bitmap(block_bitmap, indirect[j]);
                    indirect[j] = 0; // each indirect block points to "boot" block
                }
            }

            // zero the single indirect block
            clear_one_block(disk, remove->i_block[SINGLE_INDIRECT]);
            zero_bitmap(block_bitmap, remove->i_block[SINGLE_INDIRECT]);
            remove->i_block[SINGLE_INDIRECT] = 0; // points to "boot" block
        }
    }

    remove_name(disk, path);
}

/*
 * Clear all the entries in one block.
 */
void clear_one_block(unsigned char *disk, int block_num) {
    struct ext2_super_block *sb = get_superblock_loc(disk);
    struct ext2_group_desc *gd = get_group_descriptor_loc(disk);

    struct ext2_dir_entry_2 *dir = get_dir_entry(disk, block_num);
    struct ext2_dir_entry_2 *pre_dir = NULL;
    int curr_pos = 0; // used to keep track of the dir entry in each block

    while (curr_pos < EXT2_BLOCK_SIZE) {
        pre_dir = dir;

        /* Moving to the next directory */
        curr_pos = curr_pos + dir->rec_len;
        dir = (void*) dir + dir->rec_len;

        pre_dir->rec_len = 0;
        for (int i = 0; i < pre_dir->name_len; i++) {
            pre_dir->name[i] = '\0';
        }
        pre_dir->name_len = 0;
    }

    sb->s_free_blocks_count++;
    gd->bg_free_blocks_count++;
}

/*
 * Zero the given inode from the inode bitmap.
 */
void clear_inode_bitmap(unsigned char *disk, struct ext2_inode *remove) {
    struct ext2_super_block *sb = get_superblock_loc(disk);
    struct ext2_group_desc *gd = get_group_descriptor_loc(disk);
    unsigned char *inode_bitmap = get_inode_bitmap_loc(disk, gd);

    int inode_number = get_inode_num(disk, remove);
    zero_bitmap(inode_bitmap, inode_number);

    sb->s_free_inodes_count++;
    gd->bg_free_inodes_count++;
}

/*
 * Get inode number of given inode if exist, otherwise return 0.
 */
int get_inode_num(unsigned char *disk, struct ext2_inode *target) {
    struct ext2_super_block *sb = get_superblock_loc(disk);
    struct ext2_group_desc *gd = get_group_descriptor_loc(disk);
    struct ext2_inode  *inode_table = get_inode_table_loc(disk, gd);

    int inode_num = 0;

    for (int i = 0; i < sb->s_inodes_count; i++) {
        if (inode_table[i].i_size && (&(inode_table[i]) == target)) { // there is data
            inode_num = i + 1;
        }
    }

    return inode_num;
}

/*
 * Remove the file's name of the given path. Assume the given path
 * is a path to a valid file (or hard link) / link.
 */
void remove_name(unsigned char *disk, char *path) {
    char *file_name = get_file_name(path);
    char *parent_path = get_dir_path(path);
    struct ext2_inode *parent_dir = trace_path(parent_path, disk);
    int remove = 0;

    // check through the direct blocks
    for (int i = 0; i < SINGLE_INDIRECT + 1; i++) {
        if (parent_dir->i_block[i]) { // check has data, not points to 0
            remove = remove_name_in_block(disk, file_name, parent_dir->i_block[i]);
        }

        // check through the single indirect block's blocks
        if (i == SINGLE_INDIRECT && (remove == 0)) {
            unsigned int *indirect = get_indirect_block_loc(disk, parent_dir);

            for (int j = 0; j < EXT2_BLOCK_SIZE / sizeof(unsigned int); j++) {
                if (indirect[j]) {
                    remove = remove_name_in_block(disk, file_name, indirect[j]);
                }
            }
        }
    }
}

/*
 * Return 1 if successfully remove the directory entry with given name,
 * otherwise, return 0.
 */
int remove_name_in_block(unsigned char *disk, char *file_name, int block_num) {
    struct ext2_dir_entry_2 *dir = get_dir_entry(disk, block_num);

    int curr_pos = 0; // used to keep track of the dir entry in each block
    struct ext2_dir_entry_2 *prev_dir = NULL;

    while (curr_pos < EXT2_BLOCK_SIZE) {
        char *entry_name = malloc(sizeof(char) * dir->name_len + 1);

        for (int u = 0; u < dir->name_len; u++) {
            entry_name[u] = dir->name[u];
        }
        entry_name[dir->name_len] = '\0';

        if (strcmp(entry_name, file_name) == 0) { // find the dir entry with given name
           for (int i = 0; i < dir->name_len; i++) {
                dir->name[i] = '\0';
           }

            if (prev_dir != NULL) { // need to update of the rec_len of the previous dir entry
                prev_dir->rec_len += dir->rec_len;
            }
            dir->rec_len = 0;
            free(entry_name);
            return 1;
        }

        free(entry_name);

        /* Moving to the next directory */
        curr_pos = curr_pos + dir->rec_len;
        prev_dir = dir;
        dir = (void*) dir + dir->rec_len;
    }

    for (int i = 0; i < dir->name_len; i++) {
        dir->name[i] = '\0';
    }
    dir->rec_len = 0;

    return 0;
}

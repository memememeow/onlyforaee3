#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include "ext2.h"
// #include "helper.h"

#define SINGLE_INDIRECT 12

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
    // Find group descriptor.
    struct ext2_group_desc *gd = get_group_descriptor_loc(disk);

    // Find super block of the disk.
    struct ext2_super_block *sb = get_superblock_loc(disk);

    // unsigned char *i_bitmap = get_inode_bitmap_loc(disk, gd);
    unsigned char *b_bitmap = get_block_bitmap_loc(disk, gd);
    for (int k = 0; k < 12; k++) {
        // If the block exists i.e. not 0.
        int block_num = dir_inode->i_block[k];
        if (block_num == 0) { // What is init number for block?
          int b_num = get_free_block(sb, b_bitmap);
          if (b_num == -1) {
            return -1;
          }
          struct ext2_dir_entry_2 *block = (struct ext2_dir_entry_2 *)(disk + b_num * EXT2_BLOCK_SIZE);
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
 * This program created a linked file from first specific file to second absolute
 * path.
 */
int main (int argc, char **argv) {

  // Check valid command line arguments
  if (argc != 4 && argc != 5) {
    printf("Usage: ext2_ls <virtual_disk> <source_file> <target_file> <-s>\n");
    exit(1);
  } else if (argc == 5 && strcmp(argv[4], "-s") != 0) {
    printf("Usage: ext2_ls <virtual_disk> <source_file> <target_file> <-s>\n");
    exit(1);
  }

  // Check valid disk
  // Open the disk image file.
  unsigned char *disk = get_disk_loc(argv[1]);

  // Find group descriptor.
  struct ext2_group_desc *gd = get_group_descriptor_loc(disk);

  // Find super block of the disk.
  struct ext2_super_block *sb = get_superblock_loc(disk);

  unsigned char *i_bitmap = get_inode_bitmap_loc(disk, gd);
  unsigned char *b_bitmap = get_block_bitmap_loc(disk, gd);

  // Inode table
  struct ext2_inode *i_table = get_inode_table_loc(disk, gd);

  char *dir_path = get_dir_path(argv[3]); // for target file
  struct ext2_inode *source_inode = trace_path(argv[2], disk);
  struct ext2_inode *target_inode = trace_path(argv[3], disk);
  struct ext2_inode *dir_inode = trace_path(dir_path, disk);

  // If source file does not exist -> ENOENT
  if (source_inode == NULL) {
    printf("%s :Invalid path.\n", argv[2]);
    return ENOENT;
  }

  // If target file exist -> EEXIST
  if (target_inode != NULL) {
    if (target_inode->i_mode & EXT2_S_IFDIR) {
      printf("%s :Path provided is a directory.\n", argv[2]);
      return EISDIR;
    }
    printf("%s :File already exist.\n", argv[2]);
    return EEXIST;
  }

  // Directory of target file DNE
  if (dir_inode == NULL) {
    printf("%s :Invalid path.\n", dir_path);
    return ENOENT;
  }

  // If source file path is a directory -> EISDIR
  if (source_inode->i_mode & EXT2_S_IFDIR) {
    return EISDIR;
  }

  int target_inode_num = -1;
  char *target_name = get_file_name(argv[3]);
  unsigned int name_len = (unsigned int)strlen(target_name);
  int blocks_needed = name_len / EXT2_BLOCK_SIZE + (name_len % EXT2_BLOCK_SIZE != 0);

  struct ext2_dir_entry_2 *target_entry;

  if (argc == 5) { // Create soft link
    // Check if we have enough space for path if symbolic link is created
    if ((blocks_needed <= 12 && sb->s_free_blocks_count < blocks_needed) ||
        (blocks_needed > 12 && sb->s_free_blocks_count < blocks_needed + 1)) {
      printf("File system does not have enough free blocks.\n");
      return ENOSPC;
    }

    if ((target_inode_num = get_free_inode(sb, i_bitmap)) == -1) {
      printf("File system does not have enough free inodes.\n");
      return ENOSPC;
    }

    struct ext2_inode *tar_inode = &(i_table[target_inode_num - 1]);

    // Init the inode
    tar_inode->i_mode = EXT2_S_IFLNK;
    tar_inode->i_size = name_len - 1;
    tar_inode->i_links_count = 1;
    tar_inode->i_blocks = 0;

    // Change data in superblock and group desciptor
    sb->s_free_inodes_count--;
    gd->bg_free_inodes_count--;

    // Write path into target file
    int block_index = 0;
    int indirect_b = -1;

    while (block_index * EXT2_BLOCK_SIZE < name_len) {
      if (block_index < SINGLE_INDIRECT) {
        int b_num = get_free_block(sb, b_bitmap);
        unsigned char *block = disk + b_num * EXT2_BLOCK_SIZE;
        memcpy(block, &(target_name[block_index * EXT2_BLOCK_SIZE]), EXT2_BLOCK_SIZE);
        tar_inode->i_blocks += 2;
        block_index++;
      } else {
        if (block_index == 12) { // First time access indirect blocks
          indirect_b = get_free_block(sb, b_bitmap);
          tar_inode->i_blocks += 2;
        }
        int b_num = get_free_block(sb, b_bitmap);
        unsigned int *indirect_block = (unsigned int *) (disk + indirect_b * EXT2_BLOCK_SIZE);
        unsigned char *block = disk + b_num * EXT2_BLOCK_SIZE;
        memcpy(block, &(target_name[block_index * EXT2_BLOCK_SIZE]), EXT2_BLOCK_SIZE);
        indirect_block[block_index - 12] = (unsigned int)b_num;
        tar_inode->i_blocks += 2;
        block_index++;
      }
    }

    sb->s_free_blocks_count -= tar_inode->i_blocks / 2;
    gd->bg_free_blocks_count -= tar_inode->i_blocks / 2;

    target_entry = setup_entry((unsigned int)target_inode_num, target_name, 'l');

  } else {
    target_inode_num = get_inode_num(disk, target_inode);
    struct ext2_inode *tar_inode = &(i_table[target_inode_num - 1]);
    tar_inode->i_links_count++;

    target_entry = setup_entry((unsigned int)target_inode_num, target_name, 'f');
  }

  if (add_new_entry(disk, dir_inode, target_entry) == -1) {
    printf("Fail to add new directory entry in directory: %s\n", dir_path);
    exit(0);
  }

  return 0;
}

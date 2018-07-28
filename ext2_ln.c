#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include "ext2.h"
#include "helper.h"

/*
 * This program created a linked file from first specific file to second absolute
 * path.
 */
int main (int argc, char **argv) {

  // Check valid command line arguments
  if (argc != 4 && argc != 5) {
    printf("Usage: ext2_ls <virtual_disk> <source_file> <target_file> <-s>\n");
    exit(1);
  } else if  (argc == 5 && strcmp(argv[4], "-s") != 0) {
    printf("Usage: ext2_ls <virtual_disk> <source_file> <target_file> <-s>\n");
    exit(1);
  }

  // Check valid disk
  // Open the disk image file.
  struct ext2_super_block *disk = get_disk_loc(argv[1]);

  // Find group descriptor.
  struct ext2_group_desc *gd = get_group_descriptor_loc(disk);

  // Find super block of the disk.
  struct ext2_super_block *sb = get_superblock_loc(disk);

  unsigned char *i_bitmap = get_inode_bitmap_loc(disk, gd);

  // Inode table
  struct ext2_inode *i_table = get_inode_table_loc(disk, gd);

  char *dir_path = get_dir_path(argv[3]);
  struct ext2_inode *source_inode = trace_path(argv[2], disk);
  struct ext2_inode *target_inode = trace_path(argv[3], disk);
  struct ext2_inode *dir_inode = trace_path(dir_path, disk);

  // If source file does not exist -> ENOENT
  if (target_inode == NULL) {
    printf("%s :Invalid path.\n", argv[2]);
    return ENOENT;
  }

  if (dir_inode == NULL) {
    printf("%s :Invalid path.\n", dir_path);
    return ENOENT;
  }

  // If either path is a directory -> EISDIR
  if ((source_inode->i_mode & EXT2_S_IFDIR) ||
    (target_inode->i_mode & EXT2_S_IFDIR)) {
    return EISDIR;
  }

  // If second file exist -> EEXIST
  if (target_inode != NULL) {
    printf("%s :File already exist.\n", argv[2]);
    return EEXIST;
  }

  int target_inode_num;
  char *target_name = get_file_name(argv[3]);
  int name_len = strlen(target_name) + 1;
  int blocks_needed = name_len / EXT2_BLOCK_SIZE +
    (name_len % EXT2EXT2_BLOCK_SIZE != 0);
  if (argc == 5) { // Create soft link
    // Check if we have enough space for path if symbolic link is created
    if ((blocks_needed <= 12 && disk->s_free_blocks_count < blocks_needed) ||
      (blocks_needed > 12 && disk->s_free_blocks_count < blocks_needed + 1)) {
      printf("File system does not have enough free blocks.\n");
      return ENOSPC;
    }

    if (target_inode_num = get_free_inode(sb, i_bitmap) == -1) {
      printf("File system does not have enough free inodes.\n");
      return ENOSPC;
    }

    struct ext2_inode *tar_inode = i_bitmap[i_num - 1];

    // Init the inode
    tar_inode->i_mode = EXT2_S_IFLNK;
    tar_inode->i_size = name_len - 1;
    tar_inode->i_links_count = 1;
    tar_inode->i_blocks = 0;

    // Change data in superblock and group desciptor
    sb -> s_free_inodes_count --;
    gd -> bg_free_inodes_count --;

    // Write path into target file
    int block_index = 0;
    int indirect_b = -1;

    while ((indirect_index + 1) * EXT2_BLOCK_SIZE < name_len) {
      if (iblock_index < SINGLE_INDIRECT) {
        int b_num = get_free_block(sb, b_bitmap);
        unsigned char *block = disk + b_num * EXT2_BLOCK_SIZE;
        memcpy(block, target_name[i * EXT2_BLOCK_SIZE], EXT2_BLOCK_SIZE);
        tar_inode->i_blocks += 2;
        indirect_index ++;
      } else {
        if (indirect_index == 12) { // First time access indirect blocks
          int indirect_b = get_free_block(sb, b_bitmap);
          tar_inode->i_blocks += 2;
        }
        int b_num = get_free_block(sb, b_bitmap);
        unsigned int *indirect_block = (unsigned int *)(disk + indirect_b * EXT2_BLOCK_SIZE);
        int b_index = get_free_block(sb, b_bitmap);
        unsigned char *block = disk + b_index * EXT2_BLOCK_SIZE;
        memcpy(block, target_name[i * EXT2_BLOCK_SIZE], EXT2_BLOCK_SIZE);
        indirect_block[indirect_index - 12] = b_index;
        tar_inode->i_blocks += 2;
        indirect_index ++;
      }
    }
  } else {
    target_inode_num = get_inode_num(target_inode);
    struct ext2_inode *tar_inode = i_bitmap[target_inode_num - 1];
    tar_inode->i_links_count ++;
  }

  struct ext2_dir_entry_2 *target_entry = setup_entry(target_inode_num, target_name, 'l');

  if (add_new_entry(disk, dir_inode, target_entry) == -1) {
    printf("Fail to add new directory entry in directory: %s\n", dir_path);
    exit(0);
  }

  return 0;
}

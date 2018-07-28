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
 * This program copies the file on local file system on to the the specified
 * location on the disk. The program works similar to cp.
 */
int main (int argc, char **argv) {
  // Absolute path could be path to directory or path to a file.
  // If target path is a directory, new file with same name generated
  // in such directory.

  // Check valid command line arguments
  if (argc != 4) {
    printf("Usage: ext2_ls <virtual_disk> <source_file> <absolute_path> <-a>\n");
    exit(1);
  }

  // Check valid disk
  // Open the disk image file.
  struct ext2_super_block *disk = get_disk_loc(argv[1]);

  // Find group descriptor.
  struct ext2_group_desc *gd = get_group_descriptor_loc(disk);

  // Find super block of the disk.
  struct ext2_super_block *sb = get_superblock_loc(disk);

  // Inode table
  struct ext2_inode *i_table = get_inode_table_loc(disk, gd);

  // Inode bitmap and block bitmap
  unsigned char *i_bitmap = get_inode_bitmap_loc(disk, gd);
  unsigned char *b_bitmap = get_block_bitmap_loc(disk, gd);


  // Check valid path on native file system
  // Open source file
  int fd;
  if (fd = open(argv[2], O_RDONLY) < 0) {
    fprintf(stderr, "Cannot open source file path.\n");
    exit(1);
  }
  // Get source file size.
  struct stat st;
  fstat(fd, &st);
  int file_size = st.st_size;
  int blocks_needed = file_size / EXT2_BLOCK_SIZE +
    (file_size % EXT2EXT2_BLOCK_SIZE != 0);

  char type = '\0';
  char *name_var;
  struct ext2_inode *dir_inode = NULL;

  // Check valid target absolute_path (must be directory)
  // If not valid -> ENOENT
  // Get the inode of the given path
  char *parent_path = get_dir_path(argv[3]);
  struct ext2_inode *target_inode = trace_path(argv[3], disk);
  struct ext2_inode *parent_inode = trace_path(parent_path, disk);
  if (target_inode == NULL && parent_path == NULL) {
    printf("%s :Invalid path.\n", argv[3]);
    return ENOENT;

  // If file path
  // File path with file DNE (yet) in a valid directory path.
  } else if (target_inode == NULL && parent_path != NULL) {
    type = 'f';
    name_var = get_file_name(argv[3]);
    dir_inode = parent_inode;
  }

  // If such file exist -> EEXIST, no overwrite
  if (target_inode->i_mode & EXT2_S_IFREG) {
    printf("%s :File exists.\n", argv[3]);
    return EEXIST;
  }

  // If directory path
  // name variable -> source file name
  if (target_inode->i_mode & EXT2_S_IFDIR) {
    type = 'd';
    name_var = get_file_name(argv[2]);
    dir_inode = target_inode;
  }

  // Q: What if the given absolute path is a link?
  // If soft link check if file still exist, if exist check name
  // Hard link -> check count, same as normal aboslute path
  if (target_inode->i_mode & EXT2_S_IFLNK) {
    type = 'l';
    printf("%s :File exists.\n", argv[3]);
    return EEXIST;
  }

  // Check if there is enough inode (require 1)
  if (disk->s_free_inodes_count <= 0) {
    printf("File system does not have enough free inodes.\n");
    return ENOSPC;
  }

  // Check if there is enough blocks for Data
  if ((blocks_needed <= 12 && disk->s_free_blocks_count < blocks_needed) ||
    (blocks_needed > 12 && disk->s_free_blocks_count < blocks_needed + 1)) {
    printf("File system does not have enough free blocks.\n");
    return ENOSPC;
  } // If indirected block needed, one more indirect block is required to store pointers

  // Require a free inode
  int i_num = get_free_inode(b, i_bitmap);
  struct ext2_inode *tar_node = i_bitmap[i_num - 1];

  // Init the inode
  tar_node->i_mode = EXT2_S_IFREG;
  tar_node->i_size = (unsigned int) file_size;
  tar_node->i_links_count = 1; // ?? or 1?
  tar_node->i_blocks = 0;

  // Change data in superblock and group desciptor
  sb -> s_free_inodes_count --;
  gd -> bg_free_inodes_count --;

  // Write into target file (data blocks)
  // Requires enough blocks
  char buf[EXT2_BLOCK_SIZE];
  int iblock_index = 0;
  int indirect_num;

  while (read(fd, buf, EXT2_BLOCK_SIZE) != 0) {
    if (iblock_index < SINGLE_INDIRECT) {
      int b_index = get_free_block(sb, b_bitmap);
      unsigned char *block = disk + b_index * EXT2_BLOCK_SIZE;
      memcpy(block, buf, EXT2_BLOCK_SIZE);
      tar_node->i_block[iblock_index] = (unsigned int) b_index;
      tar_inode->i_blocks += 2;
      iblock_index ++;

    } else { //SINGLE_INDIRECT
      if (iblock_index == 12) { // First time access indirect blocks
        indirect_num = get_free_block(sb, b_bitmap);
        tar_inode->i_blocks += 2;
      }

      unsigned int *indirect_block = (unsigned int *)(disk + indirect_num * EXT2_BLOCK_SIZE);
      int b_index = get_free_block(sb, b_bitmap);
      unsigned char *block = disk + b_index * EXT2_BLOCK_SIZE;
      memcpy(block, buf, EXT2_BLOCK_SIZE);
      indirect_block[iblock_index - 12] = b_index;
      tar_inode->i_blocks += 2;
      iblock_index ++;
    }
  }

  // Create a new entry in directory
  struct ext2_dir_entry_2 *new_entry = setup_entry(tar_inode, name_var, type);
  // Init entry, reate new file with name variable
  add_new_entry(dir_inode, new_entry);

  return 0;
}

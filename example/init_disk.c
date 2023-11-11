/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.
*/

/** @file
 *
 * minimal example filesystem using high-level API
 *
 * Compile with:
 *
 *     gcc -Wall init_disk.c `pkg-config fuse3 --cflags --libs` -o init_disk
 *
 * ## Source code ##
 * \include init_disk.c
 */

#define FUSE_USE_VERSION 31

#include <unistd.h>
#include <time.h>
#include "sampleFS.h"



void InitSuperBlock(struct sb* super_blk)
{
  super_blk->fs_size = 8 * 1024 * 1024  / fs_bl_size;  //文件系统的大小，以块为单位
  super_blk->first_blk= getDataOffset() / fs_bl_size;  //数据区的第一块块号，根目录也放在此
  super_blk->datasize = dataBlock_count;  //数据区大小，以块为单位 
  super_blk->first_inode = 1;    //inode区起始块号
  super_blk->inode_area_size = inode_count;   //inode区大小，以块为单位
  super_blk->fisrt_blk_of_inodebitmap = getInodeBitmapOffset() / fs_bl_size;   //inode位图区起始块号
  super_blk->inodebitmap_size = inodeBitmap_count;  // inode位图区大小，以块为单位
  super_blk->first_blk_of_databitmap = getDataBitmapOffset() / fs_bl_size;   //数据块位图起始块号
  super_blk->databitmap_size = dataBitmap_count;      //数据块位图大小，以块为单位
}

int MoveBlockPtr(FILE* fp, int block_count_ptr)
{
  return fseek(fp, block_count_ptr * fs_bl_size, SEEK_SET);
}

void InitBitmapInodeBlock(struct bitmap_inode* bi)
{
  // 置0
  memset(bi,sizeof(struct bitmap_inode), 1);
}

void InitFirstInode(FILE *fp)
{
  int savePtr = ftell(fp); // 保存原来指针位置，初始化函数不应该改写闭包外环境

  struct inode* node = malloc(sizeof(struct inode));
  node->st_mode = __S_IFDIR | 0755; /* 权限，2字节。此处定义为 755的目录 */ 
  node->st_ino = 1; /* i-node号，2字节 */ 
  node->st_nlink = 1; /* 连接数，1字节 */ 
  node->st_uid = getuid(); /* 拥有者的用户 ID ，4字节 */ 
  node->st_gid = getgid(); /* 拥有者的组 ID，4字节  */ 
  node->st_size = sizeof(struct dentry); /*文件大小，4字节 */ 
  timespec_get(&node->st_atim, TIME_UTC);/* 16个字节time of last access */ 
  for (int i=0; i < 7 ; i++) {
    node->addr[i] = 0; /*磁盘地址，14字节。addr[0]-addr[3]是直接地址，addr[4]是一次间接，addr[5]是二次间接，addr[6]是三次间接。*/
  }
  node->addr[0] = 1; //设置根目录数据区号
  fseek(fp, getInodeOffset(), SEEK_SET);
  fwrite(node, sizeof(struct inode), 1, fp);
  // 考虑到文件系统较小，为了简化问题且提高读取效率，一次性读取整个 bitmap

  // 标记根目录的inode位图
  struct bitmap_inode* bi = malloc(sizeof(struct bitmap_inode));
  if(fseek(fp, getInodeBitmapOffset(), SEEK_SET) != 0) { // 标记Inode位图
    fprintf(stderr ,"Read fist inode failed"); 
    return;
  }
  fread(bi, sizeof(struct bitmap_inode), 1, fp);
  setBitmapValue(bi, 1, 1); // 关键代码
  if(fseek(fp, getInodeBitmapOffset(), SEEK_SET) != 0) { // 重新定位指针
    fprintf(stderr ,"Write fist inode failed");
    return;
  }
  fwrite(bi, sizeof(struct bitmap_inode), 1, fp);
  fflush(fp);
  free(bi);

  fseek(fp, savePtr, SEEK_SET); // 还原指针位置
}

void InitFirstDEntry(FILE* fp) // 初始化根目录到数据区
{
  int savePtr = ftell(fp);

  struct dentry* dirPtr = malloc(sizeof(struct dentry));
  if(fseek(fp, getDataOffset(), SEEK_SET) != 0) {
    fprintf(stderr, "Init First Directory Entry failed.\n");
    return;
  }
  strcpy(dirPtr->fileName,"/");
  strcpy(dirPtr->postFix, "");
  dirPtr->inodeNo = 1;
  for(int i = 0; i < max_child_count; i++) {
    dirPtr->childInodeNo[i] = 0; // 标记没有子文件
  }

  fwrite(dirPtr, sizeof(struct dentry), 1, fp);

  // 修改数据区位图
  struct bitmap_dblock* ptrBd = malloc(sizeof(struct bitmap_dblock));
  fseek(fp, getDataBitmapOffset(), SEEK_SET);
  fread(ptrBd, sizeof(struct bitmap_dblock), 1, fp);
  setBitmapValue(ptrBd, 1, 1);
  fseek(fp, getDataBitmapOffset(), SEEK_SET);
  fwrite(ptrBd, sizeof(struct bitmap_dblock), 1, fp);
  
  fflush(fp);
  free(dirPtr);

  fseek(fp, savePtr, SEEK_SET); // 还原文件指针位置
}

int main(int argc, char *argv[])
{
  printf("init_disk\nStart init %s\n", imgPath);
  FILE* fp = fopen(imgPath, "r+");
  if(fp == NULL) {
    printf("Open img failed.\n");
    return -1;
  }
  int block_count_ptr = 0; // 指定当前块指针

  // 超级块： 1 块
  struct sb *super_blk = malloc(sizeof(struct sb));
  fwrite(super_blk, sizeof(struct sb), 1, fp);
  printf("Init super block success.\n");
  block_count_ptr += sb_count;
  free(super_blk);
  
  // inode 位图块: 1 块 1*512*8=4K 个inode标记，即文件系统支持最多4K个文件
  if(MoveBlockPtr(fp, block_count_ptr) != 0) {
    fprintf(stderr, "Bitmap inode seek failed.\n");
  }
  struct bitmap_inode* bI_block= malloc(sizeof(struct bitmap_inode));
  InitBitmapInodeBlock(bI_block);
  fwrite(bI_block, sizeof(struct bitmap_inode), 1, fp);
  printf("Init Bitmap Inode block success.\n");
  block_count_ptr += inodeBitmap_count;
  free(bI_block);


  // 数据位图块： 4块 4*512*8=16K 个数据块标记，16K*512=8M 的数据空间总支持大小
  if(MoveBlockPtr(fp, block_count_ptr) != 0) {
      fprintf(stderr, "Bitmap Data Block seek failed.\n");
  }
  // 由于文件创建时以/dev/zero内容填充，故不再空白填充(注意：这表明重置文件系统的正确方法是删除Img文件并运行创建脚本重新创建)
  block_count_ptr += dataBitmap_count;

  // inode块: 需要 4K 个inode，每个inode 64字节，共需 4K*64/512 = 512 块 
  fflush(fp); // flush the file, make sure that all content write to fp
  if(MoveBlockPtr(fp, block_count_ptr) != 0) {
      fprintf(stderr, "Inode Block seek failed.\n");
  }

  // 初始化第一个inode
  InitFirstInode(fp);

  block_count_ptr += inode_count;

  // Inode 区；数据区: 填充根目录到数据区
  if(MoveBlockPtr(fp, block_count_ptr) != 0) {
      fprintf(stderr, "Data Block seek failed.\n");
  }

  // 初始化第一个dataBlock
  InitFirstDEntry(fp);
  
  fclose(fp);
  printf("Init disk successfully!\n");
  return 0;
}
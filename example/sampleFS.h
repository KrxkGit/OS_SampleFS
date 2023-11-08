#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <malloc.h>

char imgPath[] = "./SFS_Img"; // 设备载体

const int fs_bl_size = 512; // 块的大小(Bytes)

// 下列为各个区域所用块数
const int sb_count = 1;
const int inodeBitmap_count = 1;
const int dataBitmap_count = 4;
const int inode_count = 512;
const int dataBlock_count = 15866; // 8*1024*1024/512-1-1-4-512=15866
// 辅助函数： 返回区域头部距离文件系统头部的偏移量：单位（字节）.采用 inline 提高效率减少递归并保持扩展性
inline int getSuperBlockOffset()
{
    return 0;
}
inline int getInodeBitmapOffset()
{
    return sb_count * fs_bl_size;
}
inline getDataBitmapOffset()
{
    return getInodeBitmapOffset() + inodeBitmap_count * fs_bl_size;
}
inline getInodeOffset()
{
    return getDataBitmapOffset() + dataBitmap_count * fs_bl_size;
}
inline getDataOffset()
{
    return getInodeBitmapOffset() + inode_count * fs_bl_size;
}

struct sb { // 超级块
    long fs_size;  //文件系统的大小，以块为单位
    long first_blk;  //数据区的第一块块号，根目录也放在此
    long datasize;  //数据区大小，以块为单位 
    long first_inode;    //inode区起始块号
    long inode_area_size;   //inode区大小，以块为单位
    long fisrt_blk_of_inodebitmap;   //inode位图区起始块号
    long inodebitmap_size;  // inode位图区大小，以块为单位
    long first_blk_of_databitmap;   //数据块位图起始块号
    long databitmap_size;      //数据块位图大小，以块为单位
};

struct bitmap_inode {
    char available[4 * 1024 / 8];
};

struct bitmap_dblock {
    char available[2048]; // 8 * 1024* 1024 / 512 / 8 = 2048
};

// 辅助函数： 读取,设置第 n 个bit的标志。为了简化，不进行越界检查。
// 注意： 从 1 开始编号，否则将越界
int getBitmapValue(char* bytes,int bit) // 输入字节数组，获得指定节点的值
{
    int n = bit/8;
    int r = bit - 8 * n;
    r -= 1;
    char c = bytes[n] >> r;
    c = c & 1;
    return (int)c;
}
void setBitmapValue(char* bytes ,int bit ,int Value)
{
    int n = bit/8;
    int r = bit - 8 * n;
    r -= 1;
    char t = 1 << r;

    if(Value == 0) {
        t = ~t;
        bytes[n] = bytes[n] & t;
       
    } else {
        bytes[n] = bytes[n] | t;
    }
   
}


struct inode { 
    short int st_mode; /* 权限，2字节 */ 
    short int st_ino; /* i-node号，2字节，
    从1开始编号 */ 
    char st_nlink; /* 连接数，1字节，不区分硬链接与软连接 */ 
    uid_t st_uid; /* 拥有者的用户 ID ，4字节 */ 
    gid_t st_gid; /* 拥有者的组 ID，4字节  */ 
    off_t st_size; /*文件大小，4字节 */ 
    struct timespec st_atim; /* 16个字节time of last access */ 
    short int addr [7];    /*磁盘地址，14字节。addr[0]-addr[3]是直接地址，addr[4]是一次间接，addr[5]是二次间接，addr[6]是三次间接。
    为了方便，此处使用相对地址，即相对数据区头部的偏移！**/
    char __reserve[17]; // 填充为 64 字节 
};

struct dentry
{
    char fileName[8];
    char postFix[3];
    char inodeNo[2]; // inode 号，实际使用12位
    char __reserve[3]; // 保留备用
};
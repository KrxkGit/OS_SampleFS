#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <malloc.h>
#include <unistd.h>
#include <time.h>

/*注意：inode number 为 0 时为非法，用于作为判断标记*/

char imgPath[] = "/home/krxk/fuse-3.16.2/build/example/SFS_Img"; // 设备载体

#define max_child_count 20 // 目录下最大子文件数量
const int fs_bl_size = 512; // 块的大小(Bytes)

// 下列为各个区域所用块数
const int sb_count = 1;
const int inodeBitmap_count = 1;
const int dataBitmap_count = 4;
const int inode_count = 512;
const int dataBlock_count = 15866; // 8*1024*1024/512-1-1-4-512=15866

// 数据结构定义
struct inode { 
    short int st_mode; /* 权限，2字节 */ 
    short int st_ino; /* i-node号，2字节，
    从1开始编号 */ 
    char st_nlink; /* 连接数，1字节，不区分硬链接与软连接 */ 
    uid_t st_uid; /* 拥有者的用户 ID ，4字节 */ 
    gid_t st_gid; /* 拥有者的组 ID，4字节  */ 
    off_t st_size; /*文件大小，4字节 */ 
    struct timespec st_atim; /* 16个字节time of last access */ 
    short int addr [7];    /*磁盘块地址，14字节。addr[0]-addr[3]是直接地址，addr[4]是一次间接，addr[5]是二次间接，addr[6]是三次间接。
    为了方便，此处使用相对地址，即相对数据区头部的偏移！由于文件对应的数据块不一定是连续的，所以每个指针指向一个块地址。**/
    char __reserve[17]; // 填充为 64 字节 
};

struct dentry // 由于目录本质上也是一种文件，故文件也采用此结构体
{
    char fileName[8]; // 文件名
    char postFix[3]; // 扩展名
    short int inodeNo; // inode 号，实际使用12位
    short int childInodeNo[max_child_count]; // 子文件iNode 号
};

struct fileObj { // 文件头部
    char fileName[8]; // 文件名
    char postFix[3]; // 扩展名
    short checksum; // 根据文件头部信息生成的校验和，用于区分是否为一个文件或目录
};

struct sb { // 超级块
    long fs_size;  //文件系统的大小，以块为单位
    long first_blk;  //数据区的第一块块号，根目录也放在此（修改为根目录inode放在inode区）
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
// 文件系统状态量区
short int pwdInodeNo; // 标记当前目录的inode号,目前保留


// 辅助函数： 返回区域头部距离文件系统头部的偏移量：单位（字节）.采用 inline 提高效率减少递归并保持扩展性
inline int getSuperBlockOffset()
{
    return 0;
}
inline int getInodeBitmapOffset()
{
    return sb_count * fs_bl_size;
}
inline int getDataBitmapOffset()
{
    return getInodeBitmapOffset() + inodeBitmap_count * fs_bl_size;
}
inline int getInodeOffset()
{
    return getDataBitmapOffset() + dataBitmap_count * fs_bl_size;
}
inline int getDataOffset()
{
    return getInodeOffset() + inode_count * fs_bl_size;
}
// 二层封装
inline int getDataOffsetByNum(int nBlock/*数据块编号*/)
{
    return getDataOffset() + (nBlock - 1) * fs_bl_size;
}
inline int getInodeOffsetByNum(int iNodeNo/*inode编号*/)
{
    return getInodeOffset() + (iNodeNo - 1) * sizeof(struct inode);
}

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
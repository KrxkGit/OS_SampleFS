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
const Native_block_size = 4096; //原生系统的块大小

#define max_child_count 20 // 目录下最大子文件数量
const int fs_bl_size = 512; // 块的大小(Bytes)

// 下列为各个区域所用块数
const int sb_count = 1;
const int inodeBitmap_count = 1;
const int dataBitmap_count = 4;
const int inode_count = 512;
const int dataBlock_count = 15866; // 8*1024*1024/512-1-1-4-512=15866

 // 定义地址level 的起始地址，按顺序读写，从level_0 开始，填满一个层级再到下一级
const int level_0 = 0; // 直接
const int level_1 = 4; // 一级间接
const int level_2 = 5; // 二级间接
const int level_3 = 6; // 三级间接

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

// 为了简化且实现文件系统的易扩展性，目录头/文件头 设计为单独占一块，如此考虑是因为可以可以在往后很方便地实现扩展而不影响文件内容
struct dentry // 由于目录本质上也是一种文件，故文件也采用此结构体
{
    char fileName[9]; // 文件名(预留一个终止符位置)
    char postFix[4]; // 扩展名(未支持，可考虑扩展)
    short int inodeNo; // inode 号，实际使用12位
    short int childInodeNo[max_child_count]; // 子文件iNode 号
};

struct fileObj { // 文件头部
    char fileName[9]; // 文件名
    char postFix[4]; // 扩展名
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
char* ReadCache = NULL; // 读文件缓存
int ReadFinish = 0; // 标志是否缓存完成(目前似乎冗余，保留)


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
// 由于 char* 位于位图结构体的头部，所以根据 C语言规则， 可直接向下列函数传递结构体指针
// 注意： bit 从 1 开始编号，否则将越界
int getBitmapValue(char* bytes,int bit) // 输入字节数组，获得指定节点的值
{
    bit--;
    int n = bit/8;
    int r = bit - 8 * n;
    // r -= 1;
    char c = bytes[n] >> r;
    c = c & 1;
    return (int)c;
}
void setBitmapValue(char* bytes ,int bit ,int Value)
{
    bit--;
    int n = bit/8;
    int r = bit - 8 * n;
    // r -= 1;
    char t = 1 << r;

    if(Value == 0) {
        t = ~t;
        bytes[n] = bytes[n] & t;
       
    } else {
        bytes[n] = bytes[n] | t;
    }
   
}

// 辅助结构，将间接地址转换为链式结构方便读取
#define AddrNodeCapacity 20 /*10K*/
// 重定义结构体，避免多次声明struct类型
typedef struct AddrNode {
    short addr[AddrNodeCapacity];
    struct AddrNode* next;
} AddrNode;

//工厂函数，创建链表头
AddrNode* CreateListHead()
{
    AddrNode* pHead = malloc(sizeof(AddrNode));
    memset(pHead, 0, sizeof(AddrNode));
    return pHead;
}
//添加链式节点，请在外部保留链表头部指针.注意：函数将修改pCurrent, *ptrNewAddrNumIndex应初始化为0
void AddAddrToListTail(AddrNode** pCurrent ,short* ptrNewAddrNumIndex/*加入的新地址的索引*/, short addrNum/*磁盘地址值*/)
{
    AddrNode* pCopy = *pCurrent;

    if(*ptrNewAddrNumIndex >= AddrNodeCapacity) { // 需要新建节点
        // 新建节点
        pCopy->next = malloc(sizeof(AddrNode));
        pCopy = pCopy->next;
        
        // 填充新节点
        memset(pCopy, 0, sizeof(AddrNode));
        pCopy->next = NULL;
        pCopy->addr[0] = addrNum;
        *ptrNewAddrNumIndex = 1; // 下一个为 1
    } else {
        pCopy->addr[*ptrNewAddrNumIndex] = addrNum;
        (*ptrNewAddrNumIndex)++;
    }

    *pCurrent = pCopy;
}

/*返回下一个块地址号，若无更多，返回-1. *ptrCurAddrIndex应初始化为 -1*/
int ReadAddrList(AddrNode** pCurrent/*标记当前使用的list Node*/, int* ptrCurAddrIndex/*上一次迭代时使用的节点addr索引<AddrNodeCapacity*/)
{
    AddrNode* pCopy = *pCurrent;
    short res = 0;

    (*ptrCurAddrIndex)++; 

    if(*ptrCurAddrIndex >= AddrNodeCapacity) {
        AddrNode* goNext = pCopy->next;
        if(goNext == NULL) {
            return -1;
        }
        *ptrCurAddrIndex = 0;
        *pCurrent = goNext;
        res = goNext->addr[*ptrCurAddrIndex];
    } else {
        res = pCopy->addr[*ptrCurAddrIndex];
    }
    if(res == 0) {
        res = -1;
    }
    return res;
}
void FreeAddrList(AddrNode* pHead)
{
    AddrNode* temp = NULL;
    for(AddrNode* pCurrent = pHead; pCurrent!=NULL; ) {
        temp = pCurrent->next;
        free(pCurrent);
        pCurrent = temp;
    }
}


// 辅助函数：递归查找 Inode 指向的磁盘地址

// 简单抽取重复代码，可考虑利用参数宏进一步抽取
#define PrepareAction \
    char* pAddrReading = malloc(fs_bl_size); \
    memset(pAddrReading, 0, fs_bl_size); \
    fseek(fp, getDataOffsetByNum(blockNum), SEEK_SET); \
    fread(pAddrReading, fs_bl_size, 1, fp); \


void ReadOnceIndirect(FILE* fp, short blockNum, AddrNode** pCurrent, short* curIndex) 
// 注意不要改变参数的指针级别类型，否则会因为发生参数拷贝而使链表失效
{
    PrepareAction

    char* pEnd = pAddrReading + fs_bl_size;
    for(short* pRealAddrNum = pAddrReading; pRealAddrNum < pEnd; pRealAddrNum++) {
        // *pReadAddrNum 为直接地址
        AddAddrToListTail(pCurrent, curIndex, *pRealAddrNum);
    }
    free(pAddrReading);
}

void ReadTwiceIndirect(FILE* fp, short blockNum, AddrNode** pCurrent, short* curIndex)
{
    PrepareAction

    char* pEnd = pAddrReading + fs_bl_size;
    for(short* pRealAddrNum = pAddrReading; pRealAddrNum < pEnd; pRealAddrNum++) {
        // *pReadAddrNum 为一次间接地址
        ReadOnceIndirect(fp, *pRealAddrNum, pCurrent, curIndex);
    }
    free(pAddrReading);
}
void ReadThirdIndirect(FILE* fp, short blockNum, AddrNode** pCurrent, short* curIndex)
{
    PrepareAction

    char* pEnd = pAddrReading + fs_bl_size;
    for(short* pRealAddrNum = pAddrReading; pRealAddrNum < pEnd; pRealAddrNum++) {
        // *pReadAddrNum 为二次地址
        ReadTwiceIndirect(fp, *pRealAddrNum, pCurrent, curIndex);
    }
    free(pAddrReading);
}
#undef PrepareAction

// fp为ImgDisk 文件对象,通过外部传入以避免频繁涉及文件打开关闭操作，返回结果链表头部指针
AddrNode* HelpWalkInodeTable(FILE* fp ,struct inode* pIonde)
{
    //addr[0]-addr[3]是直接地址，addr[4]是一次间接，addr[5]是二次间接，addr[6]是三次间接。
    short *addArray = pIonde->addr;

    /*因为 每个块可最多容纳： 512 / 2 = 256 个磁盘地址块号，一次性返回如此大且大概率用不到的空间是不合理的。
    但由于本文件系统不支持Random Access，可利用逻辑空间的连续性，动态获取，当读取后其中一块未利用时，认为后续都未利用。
    另外就是：间接块将放置在数据区。
    综上：考虑采用链表维护可用块地址号*/
    
    int goContinue = 1; // 标记未结束,结束为0
    AddrNode* pHead = CreateListHead();
    AddrNode* pCurrent = pHead;
    int curIndex = 0;


    for(int i = level_0; i < level_1 && goContinue; i++) { // 直接
        short tempAddr = pIonde->addr[i];
        if(tempAddr == 0) {
            goContinue = 0;
            break;
        }
        AddAddrToListTail(&pCurrent, &curIndex, tempAddr);
    }
    for(int i = level_1; i < level_2 && goContinue; i++) { // 一次间接
        ReadOnceIndirect(fp, getDataOffsetByNum(pIonde->addr[i]), &pCurrent, &curIndex);
    }
    for(int i = level_2; i < level_3 && goContinue; i++) { // 二次间接
        ReadTwiceIndirect(fp, getDataOffsetByNum(pIonde->addr[i]), &pCurrent, &curIndex);
    }
    if(goContinue) { //三次间接
        ReadThirdIndirect(fp, getDataOffsetByNum(pIonde->addr[level_3]), &pCurrent, &curIndex);
    }
    
    return pHead; // 返回链表头
}

/*辅助申请一块可用块用于写入，通过链表方式维护所有块，并最后进行整理。请在外部保存好链表头指针、当前链表节点指针、当前读写索引位置.
若成功，函数返回可用磁盘块的块号，否则返回-1.
调用前请通过调用CreateListHead创建地址维护列表，并妥善保存相关状态量*/
short HelpAllocDataBlock(FILE* fp, AddrNode** ppCurrent, int* ptrNewAddNumIndex)
{
    struct bitmap_dblock* ptrDb = malloc(sizeof(struct bitmap_dblock));
    fseek(fp, getDataBitmapOffset(), SEEK_SET);
    fread(ptrDb, sizeof(struct bitmap_dblock), 1, fp);

    int available_block_num = 0;
    for(int i = 1; i <= dataBlock_count ; i++) {
        if(getBitmapValue(ptrDb, i) == 0) {
            available_block_num = i;
            setBitmapValue(ptrDb, i, 1);
            break;
        }
    }
    if(available_block_num == 0) {
        perror("No more space for write.\n");
        return -1; // 返回 -1 用于外部检查
    }
    AddAddrToListTail(ppCurrent, ptrNewAddNumIndex, available_block_num);

    // 修改位图
    fseek(fp, getDataBitmapOffset(), SEEK_SET);
    fwrite(ptrDb, sizeof(struct bitmap_dblock), 1, fp);
    // 清理
    free(ptrDb);
    
    return available_block_num;
}

/*下次写入安全返回状态1，下次写入将溢出返回状态0
注意：请在外部更新 writer*/
int DoWriteIndirect(short* writer/*本次写入位置*/, char* pMemHead, short AddrNum)
{
    *writer = AddrNum;

    // 预测下次是否溢出
    writer++;
    if(writer > pMemHead + fs_bl_size) { //溢出
        return 0;
    }
    
    return 1;
}

typedef struct WriterStatus {
    FILE* fp; // 仅用于外部
    short* writer[3]; // 保存多级写指， [0]对应一级间接，[1]对应二级间接，[2]对应三级间接
    short* pRealWriter; // 写入最终直接地址的指针
    char* pMemHead[3]; // 保存的 pMemHead,[0]对应 一级间接，[1]对应二级间接，[2]对应三级间接
    int CurrentMemIndex; // pMemHead的某一个号，表明当前使用的
    short DiskSpaceNum[3]; // 用于返回间接块的磁盘块地址号,[0]对应一级间接，[1]对应二级间接，[2]对应三级间接
} WriterStatus;

int HelpWriteIndirect(WriterStatus* writerStatus, short AddrNum)
{
    char* pWriter = writerStatus->pRealWriter;
    if(pWriter == NULL) { 
        pWriter = malloc(fs_bl_size);
        memset(pWriter, 0, fs_bl_size);
        
        writerStatus->pRealWriter = pWriter;
        writerStatus->pMemHead[writerStatus->CurrentMemIndex] = pWriter; // 由于是逐级使用的，不需要担心底层头被高层头覆盖的问题，因为不再需要使用
        //获取可用块
        int available = 0;
        struct bitmap_dblock* ptrDb = malloc(sizeof(struct bitmap_dblock));
        fseek(writerStatus->fp, getDataBitmapOffset(), SEEK_SET);
        fread(ptrDb, sizeof(struct bitmap_dblock), 1, writerStatus->fp);

        for(int i = 1; i <= dataBlock_count; i++) {
            if(getBitmapValue(ptrDb, i) == 0) {
                available = i;
                setBitmapValue(ptrDb, i, 1);
                break;
            }
        }
        if(available == 0) {
            perror("No more available data block.\n");
            return -EFBIG;
        }
        fseek(writerStatus->fp, getDataBitmapOffset(), SEEK_SET);
        fwrite(ptrDb, sizeof(struct bitmap_dblock), 1, writerStatus->fp);

        writerStatus->DiskSpaceNum[writerStatus->CurrentMemIndex] = available; // 保存该级磁盘号
        if(writerStatus->CurrentMemIndex > 0) {
            writerStatus->writer[writerStatus->CurrentMemIndex - 1] = NULL; // 清空低级指针
        }
    } else { // 已存在
        (writerStatus->writer[writerStatus->CurrentMemIndex])++;

        if(writerStatus->writer[writerStatus->CurrentMemIndex] > writerStatus->pMemHead[writerStatus->CurrentMemIndex]
        + fs_bl_size) {
            // 若该级溢出，向上
            writerStatus->CurrentMemIndex++;
            if(writerStatus->CurrentMemIndex > 2) {
                perror("No more Space.\n");
                return -EFBIG;
            }
            writerStatus->pRealWriter = writerStatus->writer[writerStatus->CurrentMemIndex];
            writerStatus->writer[writerStatus->CurrentMemIndex - 1] = NULL; // 清空低级指针
        }
    }

    if(writerStatus->CurrentMemIndex > 0) { // 未到一级间接
        writerStatus->CurrentMemIndex--;
        writerStatus->pRealWriter= writerStatus->writer[writerStatus->CurrentMemIndex];
        HelpWriteIndirect(writerStatus, AddrNum); // 递归调用
    } else {
        int res = DoWriteIndirect(writerStatus->pRealWriter, writerStatus->pMemHead[writerStatus->CurrentMemIndex],
                                AddrNum);
    }
    return 0;
}

/*辅助将所有使用的地址块合理填入Inode,成功返回0*/
int HelpTidyAddr(FILE* fp, AddrNode* pHead, struct inode* ptrInode)
{
    short* pAddr = ptrInode->addr;
    if(pHead == NULL) {
        perror("Empty Addr List.\n");
        return -1;
    }
    
    // 从 arr[1] 开始，arr[0]为文件头
    int curIndex = -1;
    int res = -1;
    int AddrCount = 1; // 从addr[1]开始写文件内容，addr[0]为文件头


    WriterStatus writerStatus;
    memset(&writerStatus, 0, sizeof(WriterStatus));
    writerStatus.fp = fp;

    int returnValue = 0;

    for(AddrNode* pCurrent = pHead; pCurrent!=NULL;) {
        res = ReadAddrList(&pCurrent, &curIndex);
        if(res == -1) { // 无更多块，可终止
            break;
        }
        if(level_0 <= AddrCount && AddrCount < level_1) {
            ptrInode->addr[AddrCount] = (short)res;
        }

        returnValue = HelpWriteIndirect(&writerStatus, res);
        if(returnValue == -EFBIG) {
            perror("No more Space.\n");
            return -EFBIG;
        }
        AddrCount++;
    }
    ptrInode->addr[level_1] = writerStatus.DiskSpaceNum[0];
    ptrInode->addr[level_2] = writerStatus.DiskSpaceNum[1];
    ptrInode->addr[level_3] = writerStatus.DiskSpaceNum[2];
}
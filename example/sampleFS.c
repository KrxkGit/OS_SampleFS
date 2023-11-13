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
 *     gcc -Wall sampleFS.c `pkg-config fuse3 --cflags --libs` -o sampleFS
 *
 * ## Source code ##
 * \include sampleFS.c
 */


#define FUSE_USE_VERSION 31

#include"sampleFS.h"

/*
 * Command line options
 *
 * We can't set default values for the char* fields here because
 * fuse_opt_parse would attempt to free() them when the user specifies
 * different values on the command line.
 */
static struct options {
	const char *filename;
	const char *contents;
	int show_help;
} options;

#define OPTION(t, p)                           \
    { t, offsetof(struct options, p), 1 }
static const struct fuse_opt option_spec[] = {
	OPTION("--name=%s", filename),
	OPTION("--contents=%s", contents),
	OPTION("-h", show_help),
	OPTION("--help", show_help),
	FUSE_OPT_END
};

static void *SFS_init(struct fuse_conn_info *conn,
			struct fuse_config *cfg)
{
	(void) conn;
	cfg->kernel_cache = 1;
	return NULL;
}

void HelpFillStat(struct stat* stbuf, struct inode* ptrInode) 	// 填充属性结构辅助函数
{
	stbuf->st_mode = ptrInode->st_mode;
	stbuf->st_nlink = ptrInode->st_nlink;
	stbuf->st_atime = ptrInode->st_atim.tv_sec;
	stbuf->st_ino = ptrInode->st_ino;
	stbuf->st_uid = ptrInode->st_uid;
	stbuf->st_gid = ptrInode->st_gid;
	stbuf->st_size = ptrInode->st_size;
}

short HelpGenFileObjHeadChecksum(struct fileObj* ptrFileObj) /*函数只生成校验码，不修改文件头部校验码，如需要修改请在外部修改*/
{
	short checksum = 0;
	char* filename = ptrFileObj->fileName;
	char *postfix = ptrFileObj->postFix;
	// 由于checksum 有 16 位，而 文件名模式为 8 + 3,取附加模式“Krxk"(4),共15位
	char attach[] = "Krxk";
	// 算法：取(每一位字母 - 'a') % 16 的值按位或
	int len = strlen(filename);
	for(int i = 0 ; i < len ; i++) {
		checksum |= 1 << ((filename[i] - 'a') % 16);
	}

	len = strlen(postfix);
	for(int i = 0 ; i < len ; i++) {
		checksum |= 1 << ((postfix[i] - 'a') % 16);
	}

	len = strlen(attach);
	for(int i = 0 ; i < len ; i++) {
		checksum |= 1 << ((attach[i] - 'a') % 16);
	}

	return checksum;
}

void HelpGetFileNameFromInodeNum(int inodeNum, char* pFileName)
{
	printf("HelpGetFileNameFromInodeNum is called.\t inodeNum:%d\n", inodeNum);
	FILE* fp = fopen(imgPath, "r");
	if(fp == NULL) {
		perror("OpenFile failed.\n");
		return;
	}
	// 读出Inode
	fseek(fp, getInodeOffsetByNum(inodeNum), SEEK_SET);
	struct inode* ptrInode = malloc(sizeof(struct inode));
	fread(ptrInode, sizeof(struct inode), 1, fp);

	printf("Reading DataBlock: %d\n", ptrInode->addr[0]);

	// 读出 data
	struct fileObj* ptrFileObj = malloc(sizeof(struct fileObj));
	fseek(fp, getDataOffsetByNum(ptrInode->addr[0]), SEEK_SET);
	fread(ptrFileObj, sizeof(struct fileObj), 1, fp);

	printf("Calculate checksum: %d\tCompare: %d \n", HelpGenFileObjHeadChecksum(ptrFileObj), ptrFileObj->checksum);

	if(HelpGenFileObjHeadChecksum(ptrFileObj) == ptrFileObj->checksum) { // 普通文件
		HelpConcatFileName(ptrFileObj->fileName, ptrFileObj->postFix, pFileName);
		printf("Block head: Regular file.\tfilename: %s\n",pFileName);
		free(ptrFileObj);
	} else { // 目录
		printf("Block head: Entry.\n");
		fseek(fp, getDataOffsetByNum(ptrInode->addr[0]), SEEK_SET);
		struct dentry* ptrEntry = malloc(sizeof(struct dentry));
		fread(ptrEntry, sizeof(struct dentry), 1, fp);
		HelpConcatFileName(ptrEntry->fileName, ptrEntry->postFix, pFileName);
		free(ptrEntry);
	}

	free(ptrInode);
	fclose(fp);

}

void HelpConcatFileName(char* fileName, char* postFix, char* concat)
{
	// printf("HelpConcatFileName is called.\tfilename: %s\t postFix: %s\n", fileName, postFix);
	concat[0]='\0';
	strcpy(concat, fileName);
	if(strcmp(postFix, "") == 0) {
		return;
	}
	strcat(concat,".");
	strcat(concat, postFix);
}

int HelpFindFile(const char* fileName, short int startInodeNum, int goDeep/*是否继续下一层*/) 
/*辅助查找文件，返回文件的inode号,判断成功条件：startInodeNum == 返回值*/
{
	// printf("HelpFindFile is called\t fileName: %s startInodeNum: %d\n", fileName, startInodeNum);
	int inodeOff = getInodeOffsetByNum(startInodeNum);
	FILE* fp = fopen(imgPath, "r");
	if(fp == NULL) {
		fprintf(stderr, "HelpFileFile Failed\tCannot open Img.\n");
		return -ENOENT;
	}
	if(fseek(fp, inodeOff, SEEK_SET)!=0) {
		fprintf(stderr, "HelpFindFile Failed:\tfseek failed.\n");
		return -ENOENT;
	}
	struct inode* ptrInode = malloc(sizeof(struct inode));
	fread(ptrInode, sizeof(struct inode), 1, fp);
	
	if(fseek(fp, getDataOffsetByNum(ptrInode->addr[0]),SEEK_SET)!=0) {
		fprintf(stderr, "HelpFileFile failed:\tseek data offest failed.\n");
		return -ENOENT;
	}
	struct fileObj* ptrfileObj = malloc(sizeof(struct fileObj));
	fread(ptrfileObj, sizeof(struct fileObj), 1, fp);
	char concat[256];

	if(ptrfileObj->checksum == HelpGenFileObjHeadChecksum(ptrfileObj)) { // 符合文件校验，为文件
		fclose(fp);
		HelpConcatFileName(ptrfileObj->fileName, ptrfileObj->postFix, concat);
		printf("HelpFindFile:\tFind Regular File: %s\n",concat);
		if(strcmp(concat, fileName) == 0) { // 找到了
			printf("Find regular file Sucessfully.\n");
			free(ptrfileObj);
			int temp = ptrInode->st_ino;
			free(ptrInode);
			return temp;
		} else {
			free(ptrfileObj);
			free(ptrInode);
			return -ENOENT;
		}
	} else { // startInodeNum 为目录,查找目录下一层深度下的所有文件
		fseek(fp, -sizeof(struct fileObj), SEEK_CUR);
		struct dentry* ptrEntry = malloc(sizeof(struct dentry));
		fread(ptrEntry, sizeof(struct dentry), 1, fp);
		HelpConcatFileName(ptrEntry->fileName, ptrEntry->postFix, concat);
		fclose(fp);

		// printf("HelpFindFile:\tDirctory: concat:%s\tfileName: %s\n", concat, fileName);
		if(strcmp(concat, fileName) == 0) { // 找到了
			int temp = ptrInode->st_ino;
			free(ptrInode);
			return temp;
		} else {
			free(ptrInode);
			if(goDeep == 0) {
				free(ptrEntry);
				return -ENOENT;
			}
			for(int i=0 ;i < max_child_count; i++) {
				// printf("HelpFindFile recursion\t Child: %d\n", i);
				if(HelpFindFile(fileName, ptrEntry->childInodeNo[i], 0) == ptrEntry->childInodeNo[i]) {
					// 成功找到
					printf("Success to find\tfilename: %s\t childInodeNo: %d\n", fileName, ptrEntry->childInodeNo[i]);
					int temp = ptrEntry->childInodeNo[i];
					free(ptrEntry);
					return temp;
				}
			}
			free(ptrEntry);
			return -ENOENT;
		}
	}
}

/*注意：函数将在end ==NULL 时修改 customPath 的内容，如果需要使用路径的最后一部分，请先在外部拷贝副本*/
void HelpSplitFileName(const char* customPath, const char* outSplitFileName)
{
	char* p = customPath;
	char* end = strchr(p, '/');
	if(end == NULL) {
		end = &customPath[strlen(customPath) - 1] + 2;
		*end = '\0';
	}
	strncpy(outSplitFileName, p, end-p);
	// printf("HelpSplitFileName:\tSplitFileName: %s\n", outSplitFileName);
}

int IsReachPathEnd(char* pNext)
{
	/*检查路径是否结束*/
	// printf("IsReachPathEnd\tpNext:%s\n", pNext);
	char* p = pNext;
	if(strlen(p) == 0) {
		printf("IsReachPathEnd\tReach End\n");
		return 1;
	} else {
		printf("IsReachPathEnd\tContinue\n");
		return 0;
	}
}

int HelpWalkPath(const char *customPath, short int startInodeNum/*遍历的起始InodeNum*/, char** pNext/*返回下一层遍历路径*/) 
/*辅助遍历目录，返回匹配文件的inode号，可通过检查 *(pNext-1) 是否为'\0' 检查是否遍历完成.
注意： 这表明 路径的末尾不应该加"/"，否则将引起报错 
注意：函数可能修改 customPath 的内容（这是由于调用了HelpSplitFileName导致的） */
{
	printf("HelpWalkPath\t customPath: %s\n", customPath);
	char* splitFilename = malloc(strlen(customPath));
	memset(splitFilename, '\0', sizeof(char) * strlen(customPath));
	HelpSplitFileName(customPath, splitFilename);
	// printf("HelpWalkPath\t splitFileName: %s\n", splitFilename);

	if(strcmp(splitFilename, "") == 0) { // 根目录
		free(splitFilename);
		*pNext = customPath + 1;
		return 1; // 根目录InodeNum=1
	} else {
		*pNext = customPath + strlen(splitFilename) + 1;
		int res =  HelpFindFile(splitFilename, startInodeNum, 1);
		free(splitFilename);
		return res;
	}
}

static int SFS_getattr(const char *path, struct stat *stbuf,
			 struct fuse_file_info *fi)
{
	printf("Get_attr\tPath:%s\n", path);
	(void) fi;
	int res = 0;
	memset(stbuf, 0, sizeof(struct stat));

	FILE* fp = fopen(imgPath, "r");
	if (fp == NULL) {
		fprintf(stderr, "Read File System failed.\n");
		perror("failed");
		return -ENOENT;
	}
	struct inode* ptrInode = malloc(sizeof(struct inode));

	// 按路径查找，直到路径末尾
	int startInodeNum = 1;
	for(char* pNext = path; !IsReachPathEnd(pNext);) {
		startInodeNum =  HelpWalkPath(pNext, startInodeNum, &pNext);
		if(startInodeNum == -ENONET) {
			// 找不到
			return -ENOENT;
		}
	}
	printf("startInodeNum:%d\n",startInodeNum);
	if(startInodeNum == -ENOENT) { // 找不到
		free(ptrInode);
		fclose(fp);
		return -ENOENT;
	}

	fseek(fp, getInodeOffsetByNum(startInodeNum), SEEK_SET);
	fread(ptrInode, sizeof(struct inode), 1, fp);
	fclose(fp);

	HelpFillStat(stbuf, ptrInode);
	printf("stbuf->st_ino: %d\tstbuf->st_atime: %d\tstbuf->st_size: %d\tnlink: %d\n",
	stbuf->st_ino, stbuf->st_atime, stbuf->st_size, stbuf->st_nlink);
	free(ptrInode);
	// if (strcmp(path, "/") == 0) {
	// 	fread(ptrInode, sizeof(struct inode), 1, fp);
	// 	HelpFillStat(stbuf, ptrInode);
	// } else if (strcmp(path+1, options.filename) == 0) {
	// 	stbuf->st_mode = S_IFREG | 0444;
	// 	stbuf->st_nlink = 1;
	// 	stbuf->st_size = strlen(options.contents);
	// } else
	// 	res = -ENOENT;

	// fclose(fp);
	return res;
}

static int SFS_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi,
			 enum fuse_readdir_flags flags)
{
	(void) offset;
	(void) fi;
	(void) flags;
	printf("SFS_readdir is called.\tpath: %s\n", path);
	// 查找目录的Inode
	int startInodeNum = 1;
	for(char *pNext = path; !IsReachPathEnd(pNext);) {
		startInodeNum = HelpWalkPath(pNext, startInodeNum, &pNext);
		if(startInodeNum == -ENOENT) {
			fprintf(stderr, "Inode: %d Not Found\n", startInodeNum);
			return startInodeNum; // 找不到
		}
	}
	int inodeOff = getInodeOffsetByNum(startInodeNum);
	FILE* fp = fopen(imgPath,"r");
	if(fp == NULL) {
		perror("readdir failed:\t Open img failed.");
		return -ENOENT;
	}
	fseek(fp, inodeOff, SEEK_SET);
	struct inode* ptrInode = malloc(sizeof(struct inode));
	fread(ptrInode, sizeof(struct inode), 1, fp);
	if(ptrInode->st_ino < 1) {
		// 非法 Inode 号，失败
		fprintf(stderr, "Invalid InodeNum: %d to read.\n", ptrInode->st_ino);
		free(ptrInode);
		fclose(fp);
		return -ENOENT;
	}

	printf("Reading DataBlock: %d\n", ptrInode->addr[0]);
	int dataOff = getDataOffsetByNum(ptrInode->addr[0]);
	struct dentry* ptrEntry = malloc(sizeof(struct dentry));
	fseek(fp, dataOff, SEEK_SET);
	fread(ptrEntry, sizeof(struct dentry), 1 ,fp);

	printf("Reading entry: %s\n", ptrEntry->fileName);

	char fileName[256];
	memset(fileName, 0, sizeof(fileName));

	printf("Child Inode Num: ");
	for(int i = 0; i < max_child_count; i++) {
		printf(" %d ",ptrEntry->childInodeNo[i]);
		if(ptrEntry->childInodeNo[i] > 0) {
			HelpGetFileNameFromInodeNum(ptrEntry->childInodeNo[i], fileName);
			filler(buf, fileName, NULL, 0, 0);
		}
	}
	printf("\n");


	// if (strcmp(path, "/") != 0)
	// 	return -ENOENT;

	filler(buf, ".", NULL, 0, 0);
	filler(buf, "..", NULL, 0, 0);
	// filler(buf, options.filename, NULL, 0, 0);

	free(ptrInode);
	free(ptrEntry);
	fclose(fp);
	return 0;
}

static int SFS_open(const char *path, struct fuse_file_info *fi)
{
	// 此函数主要用于检查权限等问题，为了简单，直接返回0忽略此步
	printf("SFS_open is called.\tpath: %s\n", path);
	// if (strcmp(path+1, options.filename) != 0)
	// 	return -ENOENT;

	// if ((fi->flags & O_ACCMODE) != O_RDONLY)
	// 	return -EACCES;

	return 0;
}

/*提示：可使用Shell 命令 cat file 进行读取*/
static int SFS_read(const char *path, char *buf, size_t size, off_t offset,
					struct fuse_file_info *fi)
{
	printf("SFS_Read is called.\tpath: %s\n", path);

	memset(buf, 0, size);

	// 查找文件Inode
	int startInodeNum = 1;

	char* pCopy = malloc(strlen(path) + 1);
	pCopy[0] = '\0';
	strcpy(pCopy, path);

	for (char *pNext = pCopy; !IsReachPathEnd(pNext);)
	{
		startInodeNum = HelpWalkPath(pNext, startInodeNum, &pNext);
		if (startInodeNum == -ENOENT)
		{
			perror("Failed to locate file.\n");
			return -ENOENT;
		}
	}
	free(pCopy);
	// 读出 Inode
	FILE *fp = fopen(imgPath, "r+");
	if (fp == NULL)
	{
		perror("Failed to open Imgdisk.\n");
		return -ENOENT;
	}
	struct inode *ptrInode = malloc(sizeof(struct inode));
	fseek(fp, getInodeOffsetByNum(startInodeNum), SEEK_SET);
	fread(ptrInode, sizeof(struct inode), 1, fp);

	if (ReadCache == NULL) { 
		// 建立缓冲区
		int length = ptrInode->st_size - sizeof(struct fileObj);
		ReadCache = malloc(length);
		if(ReadCache == NULL) {
			perror("No more Memory.\n");
			return -EOF;
		}
		memset(ReadCache, 0, length);

		// 获取 所有 文件块
		AddrNode *pAddrHead = HelpWalkInodeTable(fp, ptrInode);
		int curInodeIndex = -1;
		int res = -1;

		struct fileObj *ptrFileObj = malloc(sizeof(struct fileObj));
		int IsFirstBlock = 1;

		char *ptrBuf = ReadCache; // 定义读写指针
		int NeedRead = ptrInode->st_size - sizeof(struct fileObj);
		int read_size = 0;

		for (AddrNode *pCurrent = pAddrHead; pCurrent != NULL;) {
			res = ReadAddrList(&pCurrent, &curInodeIndex);
			if (res == -1)
			{
				break;
			}
			// res 为文件内容磁盘地址块号
			if(fseek(fp, getDataOffsetByNum(res), SEEK_SET) != 0) {
				perror("Cannot seek imgdisk.\n");
			}

			if (IsFirstBlock)
			{ // 读取文件头
				IsFirstBlock = 0;
				fread(ptrFileObj, sizeof(struct fileObj), 1, fp);
				if (ptrFileObj->checksum != HelpGenFileObjHeadChecksum(ptrFileObj))
				{ // 为目录
					free(ptrInode);
					free(ptrFileObj);
					fclose(fp);
					FreeAddrList(pAddrHead);
					return -EISDIR;
				}
				printf("SFS_read\tReading file: %s.%s\n", ptrFileObj->fileName, ptrFileObj->postFix); // 简单的调试输出(若无扩展名可能不美观，当然可以改用HelpConcatFileName辅助函数，此处为了简单)
			}
			else {
				// 读取文件具体内容
				while(1) {
					int modValue =  NeedRead % fs_bl_size; // 以块为周期(整体)
					// printf("%d\n", modValue);
					int rs = fread(ptrBuf, modValue, 1, fp); 
					if(rs != 1) {
						perror("Fail happens when reading.\n");
					}
					read_size += rs * modValue;
					printf("Read get: %s\n", ptrBuf);
					if(read_size >= NeedRead) { // 保证不超出文件大小
						ReadFinish = 1;
						break;
					}
					ptrBuf += read_size; // 向后移动指针
				}
			}
		}
		// 清理
		free(ptrFileObj);
		FreeAddrList(pAddrHead);
		// ReadCache[NeedRead]
	} 
	
	memset(buf, 0, size);
	int coreSize = ptrInode->st_size - sizeof(struct fileObj);

	memcpy(buf, ReadCache + offset, size - sizeof(struct fileObj));
	if((offset + 1)*Native_block_size >= coreSize) {
		free(ReadCache);
		ReadCache = NULL;
		ReadFinish = 0;
	}

	
	int returnValue = (size > coreSize)? coreSize : size;
	// 清理
	free(ptrInode);
	fclose(fp);
	
	fflush(stdout);

	// 返回读取的文件大小
	return returnValue;
}

/*注意：为了简化实现，不支持存在与直接父目录同名的子目录。若实在有必要，可考虑建立中间过渡目录。
辅助函数： 支持多级目录下的文件夹创建，但不可递归创建。
另外，为了与Regular File区分，参照常见文件系统，不支持目录扩展名，"."后将视为filename而非postfix部分。
但dentry结构保留了相关结构，有需要可进行扩展(利用代码中的HelpConcatFileName等接口可以很简单地实现)*/
int SFS_mkdir(const char *path, mode_t mode)
{
	// 为了提高文件系统的便捷性，借鉴Shell的mkdir命令，此处设计为直接使用mkdir命令创建子目录。
	printf("mkdir path: %s\n", path);
	
	// 分割pwd与需要新建的目录路径, *p 指向需要新建的目录名字
	char *p = strrchr(path, '/');
	*p = '\0'; // 截断
	p += 1;

	// 恢复父目录开头的"/"符号
	char *parentPath = malloc((strlen(path) + 1) * sizeof(char));
	strcpy(parentPath,"/");
	strcat(parentPath, path);


	printf("Mkdir Parent path: %s\n", parentPath);
	printf("Mkdir Child path: %s\n", p);

	// 下行代码参考Linux内核源码风格，既无需定义新变量，也具有扩展性
	if(strlen(p) > sizeof( ((struct dentry*)0)->fileName ) - 1 ) {
		perror("Length of filename overflow.\n");
		return -ENAMETOOLONG;
	}

	// 由于需要实现多级目录，故不要求在根目录下运行此命令
	// 读取pwd的inode
	int startInodeNum = 1;
	for(char* pNext = parentPath; !IsReachPathEnd(pNext);) {
		startInodeNum = HelpWalkPath(pNext, startInodeNum, &pNext);
		if(startInodeNum == -ENOENT) {
			perror("Pwd Error.\n");
			free(parentPath);
			return -ENOENT;
		}
	}
	free(parentPath);

	int iNodeOffset = getInodeOffsetByNum(startInodeNum);
	FILE* fp = fopen(imgPath, "r+"); // rw模式
	if(fp == NULL) {
		perror("Open ImgDisk failed.\n");
		return -ENOENT;
	}
	fseek(fp, iNodeOffset, SEEK_SET);
	struct inode* ptrInode = malloc(sizeof(struct inode));
	fread(ptrInode, sizeof(struct inode), 1, fp);

	fseek(fp, getDataOffsetByNum(ptrInode->addr[0]), SEEK_SET);
	struct dentry* ptrEntry = malloc(sizeof(struct dentry));
	fread(ptrEntry, sizeof(struct dentry), 1, fp);

	printf("mkdir pwd: %s\n", ptrEntry->fileName);
	// 判断目录是否已存在
	char tempFilename[256];
	memset(tempFilename, '\0', sizeof(tempFilename));
	for(int i=0; i < max_child_count; i++) {
		if(ptrEntry->childInodeNo[i] != 0) {
			HelpGetFileNameFromInodeNum(ptrEntry->childInodeNo[i], tempFilename);
			if(strcmp(tempFilename, p) == 0) { // 目录已存在
				perror("Directory existed.\n");
				return -EEXIST;
			}
		}
	}
	
	int available = -1; // 标记可用子目录标号
	for(int i = 0; i < max_child_count; i++) {
		if(ptrEntry->childInodeNo[i] < 1) {
			printf("Available Child position: %d\n", i);
			available = i;
			break;
		}
	}
	if(available == -1) { // 超过当前目录最大目录数量
		perror("Number of directories overflow.\n");
		return -ENOENT;
	}
	
	// 读取 inode 位图 与 数据位图
	struct bitmap_inode* ptrBi = malloc(sizeof(struct bitmap_inode));
	struct bitmap_dblock* ptrBd = malloc(sizeof(struct bitmap_dblock));

	fseek(fp, getInodeBitmapOffset(), SEEK_SET);
	fread(ptrBi, sizeof(struct bitmap_inode),1, fp);
	fseek(fp, getDataBitmapOffset(), SEEK_SET);
	fread(ptrBd, sizeof(struct bitmap_dblock), 1, fp);

	// 暴力算法：获取可用inode号与可用数据区号
	int avail_InodeNo = 0;
	int avail_dataBlockNo = 0;
	for(int i = 1; i <= inode_count; i++) {
		if(getBitmapValue(ptrBi, i) == 0) {
			avail_InodeNo = i;
			break;
		}
	}
	for(int i = 1; i <= dataBlock_count; i++) {
		if(getBitmapValue(ptrBd, i) == 0) {
			avail_dataBlockNo = i;
			break;
		}
	}
	if(avail_InodeNo == 0 || avail_dataBlockNo == 0) {
		perror("No available Block.\n");
		free(ptrBi);
		free(ptrBd);
		free(ptrInode);
		free(ptrEntry);
		fclose(fp);
		return -ENOENT;
	}

	printf("Available num:\tInode: %d\t DataBlock: %d\n", avail_InodeNo, avail_dataBlockNo);

	// 写入inode与dentry
	struct inode* ptrNewInode = malloc(sizeof(struct inode));
	struct dentry* ptrNewEntry = malloc(sizeof(struct dentry));
	
	strcpy(ptrNewEntry->fileName, p);
	strcpy(ptrNewEntry->postFix, "");
	ptrNewEntry->inodeNo = avail_InodeNo;
	for(int i =0; i < max_child_count; i++) {
		ptrNewEntry->childInodeNo[i] = 0;
	}
	
	// ptrNewInode->st_atim;
	ptrNewInode->addr[0] = avail_dataBlockNo;
	timespec_get(&ptrNewInode->st_atim, TIME_UTC);
	ptrNewInode->st_mode = mode | __S_IFDIR; // __S_IFDIR 赋予目录属性

	// 继承父目录属性
	ptrNewInode->st_gid = ptrInode->st_gid;
	ptrNewInode->st_uid = ptrInode->st_uid;
	ptrNewInode->st_ino = avail_InodeNo;
	ptrNewInode->st_nlink = 1;
	ptrNewInode->st_size = sizeof(struct dentry);

	fseek(fp, getInodeOffsetByNum(ptrNewInode->st_ino), SEEK_SET);
	fwrite(ptrNewInode, sizeof(struct inode), 1, fp);

	fseek(fp, getDataOffsetByNum(ptrNewInode->addr[0]), SEEK_SET);
	fwrite(ptrNewEntry, sizeof(struct dentry), 1, fp);


	// 修改父目录
	ptrEntry->childInodeNo[available] = ptrNewInode->st_ino;
	fseek(fp, getDataOffsetByNum(ptrInode->addr[0]), SEEK_SET);
	fwrite(ptrEntry, sizeof(struct dentry), 1, fp);

	// 修改位图
	setBitmapValue(ptrBi, avail_InodeNo, 1);
	setBitmapValue(ptrBd, avail_dataBlockNo, 1);
	
	fseek(fp, getInodeBitmapOffset(), SEEK_SET);
	fwrite(ptrBi, sizeof(struct bitmap_inode), 1, fp);

	fseek(fp, getDataBitmapOffset(), SEEK_SET);
	fwrite(ptrBd, sizeof(struct bitmap_dblock), 1, fp);

	// 释放资源
	free(ptrNewInode);
	free(ptrNewEntry);
	free(ptrBi);
	free(ptrBd);
	free(ptrInode);
	free(ptrEntry);
	fclose(fp);
	return 0;
}

int SFS_rmdir(const char *path)
{
	printf("Rmdir path: %s\n", path);
	int startInodeNum = 1;
	for(char* pNext = path; !IsReachPathEnd(pNext);) {
		startInodeNum = HelpWalkPath(pNext, startInodeNum, &pNext);
		if(startInodeNum == -ENOENT) {
			return -ENOENT;
		}
	}
	FILE* fp = fopen(imgPath, "r+");
	if(fp == NULL) {
		perror("Cannot open ImgFile");
		return -ENOENT;
	}
	
	// 读取目录Inode
	struct inode* ptrInode = malloc(sizeof(struct inode));
	fseek(fp, getInodeOffsetByNum(startInodeNum), SEEK_SET);
	fread(ptrInode, sizeof(struct inode), 1, fp);

	if(ptrInode->addr[0] < 1) { // 非法磁盘地址
		return -ENOENT;
	}

	// 判断Inode指向的是否为目录
	struct fileObj* ptrfileObj = malloc(sizeof(struct fileObj));
	fseek(fp, getDataOffsetByNum(ptrInode->addr[0]),SEEK_SET);
	fread(ptrfileObj, sizeof(struct fileObj), 1, fp);

	if(ptrfileObj->checksum == HelpGenFileObjHeadChecksum(ptrfileObj)) {
		// 普通文件非目录
		free(ptrfileObj);
		free(ptrInode);
		fclose(fp);
		return -ENOTDIR;
	}

	struct dentry* ptrEntry = malloc(sizeof(struct dentry));
	fseek(fp, getDataOffsetByNum(ptrInode->addr[0]),SEEK_SET);
	fread(ptrEntry, sizeof(struct dentry), 1, fp);

	// 检查是否目录是否为空
	for(int i = 0; i < max_child_count; i++) {
		if(ptrEntry->childInodeNo[i] > 0) {
			free(ptrfileObj);
			free(ptrInode);
			free(ptrEntry);
			fclose(fp);
			return -ENOTEMPTY;
		}
	}

	// 删除目录： 由于目录仅占用一个块，删除一个空目录比较简单
	// 标记位图为0
	struct bitmap_inode* ptrBi = malloc(sizeof(struct bitmap_inode));
	struct bitmap_dblock* ptrDb = malloc(sizeof(struct bitmap_dblock));

	fseek(fp, getInodeBitmapOffset(), SEEK_SET);
	fread(ptrBi, sizeof(struct bitmap_inode), 1, fp);

	fseek(fp, getDataBitmapOffset(), SEEK_SET);
	fread(ptrDb, sizeof(struct bitmap_dblock), 1, fp);

	setBitmapValue(ptrBi, ptrInode->st_ino, 0);
	setBitmapValue(ptrDb, ptrInode->addr[0], 0);
	
	fseek(fp, getInodeBitmapOffset(), SEEK_SET);
	fwrite(ptrBi, sizeof(struct bitmap_inode), 1, fp);

	fseek(fp, getDataBitmapOffset(), SEEK_SET);
	fwrite(ptrDb, sizeof(struct bitmap_dblock), 1, fp);

	free(ptrBi);
	free(ptrDb);

	// 删除Inode节点，由于空目录数据较小，也一并置零清除
	fseek(fp, getDataOffsetByNum(ptrInode->addr[0]), SEEK_SET);
	memset(ptrEntry, 0, sizeof(struct dentry));
	fwrite(ptrEntry, sizeof(ptrEntry), 1, fp);

	fseek(fp, getInodeOffsetByNum(ptrInode->st_ino), SEEK_SET);
	memset(ptrInode, 0, sizeof(struct inode));
	fwrite(ptrInode, sizeof(struct inode), 1, fp);

	// 修改父目录的信息
	char* p = malloc(sizeof(char) * strlen(path));
	char* pSave = p; // 保存p，用于释放内存
	memset(p, '\0', sizeof(char) * strlen(path));
	strcpy(p, path);
	char* t = strrchr(p, '/');
	*t = '\0'; // 此时p为父目录路径
	// 处理根目录情况
	if(strcmp(p, "") == 0) {
		strcat(p, "/");
	}
	int start = 1;
	for(;!IsReachPathEnd(p);) {
		start = HelpWalkPath(p, start, &p);
		if(start == -ENOENT) {
			// 此情况理论上不存在
			perror("Parent directory not found.\n");
			return -ENOENT;
		}
	}
	fseek(fp, getInodeOffsetByNum(start), SEEK_SET);
	fread(ptrInode, sizeof(struct inode), 1, fp);
	fseek(fp, getDataOffsetByNum(ptrInode->addr[0]), SEEK_SET);
	fread(ptrEntry, sizeof(struct dentry), 1, fp);
	for(int i = 0; i < max_child_count; i++) {
		if(ptrEntry->childInodeNo[i] == startInodeNum) {
			ptrEntry->childInodeNo[i] = 0;
			// break; // 若存在多个链接情况，多个子目录对应同一inode 情况可能存在，不应break
		}
	}
	fseek(fp, getDataOffsetByNum(ptrInode->addr[0]), SEEK_SET);
	fwrite(ptrEntry, sizeof(struct dentry), 1, fp);

	// 清理
	free(pSave);
	free(ptrfileObj);
	free(ptrEntry);
	free(ptrInode);
	fclose(fp);
	return 0;
}

/*注意：支持使用 mknod filename c 0 0 创建字符设备文件。
由于实现了 .create 故touch命令将调用 SFS_create */
int SFS_mknod(const char *path, mode_t mode,dev_t rdev)
{
	return HelpCreateFile(path, mode, __S_IFCHR);
}

/*支持通过 touch filename 命令创建普通文件，同时支持touch命令修改文件时间
也可通过输出重定向方式创建普通文件*/
int SFS_create(const char *path, mode_t mode, struct fuse_file_info *)
{
	// 先删除文件
	char* pCopy = malloc(strlen(path) + 1);
	pCopy[0] = '\0';
	strcpy(pCopy, path);
	SFS_unlink(pCopy);

	return HelpCreateFile(path, mode, __S_IFREG);
}


int HelpCreateFile(const char *path, mode_t mode, mode_t attach_mode)
{
	printf("SFS_mknod is called.\tpath: %s\n", path);
	int count = 0;
	for(char *temp = path; *temp!='\0'; temp++) {
		if(*temp == '/') {
			count++;
		}
	}
	if(count <= 1) {
		//直接在根目录创建文件，不应该赋予权限
		return -EPERM;
	}

	// 分离文件名与父目录名
	char* p = malloc(sizeof(char) * (strlen(path) + 1));
	memset(p, '\0', (strlen(path)+ 1)* sizeof(char));
	
	//恢复开头的"/"(此步似乎不需要，可考虑删除)
	strcpy(p, "/");
	strcat(p, path);
	char* pos_split = strrchr(p, '/');
	*pos_split = '\0';
	char* child = pos_split + 1;

	printf("parent: %s\t child: %s\n", p, child);

	// 下行代码参考Linux内核源码风格，既无需定义新变量，也具有扩展性
	//分离扩展名
	char *tempSplitPos = strrchr(child, '.');
	int lenfile,lenposfix;
	if(tempSplitPos != NULL) { // 存在扩展名
		lenfile = tempSplitPos - child;
		lenposfix = strlen(tempSplitPos + 1);
	} else { // 扩展名为空
		lenfile = strlen(child);
		lenposfix = 0;
	}
	
	if(lenfile > sizeof( ((struct fileObj*)0)->fileName ) - 1 || lenposfix > sizeof(((struct fileObj*)0)->postFix) -1 ) {
		perror("Length of filename overflow.\n");
		return -ENAMETOOLONG;
	}

	//查找父目录Inode
	int startInodeNum = 1;
	char *pCopy = malloc(strlen(p));
	memset(pCopy, '\0', sizeof(char)* strlen(p));
	strcpy(pCopy, p);
	
	for(char* pNext = pCopy; !IsReachPathEnd(pNext);) {
		startInodeNum = HelpWalkPath(pNext, startInodeNum, &pNext);
		if(startInodeNum == -ENOENT) {
			free(pCopy);
			perror("Parent Dirctory Not Found.\n");
			return -ENOENT;
		}
	}
	printf("Parent Directory Inode: %d\n", startInodeNum);
	FILE* fp = fopen(imgPath, "r+");
	if(fp == NULL) {
		perror("Img Open Failed.\n");
		free(pCopy);
		return -ENOENT;
	}

	fseek(fp, getInodeOffsetByNum(startInodeNum), SEEK_SET);
	struct inode* ptrInode = malloc(sizeof(struct inode));
	fread(ptrInode, sizeof(struct inode), 1, fp);
	
	fseek(fp, getDataOffsetByNum(ptrInode->addr[0]), SEEK_SET);
	struct dentry* ptrEntry = malloc(sizeof(struct dentry));
	fread(ptrEntry, sizeof(struct dentry), 1, fp);

	// 查找可用的子文件位置
	int available = -1;
	for(int i=0; i < max_child_count;i++) {
		if(ptrEntry->childInodeNo[i] == 0) {
			available = i;
			break;
		}
	}
	if(available == -1) {
		perror("No available position for sub.\n");
		free(p);
		free(ptrInode);
		free(ptrEntry);
		fclose(fp);
		return -EBADF; // fuse框架似乎不会对mknod命令检测该返回值，故无法提示用户超过最大子文件数量
	}

	// 读取 inode 位图 与 数据位图
	struct bitmap_inode* ptrBi = malloc(sizeof(struct bitmap_inode));
	struct bitmap_dblock* ptrBd = malloc(sizeof(struct bitmap_dblock));

	fseek(fp, getInodeBitmapOffset(), SEEK_SET);
	fread(ptrBi, sizeof(struct bitmap_inode),1, fp);
	fseek(fp, getDataBitmapOffset(), SEEK_SET);
	fread(ptrBd, sizeof(struct bitmap_dblock), 1, fp);

	// 暴力算法：获取可用inode号与可用数据区号
	int avail_InodeNo = 0;
	int avail_dataBlockNo = 0;
	for(int i = 1; i <= inode_count; i++) {
		if(getBitmapValue(ptrBi, i) == 0) {
			avail_InodeNo = i;
			break;
		}
	}
	for(int i = 1; i <= dataBlock_count; i++) {
		if(getBitmapValue(ptrBd, i) == 0) {
			avail_dataBlockNo = i;
			break;
		}
	}
	if(avail_InodeNo == 0 || avail_dataBlockNo == 0) {
		perror("No available Block.\n");
		free(ptrBi);
		free(ptrBd);
		free(ptrInode);
		free(ptrEntry);
		fclose(fp);
		return -ENOENT;
	}

	printf("Available num:\tInode: %d\t DataBlock: %d\n", avail_InodeNo, avail_dataBlockNo);

	// 写入Inode 与文件头
	struct inode* ptrNewInode = malloc(sizeof(struct inode));
	struct fileObj* ptrNewObj = malloc(sizeof(struct fileObj));

	//分离扩展名
	char *safeSplit = strrchr(child, '.');
	memset(ptrNewObj->fileName, '\0', sizeof(ptrNewObj->fileName));
	memset(ptrNewObj->postFix, '\0', sizeof(ptrNewObj->postFix));

	printf("Child Name: %s\n", child);
	if(safeSplit == NULL) { //文件无扩展名
		strcpy(ptrNewObj->fileName, child);
		memset(ptrNewObj->postFix, '\0', sizeof(ptrNewObj->postFix));
	} else {
		strncpy(ptrNewObj->fileName,child, safeSplit - child);
		strcpy(ptrNewObj->postFix, safeSplit + 1);
	}
	ptrNewObj->checksum = HelpGenFileObjHeadChecksum(ptrNewObj);

	printf("Filename: %s Postfix: %s checksum: %d\n", ptrNewObj->fileName, ptrNewObj->postFix, ptrNewObj->checksum);
	
	ptrNewInode->addr[0] = avail_dataBlockNo;
	timespec_get(&ptrNewInode->st_atim, TIME_UTC);
	ptrNewInode->st_mode = mode | attach_mode; // 赋予 __S_IFREG 或 __ S_IFCHR 权限，以通过权限检测
	ptrNewInode->st_ino = avail_InodeNo;

	// 继承父目录属性
	ptrNewInode->st_gid = ptrInode->st_gid;
	ptrNewInode->st_uid = ptrInode->st_uid;
	ptrNewInode->st_nlink = 1;
	ptrNewInode->st_size = sizeof(struct fileObj); // 文件初始大小为文件头大小

	fseek(fp, getInodeOffsetByNum(ptrNewInode->st_ino), SEEK_SET);
	fwrite(ptrNewInode, sizeof(struct inode), 1, fp);

	fseek(fp, getDataOffsetByNum(ptrNewInode->addr[0]), SEEK_SET);
	fwrite(ptrNewObj, sizeof(struct fileObj), 1, fp);

	// 修改父目录
	ptrEntry->childInodeNo[available] = ptrNewInode->st_ino;
	fseek(fp, getDataOffsetByNum(ptrInode->addr[0]), SEEK_SET);
	fwrite(ptrEntry, sizeof(struct dentry), 1, fp);

	// 修改位图
	setBitmapValue(ptrBi, avail_InodeNo, 1);
	setBitmapValue(ptrBd, avail_dataBlockNo, 1);
	
	fseek(fp, getInodeBitmapOffset(), SEEK_SET);
	fwrite(ptrBi, sizeof(struct bitmap_inode), 1, fp);

	fseek(fp, getDataBitmapOffset(), SEEK_SET);
	fwrite(ptrBd, sizeof(struct bitmap_dblock), 1, fp);

	
	// 释放资源
	free(p);
	free(ptrBi);
	free(ptrBd);
	free(pCopy);
	free(ptrInode);
	free(ptrEntry);
	free(ptrNewInode);
	free(ptrNewObj);
	fclose(fp);

	return 0;
}

/*提示：可通过shell 命令 echo "content your want" > file 进行写入*/
int SFS_write(const char *path, const char *buf, size_t size, off_t off, struct fuse_file_info *fi)
 {
	printf("Write path: %s\n", path);
	
	FILE *fp = fopen(imgPath, "r+");
	if(fp == NULL) {
		perror("Failed to open ImgDisk.\n");
		return -ENOENT;
	}
	int startInodeNum = 1;
	for(char*pNext = path;!IsReachPathEnd(pNext);) {
		startInodeNum = HelpWalkPath(pNext, startInodeNum, &pNext);
		if(startInodeNum == -ENOENT) {
			perror("Failed to find the file\n");
			return -ENOENT;
		}
	} // 此后startInodeNum 保存了文件的Inode号

	// 读出文件的 Inode
	struct inode* ptrInode = malloc(sizeof(struct inode));
	// struct fileObj* ptrFileObj = malloc(sizeof(struct fileObj)); // 由于目前仅修改文件内容，故不考虑文件头部数据
	fseek(fp, getInodeOffsetByNum(startInodeNum), SEEK_SET);
	fread(ptrInode, sizeof(struct inode), 1, fp);

	// 接下来是写入数据，从ptrInode->addr[1]开始
	char* pWrite = malloc(fs_bl_size);
	char* curPointer = buf;
	int writeSize = size;
	AddrNode* pHead = CreateListHead();
	AddrNode* pCurrent = pHead;
	int curIndex = 0;

	while(1) {
		int s = writeSize % fs_bl_size;
		if(s <= 0) {
			break;
		}
		int availableNum = HelpAllocDataBlock(fp, &pCurrent, &curIndex);
		memset(pWrite, 0, fs_bl_size);
		memcpy(pWrite, curPointer, s);
		fseek(fp, getDataOffsetByNum(availableNum), SEEK_SET);
		fwrite(pWrite, fs_bl_size, 1, fp);
		writeSize -= s;
	}
	

	HelpTidyAddr(fp, pHead, ptrInode);
	
	// ptrInode->st_size += size;
	ptrInode->st_size = sizeof(struct fileObj) + size;
	fseek(fp, getInodeOffsetByNum(ptrInode->st_ino), SEEK_SET);
	fwrite(ptrInode, sizeof(struct inode), 1, fp);

	
	// 清理
	fclose(fp);
	free(pWrite);
	FreeAddrList(pHead);
	free(ptrInode);

	return size;
 }

int SFS_unlink(const char *path)
{
	printf("Unlink path: %s\n", path);
	int pathLength = sizeof(char) * (strlen(path) + 1); // 保存目录长度
	
	int startInodeNum = 1;
	for(char* pNext = path; !IsReachPathEnd(pNext);) {
		startInodeNum = HelpWalkPath(pNext, startInodeNum, &pNext);
		if(startInodeNum == -ENOENT) {
			return -ENOENT;
		}
	}

	FILE* fp = fopen(imgPath, "r+");
	if(fp == NULL) {
		perror("Failed to open imgdisk.\n");
		return -ENOENT;
	}
	
	// 读Inode
	struct inode* ptrInode = malloc(sizeof(struct inode));
	struct fileObj * ptrFileObj = malloc(sizeof(struct fileObj));

	fseek(fp, getInodeOffsetByNum(startInodeNum), SEEK_SET);
	fread(ptrInode, sizeof(struct inode), 1, fp);

	fseek(fp, getDataOffsetByNum(ptrInode->addr[0]), SEEK_SET);
	fread(ptrFileObj, sizeof(struct fileObj), 1, fp);

	if(HelpGenFileObjHeadChecksum(ptrFileObj) != ptrFileObj->checksum) {
		//非文件，为目录
		free(ptrInode);
		free(ptrFileObj);
		fclose(fp);
		return -EISDIR;
	}

	// 分离得到父目录
	char *parent = malloc(pathLength);
	strcpy(parent, path);
	char *temp = strrchr(parent, '/');
	*temp = '\0';
	int anotherStartNum = 1;
	for(char* pParentNext = parent; !IsReachPathEnd(pParentNext); ) {
		anotherStartNum = HelpWalkPath(pParentNext, anotherStartNum, &pParentNext);
		if(anotherStartNum == -ENOENT) {
			return -ENOENT; //理论上此情况不会发生。因为文件存在必然父目录存在
		}
	} // 此后 anotherStartNum 为 父目录的Inode号

	// 获取使用的磁盘块，不清零，故在write前需要对Data位图标记为0的块进行清零，read不应该读取Data标记为0的块
	struct bitmap_inode* ptrBi = malloc(sizeof(struct bitmap_inode));
	struct bitmap_dblock* ptrDb = malloc(sizeof(struct bitmap_dblock));

	fseek(fp, getInodeBitmapOffset(), SEEK_SET);
	fread(ptrBi, sizeof(struct bitmap_inode), 1, fp);
	
	fseek(fp, getDataBitmapOffset(), SEEK_SET);
	fread(ptrDb, sizeof(struct bitmap_dblock), 1, fp);

	// 重新标记Inode 与 Data 位图
	AddrNode* addrListHead =  HelpWalkInodeTable(fp, ptrInode);
	int curIndex = -1;
	for(AddrNode* addrCur = addrListHead; addrCur!=NULL;) {
		int res = ReadAddrList(&addrCur, &curIndex);
		if(res == -1) {
			break;
		}
		setBitmapValue(ptrDb, res, 0);
	}

	setBitmapValue(ptrBi, ptrInode->st_ino, 0);

	fseek(fp, getInodeBitmapOffset(), SEEK_SET);
	fwrite(ptrBi, sizeof(struct bitmap_inode), 1, fp);
	
	fseek(fp, getDataBitmapOffset(), SEEK_SET);
	fwrite(ptrDb, sizeof(struct bitmap_dblock), 1, fp);
	
	// 重写父目录
	fseek(fp, getInodeOffsetByNum(anotherStartNum), SEEK_SET);
	struct inode* ptrParentInode = malloc(sizeof(struct inode));
	fread(ptrParentInode, sizeof(struct inode), 1, fp);

	fseek(fp, getDataOffsetByNum(ptrParentInode->addr[0]), SEEK_SET);
	struct dentry* ptrParentEntry = malloc(sizeof(struct dentry));
	fread(ptrParentEntry, sizeof(struct dentry), 1, fp);

	for(int i=0; i < max_child_count; i++) {
		if(ptrParentEntry->childInodeNo[i] == ptrInode->st_ino) {
			ptrParentEntry->childInodeNo[i] = 0;
		}
	}

	fseek(fp, getDataOffsetByNum(ptrParentInode->addr[0]), SEEK_SET);
	fwrite(ptrParentEntry, sizeof(struct dentry), 1, fp);

	// 清空文件 Inode 与 文件头(mknod不检测脏数据)
	fseek(fp, getDataOffsetByNum(ptrInode->addr[0]), SEEK_SET);
	char* empty = malloc(fs_bl_size);
	memset(empty, 0, fs_bl_size);
	fwrite(empty, fs_bl_size, 1 ,fp);

	fseek(fp, getInodeOffsetByNum(ptrInode->st_ino), SEEK_SET);
	fwrite(empty, sizeof(struct inode), 1, fp);
	
	// 清理
	free(empty);
	free(ptrParentEntry);
	free(ptrParentInode);
	free(ptrBi);
	free(ptrDb);
	FreeAddrList(addrListHead);
	free(parent);
	free(ptrInode);
	free(ptrFileObj);
	fclose(fp);
	return 0;
}

/*touch 命令修改文件时间*/
int SFS_utimens (const char *path, const struct timespec tv[2], struct fuse_file_info *fi)
{
	int startInodeNum = 1;
	for(char* pNext = path; !IsReachPathEnd(pNext);) {
		startInodeNum = HelpWalkPath(pNext, startInodeNum, &pNext);
		if(startInodeNum == -ENOENT) {
			return -ENOENT;
		}
	}

	FILE* fp = fopen(imgPath, "r+");
	if(fp == NULL) {
		perror("Cannot open Imgdisk.\n");
		return -ENOENT;
	}
	struct inode* ptrInode = malloc(sizeof(struct inode));
	fseek(fp, getInodeOffsetByNum(startInodeNum), SEEK_SET);
	fread(ptrInode, sizeof(struct inode), 1, fp);

	ptrInode->st_atim = tv[0];
	fseek(fp, getInodeOffsetByNum(startInodeNum), SEEK_SET);
	fwrite(ptrInode, sizeof(struct inode), 1, fp);

	fclose(fp);
	free(ptrInode);

	return 0;
}


static const struct fuse_operations hello_oper = {
	.init       = SFS_init,
	.getattr	= SFS_getattr,
	.readdir	= SFS_readdir,
	.open		= SFS_open,
	.read		= SFS_read,
    .mkdir      = SFS_mkdir,
    .rmdir      = SFS_rmdir,
    .mknod      = SFS_mknod,
	.create		= SFS_create,
	.utimens	= SFS_utimens,
    .write      = SFS_write,
    .unlink     = SFS_unlink
};

static void show_help(const char *progname)
{
    printf("Welcome to use sampleFS Developed by Krxk.\n");
	printf("usage: %s [options] <mountpoint>\n\n", progname);
	printf("File-system specific options:\n"
	       "    --name=<s>          Name of the \"hello\" file\n"
	       "                        (default: \"hello\")\n"
	       "    --contents=<s>      Contents \"hello\" file\n"
	       "                        (default \"Hello, World!\\n\")\n"
	       "\n");
}

int main(int argc, char *argv[])
{
	int ret;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	/* Set defaults -- we have to use strdup so that
	   fuse_opt_parse can free the defaults if other
	   values are specified */
	options.filename = strdup("/");
	options.contents = strdup("Hello SampleFS!\n");

	/* Parse options */
	if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
		return 1;

	/* When --help is specified, first print our own file-system
	   specific help text, then signal fuse_main to show
	   additional help (by adding `--help` to the options again)
	   without usage: line (by setting argv[0] to the empty
	   string) */
	if (options.show_help) {
		show_help(argv[0]);
		assert(fuse_opt_add_arg(&args, "--help") == 0);
		args.argv[0][0] = '\0';
	}

	ret = fuse_main(args.argc, args.argv, &hello_oper, NULL);
	fuse_opt_free_args(&args);
	return ret;
}

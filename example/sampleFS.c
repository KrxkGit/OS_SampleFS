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
	ptrFileObj->checksum = 1;

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
	FILE* fp = fopen(imgPath, "r");
	if(fp == NULL) {
		perror("OpenFile failed.\n");
		return;
	}
	// 读出Inode
	fseek(fp, getInodeOffsetByNum(inodeNum), SEEK_SET);
	struct inode* ptrInode = malloc(sizeof(struct inode));
	fread(ptrInode, sizeof(struct inode), 1, fp);

	// 读出 data
	struct fileObj* ptrFileObj = malloc(sizeof(struct fileObj));
	fseek(fp, getDataOffsetByNum(ptrInode->addr[0]), SEEK_SET);
	fread(ptrFileObj, sizeof(struct fileObj), 1, fp);
	if(HelpGenFileObjHeadChecksum(ptrFileObj) == ptrFileObj->checksum) { // 普通文件
		HelpConcatFileName(ptrFileObj->fileName, ptrFileObj->postFix, pFileName);
		free(ptrFileObj);
	} else { // 目录
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
	printf("HelpFindFile is called\t fileName: %s\n", fileName);
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
		printf("HelpFindFile:\tFind Common File\n");
		fclose(fp);
		HelpConcatFileName(ptrfileObj->fileName, ptrfileObj->postFix, concat);
		if(strcmp(concat, fileName) == 0) { // 找到了
			return ptrInode->st_ino;
		} else {
			return -ENOENT;
		}
	} else { // startInodeNum 为目录,查找目录下一层深度下的所有文件
		fseek(fp, -sizeof(struct fileObj), SEEK_CUR);
		struct dentry* ptrEntry = malloc(sizeof(struct dentry));
		fread(ptrEntry, sizeof(struct dentry), 1, fp);
		HelpConcatFileName(ptrEntry->fileName, ptrEntry->postFix, concat);
		fclose(fp);

		if(strcmp(concat, fileName) == 0) { // 找到了
			return ptrInode->st_ino;
		} else {
			if(goDeep == 0) {
				return -ENOENT;
			}
			for(int i=0 ;i < max_child_count;i++) {
				printf("HelpFindFile recursion\n");
				if(HelpFindFile(fileName, ptrEntry->childInodeNo[i], 0)==ptrEntry->childInodeNo[i]) {
					// 成功找到
					return ptrEntry->childInodeNo[i];
				}
				return -ENOENT;
			}
		}
	}
}

void HelpSplitFileName(const char* customPath, const char* outSplitFileName)
{
	char* p = customPath;
	char* end = strchr(p, '/');
	if(end == NULL) {
		end = &customPath[strlen(customPath) - 1] + 2;
		*end = '\0';
	}
	strncpy(outSplitFileName, p, end-p);
	printf("HelpSplitFileName:\tSplitFileName: %s\n", outSplitFileName);
}

int IsReachPathEnd(char* pNext)
{
	/*检查路径是否结束*/
	printf("IsReachPathEnd\tpNext:%s\n", pNext);
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
注意： 这表明 路径的末尾不应该加"/"，否则将引起报错 "*/
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
	printf("stbuf->st_ino:%d\n",stbuf->st_ino);
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

	// 查找目录的Inode
	int startInodeNum = 1;
	for(char *pNext = path; IsReachPathEnd(pNext);) {
		startInodeNum = HelpWalkPath(pNext, startInodeNum, &pNext);
		if(startInodeNum == -ENOENT) {
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
		free(ptrInode);
		fclose(fp);
		return -ENOENT;
	}
	int dataOff = getDataOffsetByNum(ptrInode->addr[0]);
	struct dentry* ptrEntry = malloc(sizeof(struct dentry));
	fseek(fp, dataOff, SEEK_SET);
	fread(ptrEntry, sizeof(struct dentry), 1 ,fp);

	char fileName[256];
	memset(fileName, 0, sizeof(fileName));
	for(int i = 0; i < max_child_count; i++) {
		if(ptrEntry->childInodeNo[i] > 0) {
			HelpGetFileNameFromInodeNum(ptrEntry->childInodeNo[i], fileName);
			filler(buf, fileName, NULL, 0, 0);
		}
	}


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
	printf("SFS_open is called.\tpath: %s\n", path);
	if (strcmp(path+1, options.filename) != 0)
		return -ENOENT;

	if ((fi->flags & O_ACCMODE) != O_RDONLY)
		return -EACCES;

	return 0;
}

static int SFS_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	printf("SFS_Read is called.\tpath: %s\n", path);
	size_t len;
	(void) fi;
	if(strcmp(path+1, options.filename) != 0)
		return -ENOENT;

	len = strlen(options.contents);
	if (offset < len) {
		if (offset + size > len)
			size = len - offset;
		memcpy(buf, options.contents + offset, size);
	} else
		size = 0;

	return size;
}

int SFS_mkdir(const char *path, mode_t mode)
{
	printf("Mkdir path: %s\n", path);
	return 0;
}

int SFS_rmdir(const char *path)
{
	printf("Rmdir path: %s\n", path);
	return 0;
}

int SFS_mknod(const char *path, mode_t mode,dev_t rdev)
{
	printf("Mknod path: %s\n", path);
	return 0;
}

int SFS_write(const char *path, const char *buf, size_t size, off_t off, struct fuse_file_info *fi)
 {
	printf("Write path: %s\n", path);
	return 0;
 }

int SFS_unlink(const char *path)
{
	printf("Unlink path: %s\n", path);
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

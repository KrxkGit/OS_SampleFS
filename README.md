# 🎉SampleFS 使用说明

## ▶重新构建

由于 *SampleFS* 管理的磁盘依赖于绝对路径，为了能够在其他设备使用本系统，需要重新构建本程序。

### 1️⃣修改磁盘文件路径

```sh
cd OS_SampleFS/example
vim sampleFS.h
```

修改位于第14行的磁盘路径：

```c
char imgPath[] = "/home/krxk/fuse-3.16.2/build/example/SFS_Img"; // 设备载体
```

### 2️⃣构建

回到根目录，执行

```sh
cd OS_SampleFS/build
ninja
```

即可重新编译构建。

生成输出文件位于 build/example中。

## ✅帮助文档

### 1️⃣创建磁盘文件

执行

```sh
cd build/example
./CreateDiskFile.sh
```

创建磁盘文件*SFS_Img*

### 2️⃣初始化文件系统

执行

```sh
./init_disk
```

初始化文件系统。

### ✔SampleFS 支持的操作

#### ❔查看帮助

运行

```sh
./sampleFS --help
```

可查看使用文件系统使用说明。

#### 🟣挂载文件系统

在源代码根目录下，执行

```sh
cd build/example
mkdir Krxk # Krxk 可以是任意挂载的目标目录
./sampleFS Krxk
```

卸载文件系统可执行

```sh
umount Krxk
```

#### 🆗支持的文件操作

❕注意：文件名长度不超过8，扩展名长度不超过3。

```sh
# 查看文件属性
stat filename

# 查看目录
ls dirname
ls -a .

# 创建多层目录
mkdir child
mkdir child2
cd child
mkdir ch_ch
cd ch_ch
mkdir chchch

# 创建文件
echo "HelloWorld" > filename
touch filename
mknod filename c 0 0

# 删除空目录
rmdir empty_dirname

# 删除文件
rm filename
rm *.txt # 支持通配符形式删除文件

# 写入文件 & 查看文件内容
echo "Hello" > 1.txt
cat 1.txt

# 修改文件时间
touch -d "2023-8-20 16:00" filename
```

🟡暂未支持的操作：在文件末尾追加内容，文件的写入采取从头覆盖写入的方式。
说明：这个是GL-MT300A的uboot，请使用32位虚拟机编译
========
## 1 编译前的准备  

#### 1.1 安装必须的软件包
```shell
  $ sudo apt-get update
  $ sudo apt-get install g++ libghc-zlib-dev liblzma-dev ncurses-dev git
```

#### 1.2 同步代码    
```shell
  $ git clone git@192.168.11.11:GL-MT300A/GL-MT300A-uboot.git
```

#### 1.3 安装SDK提供软件包
```shell
  $ sudo tar -xjvf toolchain/buildroot-gcc463_32bits.tar.bz2 -C /opt/
  $ sudo tar -jxvf toolchain/buildroot-gcc342.tar.bz2 -C /opt/
```

######  Install LZMA Utility（交叉编译工具链）
```shell
  $ cd toolchain
  $ tar -zxvf lzma-4.32.7.tar.gz
  $ cd lzma-4.32.7/
  $ ./configure
  $ make
  $ sudo make install
  $ cd ../
```

######  Install mksquashfs utility（压缩rootfs文件系统）
```shell
  $ tar -xjvf squashfs4.2.tar.bz2
  $ cd squashfs4.2/squashfs-tools/
  $ make
  $ sudo cp mksquashfs /opt/buildroot-gcc463/usr/bin/mksquashfs_lzma-4.2		
  $ cd ../../../
```
#### 1.4 MT7628官方文档说明
    在doc目录下有具体的MT7628文档，有兴趣的可以参考！

## 2 编译uboot  

#### 2.1 配置uboot
```shell
  $ make menuconfig
     Chip Type ---> ASIC
     Chip ID ---> MT7628
     DRAM Type ---> DDR2
     DDR Component ---> 1024Mb
     Ram/Rom version ---> ROM
  配置完毕，保存退出！
```		

	
#### 2.2 编译uboot
```shell
  $ make
  编译OK会在根目录下生成boot.bin镜像
``` 
   
## 3 开发流程

### 3.1  每天开发的时候，先pull一下最新代码
```shell
  $ git pull
```

### 3.2 每天开发结束的时候，要push一下代码
##### 修改代码后，将修改文件信息添加到索引库中(`注意：编译过的工程不能使用-A参数，要指定详细文件`)
```shell
  $ git add xxx
```
##### 为修改记录添加修改注释
`注释参考（注释因人而异，本注释仅供参考，在保证可读性的同时，择优而用）`
```
  修改的:[MOD] 1.xxx
               2.xxx
  添加的:[ADD] 1.xxx
               2.xxx
  删除的:[DEL] 1.xxx
               2.xxx
  有BUG:[BUG]  1.xxx
               2.xxx
```
```shell
  $ git commit -m "xxxx"
  或者使用
  $ git commit -m '
  >[XXX] 1.xxx
  >      2.xxx
  >'
```
##### 将修改推送到服务器
```shell
  $ git push
```

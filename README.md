Bolt
====
Bolt - The Realtime Image Compress System
<hr />

介绍
----
&nbsp;&nbsp;&nbsp;&nbsp;Bolt是一个实时裁剪压缩图片服务器，其比nginx的image_filter快2倍以上，主要是因为Bolt对一张图片只做一次处理，就算在处理图片的过程中，其他的客户端也在请求此图片，Bolt也能保证只有一个线程在处理此图片。<br />

&nbsp;&nbsp;&nbsp;&nbsp;另外Bolt替换缓存机制，处理过的图片不再进行第二次处理，除非内存不足的时候，Bolt才会处理LRU算法来删除缓存中的图片，在启动Bolt的时候可以使用“--max-cache”启动参数来设置最大内存限制。Bolt使用LRU算法来淘汰缓存的图片，也就是说一般只会淘汰较少访问的图片，这就可以很好的限制Bolt的内存使用。

用在哪里
--------
Bolt可以用在内存和CPU都过剩的服务器，另外使用Bolt可以减少磁盘的使用，加快图片的加载速度。

安装
----
* 安装libevent (http://libevent.org/)
* 安装ImageMagick (http://www.imagemagick.org/script/index.php)
* 安装Bolt
```shell
$ git clone https://github.com/liexusong/bolt
$ cd bolt
$ make
```

使用方式
--------
访问URL：http://your-host/filename.jpg-(width)x(height)_(quality).(ext)

Bolt配置文件
--------------
* host = [str]          # 设置绑定的IP
* port = [int]          # 设置监听的端口
* workers = [int]       # 启动多少个worker线程(用于裁剪图片)
* logfile = [str]       # 日志文件输出的路径
* logmark = [str]       # 日志要显示的级别，可以选择(DEBUG|NOTICE|ALERT|ERROR)
* max-cache = [int]     # 设置Bolt可以使用的最大内存(单位为字节)
* gc-threshold = [int]  # GC要清理的阀值(也就是说GC会清理到max-cache的百分之多少停止，可选值为0 ~ 99)
* path = [str]          # 要进行裁剪的图片源路径
* watermark = [str]     # 水印图片路径
* daemon = [yes|no]     # 是否启动守护进程模式

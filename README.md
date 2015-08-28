Bolt
====
Bolt - The Realtime Image Compress System
<hr />

介绍
----
&nbsp;&nbsp;&nbsp;&nbsp;Bolt是一个实时裁剪压缩图片服务器，其比nginx的image_filter快2倍以上，主要是因为Bolt对一张图片只做一次处理，就算在处理图片的过程中，其他的客户端也在请求此图片，Bolt也能保证只有一个线程在处理此图片。<br />

&nbsp;&nbsp;&nbsp;&nbsp;另外Bolt替换缓存机制，处理过的图片不再进行第二次处理，除非内存不足的时候，Bolt才会处理LRU算法来删除缓存中的图片，在启动Bolt的时候可以使用“--max-cache”启动参数来设置最大内存限制。Bolt使用LRU算法来淘汰缓存的图片，也就是说一般只会淘汰较少访问的图片，这就可以很好的限制Bolt的内存使用。

Bolt
====
Bolt - The Realtime Image Compress System
<hr />

&nbsp;&nbsp;&nbsp;&nbsp;Bolt是一个实时裁剪压缩图片服务器，其比nginx的image_filter快2倍以上，主要是因为Bolt对一张图片只做一次处理，就算在处理图片的过程中，其他的客户端也在请求此图片，Bolt也能保证只有一个线程在处理此图片。<br />


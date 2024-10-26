## 1.配置Web服务

进入虚拟环境
```
conda activate django
```

安装django
```
pip install django
```

打开`PyCharm` cd到`myWeb`文件夹
```
cd myWeb
```

设置启动端口8080并启动项目
```
python manage.py runserver 0.0.0.0:8080
```

在浏览器中打开以下网址，就可以看到自己编写的HTML文档，并显示Web页面
```
http://127.0.0.1:8080
```
## 2.使用Wireshark分析HTTP交互过程

双击`Adapter for loopback traffic capture`（迂回路线，就是本机自己的网络，抓的是 127.0.0.1 的包），开始抓包。

应用显示过滤器进行数据包列表过滤

- 仅显示 8080 端口报文
```
tcp.port==8080
```

- 仅显示 HTTP 报文
```
http
```
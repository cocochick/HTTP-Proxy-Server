# 使用说明
请在linux环境下编译，大部分代码基于c++11,少部分基于c++17的内容为std::chrono_literals内相关重载。  
使用了多线程库，因此若使用g++编译，需要加上-pthread选项，其他编译器类似。  
默认端口号为12345  
仅支持HTTP代理  

# TODO List
~~线程池~~  
端口分配（现有程序接受超过一定连接后端口号就会超出使用范围从而出错）  
HTTPS支持  
更加完善的断开连接算法（目前只要超过1s没有读写数据即断开）
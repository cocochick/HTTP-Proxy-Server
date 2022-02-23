#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <assert.h>
#include <syslog.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <future>
#include <map>
#include <set>
#include <vector>
#include <fstream>
#include "http.h"
#include "thread_pool.h"

constexpr int MAX_SIZE = 64;
constexpr int BUFFER_SIZE = 512;

extern thread_pool th_pool;

class Proxy{
    using FD = int;
public:
    Proxy() = default;
    static void recv_header(FD hostfd);
    static void connect_to_server(FD hostfd, char* origin_package, char* website);
    static FD create_local_socket(const char* address, int port);
private:
    static bool inWhiteList(std::string server_address);
    static int exchange_key(FD hostfd, FD serverfd, int* pipefd);
};


void Proxy::recv_header(FD hostfd){
    char buffer[BUFFER_SIZE], origin_package[BUFFER_SIZE], website[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    memset(origin_package, 0, BUFFER_SIZE);
    memset(website, 0, BUFFER_SIZE);
    int data_read = recv(hostfd, buffer, BUFFER_SIZE, 0);
    assert(data_read != -1);
    HTTP_CODE result = parse_context(buffer, origin_package, website);
    if(result == NO_REQUEST){
        syslog(LOG_WARNING, "Get a NO_REQUEST, context is:%s\n", origin_package);
		return;
    }
    /*
    if(!inWhiteList(std::string{website})){
        close(hostfd);
        return;
    }
    */
    connect_to_server(hostfd, origin_package, website);
    close(hostfd);
}


void Proxy::connect_to_server(FD hostfd, char* origin_package, char* website){
	bool isHTTPS = false;
	char https_response[] = "HTTP/1.1 200 Connection established\r\n\r\n";
	FD server = create_local_socket("0.0.0.0", 0);
	char origin_website[BUFFER_SIZE];
	strcpy(origin_website, website);
	if(server < 0){
		syslog(LOG_ERR, "Socket create error\n");
		return;
	}
	sockaddr_in server_address;
	bzero(&server_address, sizeof(server_address));
    socklen_t length = sizeof(server_address);
	server_address.sin_family = AF_INET;
    if(strchr(website, ':') != nullptr){    //判断网址内是否存在端口号
        *(strchr(website, ':')) = '\0';
        hostent* serverinfo = gethostbyname(website);   //为空有两种情况：没查询到或本来就是IP地址，这里认为他一定是IP地址
        if(serverinfo == nullptr){
            inet_pton(AF_INET, website, &server_address.sin_addr);
        }
        else{
            server_address.sin_addr.s_addr = inet_addr(inet_ntoa(*(in_addr*)serverinfo->h_addr_list[0]));
        }
        website += strlen(website) + 1;
        server_address.sin_port = htons(atoi(website));
        if(atoi(website) == 443){     //判断为HTTPS请求
            isHTTPS = true;
        }
    }
    else{
        hostent* serverinfo = gethostbyname(website);
        /*if(serverinfo->h_addrtype == AF_INET6){
            get_fd.set_value(-1);
            syslog(LOG_ERR, "Get an IPV6 package\n");
            return -1;
        }*/
        if(serverinfo == nullptr){  //或许有些网站很头铁拿ip直接当域名
            inet_pton(AF_INET, website, &server_address.sin_addr);
        }
        else{
            server_address.sin_addr.s_addr = inet_addr(inet_ntoa(*(in_addr*)serverinfo->h_addr_list[0]));
        }
        server_address.sin_port = htons(80);
    }
	FD err = connect(server, (sockaddr*)&server_address, length);
	if(err < 0){
		syslog(LOG_ERR, "Connect error\n");
		close(server);
		return;
	}
	assert(err != -1);
    int pipefd[2];  //两根管道，分别给客户端-代理和代理-服务器使用
	int ret = pipe(pipefd);
    int pipefd1[2];
	ret = pipe(pipefd1);
    assert(ret != -1);
    if(isHTTPS){
        send(hostfd, https_response, strlen(https_response), 0);
        //exchange_key(hostfd, server, pipefd);
        fcntl(hostfd, F_SETFL, fcntl(hostfd, F_GETFL) | O_NONBLOCK);
        fcntl(server, F_SETFL, fcntl(server, F_GETFL) | O_NONBLOCK);
        while(1){
            ret = splice(hostfd, NULL, pipefd[1], NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
            if(ret == 0){
                break;
            }
            else if(ret == -1){
                if(errno != EAGAIN){
                    break;
                }
            }
            ret = splice(pipefd[0], NULL, server, NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
            if(ret == 0){
                break;
            }
            else if(ret == -1){
                if(errno != EAGAIN){
                    break;
                }
            }
            ret = splice(server, NULL, pipefd1[1], NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
            if(ret == 0){
                break;
            }
            else if(ret == -1){
                if(errno != EAGAIN){
                    break;
                }
            }
            ret = splice(pipefd1[0], NULL, hostfd, NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
            if(ret == 0){
                break;
            }
            else if(ret == -1){
                if(errno != EAGAIN){
                    break;
                }
            }
        }
    }
    else{
        send(server, origin_package, strlen(origin_package), 0);
        fcntl(hostfd, F_SETFL, fcntl(hostfd, F_GETFL) | O_NONBLOCK);
        fcntl(server, F_SETFL, fcntl(server, F_GETFL) | O_NONBLOCK);
        while(1){
            ret = splice(hostfd, NULL, pipefd[1], NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
            if(ret == 0){
                break;
            }
            else if(ret == -1){
                if(errno != EAGAIN){
                    break;
                }
            }
            ret = splice(pipefd[0], NULL, server, NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
            if(ret == 0){
                break;
            }
            else if(ret == -1){
                if(errno != EAGAIN){
                    break;
                }
            }
            ret = splice(server, NULL, pipefd1[1], NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
            if(ret == 0){
                break;
            }
            else if(ret == -1){
                if(errno != EAGAIN){
                    break;
                }
            }
            ret = splice(pipefd1[0], NULL, hostfd, NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
            if(ret == 0){
                break;
            }
            else if(ret == -1){
                if(errno != EAGAIN){
                    break;
                }
            }
        }
    }
    for(int i = 0; i < 2; ++i){
        close(pipefd[i]);
        close(pipefd1[i]);
    }
    close(server);
}

bool Proxy::inWhiteList(std::string server_address){
	std::ifstream in("./whitelist.txt");
	std::string list;
	while(in >> list){
		if(server_address.find(list) != std::string::npos){
			in.close();
			return true;
		}
	}
	in.close();
	return false;
}

Proxy::FD Proxy::create_local_socket(const char* address, int port){
    FD fd = socket(PF_INET, SOCK_STREAM, 0);
	if(fd < 0){
		syslog(LOG_ERR, "socket create error\n");
		return -1;
	}
	int reuse = 1;
    setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
    sockaddr_in local_address;
    bzero(&local_address, sizeof(local_address));
    local_address.sin_family = AF_INET;
    inet_pton(AF_INET, address, &local_address.sin_addr);
	local_address.sin_port = htons(port);
	int ret = bind(fd, (sockaddr*)&local_address, sizeof(local_address));
	if(ret < 0){
		syslog(LOG_ERR, "Bind error\n");
		close(fd);
		return -1;
	}
	return fd;
}

int Proxy::exchange_key(FD hostfd, FD serverfd, int* pipefd){
    int ret;
    ret = splice(hostfd, NULL, pipefd[1], NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
    if(ret <= 0){
        return -1;
    }
    ret = splice(pipefd[0], NULL, serverfd, NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
    if(ret <= 0){
        return -1;
    }
    for(int i = 0; i < 3; ++i){
        ret = splice(serverfd, NULL, pipefd[1], NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
        if(ret <= 0){
            return -1;
        }
        ret = splice(pipefd[0], NULL, hostfd, NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
        if(ret <= 0){
            return -1;
        }
    }
    for(int i = 0; i < 3; ++i){
        ret = splice(hostfd, NULL, pipefd[1], NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
        if(ret <= 0){
            return -1;
        }
        ret = splice(pipefd[0], NULL, serverfd, NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
        if(ret <= 0){
            return -1;
        }
    }
    for(int i = 0; i < 2; ++i){
        ret = splice(serverfd, NULL, pipefd[1], NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
        if(ret <= 0){
            return -1;
        }
        ret = splice(pipefd[0], NULL, hostfd, NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
        if(ret <= 0){
            return -1;
        }
    }
    return 0;
}
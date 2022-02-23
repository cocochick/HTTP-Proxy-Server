#pragma once
#include <cstdio>
#include <cstring>
//LINE_OK表示读取到一个完整的行，LINE_BAD 行错误，LINE_OPEN 行数据不完整。
enum LINE_STATUS {LINE_OK = 0, LINE_BAD, LINE_OPEN};
//NO_REQUEST 表示请求不完整，GET_REQUEST 获得了一个完整的请求，BAD_REQUEST 请求有语法错误，FORBIDDEN_REQUEST 对资源没有足够的访问权限
//INTERNAL_ERROR 服务器内部错误，CLOSED_CONNECTION客户端已经关闭连接
enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, FORBIDDEN_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};
//CHECK_STATE_REQUESTLINE表示正在分析请求行，CHECK_STATE_HEADER表示分析头部字段，CHECK_STATE_CONTENT表示分析内容
enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};

LINE_STATUS parse_line(char* buffer, int& begin, int& end){
    char temp;
    for(; begin < end; ++begin){
        temp = buffer[begin];
        if(temp == '\r'){
            if(begin + 1 == end){
                return LINE_OPEN;
            }
            if(buffer[begin + 1] == '\n'){
                buffer[begin++] = '\0';
                buffer[begin++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp == '\n'){
            if(begin >= 1 && buffer[begin - 1] == '\r'){
                buffer[begin - 1] = '\0';
                buffer[begin++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

HTTP_CODE parse_requestline(char* line, char* package, CHECK_STATE& checkstate){
    char* url = strpbrk(line, " \t");   //strpbrk 查找str1里第一个出现在str2里的字符的位置
    if(url == nullptr){
        return BAD_REQUEST;
    }
    char empty_char = *url;
    *url++ = '\0';
    strcat(package, line);
    strncat(package, &empty_char, 1);
    url += strspn(url, " \t");  //strspn 寻找第一个不在str2里出现的字符的位置
    char* mode = line;
    //暂时只处理GET，其他后续处理或不使用该函数
    if(strcasecmp(mode, "GET") == 0){
        printf("mode GET\n");
    }
    else if(strcasecmp(mode, "HTTP") != 0){
        printf("mode %s\n", mode);
        //return BAD_REQUEST;
    }
    else{
        return BAD_REQUEST;
    }
    char* version = strpbrk(url, " \t");
    if(version == nullptr){
        return BAD_REQUEST;
    }
    empty_char = *version;
    *version++ = '\0';
    if(strncasecmp(url, "http://", 7) == 0){
        url += 7;
        url = strchr(url, '/'); //strchr 找到首次出现char的位置
    }
    if(strstr(url, ":443") == nullptr && (url == nullptr || url[0] != '/')){
        return BAD_REQUEST;
    }
    strcat(package, url);
    strncat(package, &empty_char, 1);
    printf("URL is %s\n", url);  
    version += strspn(version, " \t");
    if(strcasecmp(version, "HTTP/1.1") != 0){
        return BAD_REQUEST;
    }
    strcat(package, version);
    strcat(package, "\r\n");
    checkstate = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

HTTP_CODE parse_header(char* buffer, char* package, char* website, CHECK_STATE& checkstate){
    if(buffer[0] == '\0'){
        strcat(package, "\r\n");
        return GET_REQUEST;
    }
    else if(strncasecmp(buffer, "Host:", 5) == 0){
        strcat(package, buffer);
        buffer += 5;
        buffer += strspn(buffer, " \t");
        strcpy(website, buffer);
        printf("HOST:%s\n", buffer);
    }
    else if(strncasecmp(buffer, "Proxy-Connection:", 17) == 0){
        return NO_REQUEST;
    }
    else{
        strcat(package, buffer);
        //printf( "I can not handle this header\n" );
        //解析其他头部信息,暂时不需要
    }
    strcat(package, "\r\n");
    return NO_REQUEST;
}

HTTP_CODE parse_context(char* buffer, char* package, char* website){
    CHECK_STATE checkstate = CHECK_STATE_REQUESTLINE;
    LINE_STATUS linestatus = LINE_OK;
    HTTP_CODE retcode = NO_REQUEST;
    int begin = 0, end = strlen(buffer), linepos = 0;
    while((linestatus = parse_line(buffer, begin, end)) == LINE_OK){
        char* temp = buffer + linepos;
        linepos = begin;
        switch(checkstate){
            case CHECK_STATE_REQUESTLINE:{
                retcode = parse_requestline(temp, package, checkstate);
                if(retcode == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:{
                retcode = parse_header(temp, package, website, checkstate);
                if(retcode == GET_REQUEST){
                    if(begin < end){
                        temp = buffer + linepos;
                        strcat(package, temp);
                    }
                    return retcode;
                }
                break;
            }
            default:{
                return INTERNAL_ERROR;
            }
        }
    }
    if(linestatus == LINE_OPEN){
        return NO_REQUEST;
    }
    else{
        return BAD_REQUEST;
    }
}
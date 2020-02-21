#include <stdio.h>
#include <string.h>
#include "csapp.h"
#include "cache.h"

void doit(int fd, CacheList *cache);
int parse_url(const char *url, char *host, char *port, char *path);
void cache_URL(const char *URL, const char *headers, void *item, size_t size, CacheList *list);
CachedItem *find(const char *url, CacheList *list);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

  int main(int argc, char **argv)
  {
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    CacheList cache_store;
    char uri[MAXLINE], host[MAXLINE];
    char path[MAXLINE];

    /*initialize the cache*/
    cache_init(&cache_store);  

    /*check command line args*/ 
    if(argc != 2)
    {
      fprintf(stderr, "usage: %s <port>\n", argv[0]);
      exit(1);
    } 

    Signal(SIGPIPE, SIG_IGN);
    listenfd = Open_listenfd(argv[1]);
    while(1)
    {
      clientlen = sizeof(clientaddr);
      connfd = accept(listenfd, (SA*)&clientaddr, &clientlen);
      Getnameinfo((SA*) &clientaddr, clientlen, hostname, MAXLINE,
          port, MAXLINE, 0);
      printf("Accepted connection from (%s, %s) \n", hostname, port);
      parse_url(uri, host, port, path);
      doit(connfd, &cache_store);
      close(connfd);
    }
  }

  void doit(int fd, CacheList *cache)
  {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char port[MAXLINE], path[MAXLINE], host[MAXLINE], temp[MAXLINE];
    char cache_buf[MAXLINE], obj_str[MAXLINE], headers[MAXLINE];
    char item_buf[MAXLINE];

    rio_t rio;
    rio_t rio_server;

    char *mod_since = "If-Modified-Since:";
    char *none_match = "If-None-Match:";

    char* status_code = "HTTP/1.0 200 OK\r\n";

    CachedItem* item; 

    int open_fd;
    int num_bytes;
    int object_size = 0;
    int i = 0;
    int j = 0;
    int status_flag = 0;
    int length_flag = 0;
    int object_flag = 0;
    int rec_bytes_flag = 0;

    void* cache_item = NULL;

    /*check to see if the item is already in the cache*/
    item = find(uri, cache);

    /*write to browser directly from cache*/
    if (item != NULL)
    {
      rio_writen(fd, item->headers, strlen(item->headers));
      rio_writen(fd, "\r\n", 2);
      rio_writen(fd, item->item_p, item->size);
    }

    /*initialize*/
    rio_readinitb(&rio, fd);

    rio_readlineb(&rio, buf, MAXLINE);

    printf("%s \n", buf);

    sscanf(buf, "%s %s %s", method, uri, version);

    if(strcasecmp(method, "GET"))
    {
      return;
    }
    
    /*parse url to get the host, port and path*/
    parse_url(uri, host, port, path);
    
    /*open connection*/
    open_fd = open_clientfd(host, port);

    /*error checking*/
    if(open_fd < 0)
      return

    /*store request headers in temp*/
    sprintf(temp, "GET %s HTTP/1.0\r\n", path);

    /*initialize*/
    rio_readinitb(&rio_server,open_fd);

    /*store request headers in temp*/
    sprintf(temp, "%sHost: %s\r\n",temp, host);
    sprintf(temp, "%s%s",temp, user_agent_hdr);
    sprintf(temp, "%sProxy-connection: close\r\n",temp);
    sprintf(temp, "%sConnection: close\r\n\r\n",temp);
    
    rio_writen(open_fd, temp, strlen(temp));
    
    /*read and write request headers*/
    while((num_bytes = rio_readlineb(&rio, temp, MAXLINE)) > 2)
    {
      if(strncasecmp(temp, mod_since, strlen(mod_since)) == 0)
      {
        continue;
      } 
      else if(strncasecmp(temp, none_match, strlen(none_match)) == 0)
      {
        continue;
      }
      rio_writen(open_fd, temp, num_bytes);
    }

    /*add null terminators*/
    rio_writen(open_fd, temp, num_bytes);

    /*read and write the response headers*/
    while((num_bytes = rio_readlineb(&rio_server, buf, MAXLINE)) > 2)
    {
      /*store request headers in headers*/
      sprintf(headers, "%s%s",headers, buf);
      printf("num bytes read loop 2: %d\n",num_bytes);
          
      /*check if response header has status 200 OK*/
      if(strncasecmp(buf, status_code, strlen(status_code)) == 0)
      {
        /*set flag that indicates the first condition passed*/
        status_flag = 1;
      }
      /*check if response headers have a header called "content-lenght"*/
      else if(strncasecmp(buf, "Content-length:", 15) == 0)
      {
        /*set flag that indicates the second condition passed*/
        length_flag = 1;

        /*if a content-length header exists then extract the object size
         * from the header*/
        while(buf[i] != ':')
        {
          i++;
        }

        i++;

        while(buf[i] != '\r' && buf[i] != '\n')
        {
          obj_str[j] = buf[i];
          i++;
          j++;
        }

        obj_str[j] = '\0';

        /*convert object size string to an integer*/
        object_size = atoi(obj_str);

        /*check if the object is less than the max object size*/
        if(object_size < MAX_OBJECT_SIZE)        
        {
          /*set flag that inidcates third condition passed*/
          object_flag = 1;
        }
      }
    rio_writen(fd, buf, num_bytes);
    }
    /*add null terminators*/
    rio_writen(fd, buf, num_bytes);

    /*read and write the binary data*/
    while((num_bytes = rio_readnb(&rio_server, item_buf, MAXLINE)) > 2)
    {
      /*check if the number of bytes recieved is the same as the
       * object size*/
      if(num_bytes == object_size)
      {
        /*set flag that indicates the fourth condition passed*/
        rec_bytes_flag = 1;
      }
      rio_writen(fd, item_buf, num_bytes);
    }
    /*add null terminators*/
    rio_writen(fd, item_buf, num_bytes);

    /*if all conditions have passed cache the url*/
    if(status_flag && length_flag && object_flag && rec_bytes_flag)
    {
      /*store binary data in cache_item*/
      cache_item = malloc(object_size);
      memcpy(cache_item, item_buf, object_size);
      cache_URL(uri, headers, cache_item, object_size, cache);
    }    
    else
    {
      free(cache_item);
    }

    /*close connection*/
    close(open_fd);

  }

  int parse_url(const char *url, char *host, char *port, char *path)
  {
    int i = 7;
    int host_length = 0;
    int port_length = 0;
    int path_length = 0;

    /*check to see if the url is a proper http scheme*/
    if(strncasecmp(url, "http://", 7) != 0)
    {
      /*return 0 if the url is not a proper http scheme*/
      return 0;   
    }

    /*parse the url to find the host*/
    while((url[i] != '/') && (url[i] != ':') && (url[i] != '\0'))
    {
      host[host_length] = url[i];
      host_length++;
      i++;
    } 

    /*add null terminator to host*/
    host[host_length] = '\0';

    /*check to see if a port exists*/
    if(url[i] == ':')
    {
      /*if a port exists parse the url to get the port number*/
      i++;
      while((url[i] != '/') && (url[i] != '\0'))
      {
        port[port_length] = url[i];
        port_length++;
        i++;
      }

      /*add null terminator to port*/
      port[port_length] = '\0';
    }

    /*if the port does not exist set port to "80" */
    else
    {
      port[0] = '8';
      port[1] = '0';
      port[2] = '\0';
    }

    /*check to see if a path exists*/
    if(url[i] == '\0')
    {
      /*if a path does not exist set path to "/"*/
      path[0] = '/';
      path[1] = '\0';
    }
    else
    {
      /*parse the url until you reach the null terminator
       * to get the path*/
      while(url[i] != '\0')
      {
        path[path_length] = url[i];
        path_length++;
        i++;
      }

      /*add null terminator to the path*/
      path[path_length] = '\0';

    }

    return 1;
  }
  
   

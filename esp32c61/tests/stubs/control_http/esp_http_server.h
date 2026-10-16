#pragma once
#include "esp_err.h"
#include <stddef.h>
typedef void *httpd_handle_t;
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
#define HTTPD_RESP_USE_STRLEN -1
#define HTTP_POST 1
#define HTTP_GET 0
typedef struct {
    const char *uri,*body,*type;
    size_t content_len,received;
    char output[8192],status[64];
    int fail_receive;
} httpd_req_t;
typedef struct { const char *uri;int method;esp_err_t (*handler)(httpd_req_t*); } httpd_uri_t;
esp_err_t httpd_resp_send_err(httpd_req_t*,int,const char*);
esp_err_t httpd_resp_set_type(httpd_req_t*,const char*);
esp_err_t httpd_resp_set_status(httpd_req_t*,const char*);
esp_err_t httpd_resp_set_hdr(httpd_req_t*,const char*,const char*);
esp_err_t httpd_resp_send(httpd_req_t*,const char*,int);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t*,const char*,char*,size_t);
int httpd_req_recv(httpd_req_t*,char*,size_t);
esp_err_t httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t*);

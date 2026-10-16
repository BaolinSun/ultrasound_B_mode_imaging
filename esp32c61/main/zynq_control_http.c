#include "zynq_control_http.h"
#include "zynq_control.h"
#include <stdlib.h>

static esp_err_t json_reply(httpd_req_t *r,const char *status,cJSON *j) {
    if(!j) return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"JSON allocation failed");
    char *body=cJSON_PrintUnformatted(j); cJSON_Delete(j);
    if(!body) return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"JSON allocation failed");
    httpd_resp_set_type(r,"application/json"); httpd_resp_set_status(r,status);
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    esp_err_t e=httpd_resp_send(r,body,HTTPD_RESP_USE_STRLEN); cJSON_free(body); return e;
}
static esp_err_t error(httpd_req_t *r,const char *status,const char *text) {
    cJSON *j=cJSON_CreateObject(); if(j) cJSON_AddStringToObject(j,"message",text);
    return json_reply(r,status,j);
}
static esp_err_t commands(httpd_req_t *r) {
    if(!zynq_control_ready()) return error(r,"503 Service Unavailable","控制服务不可用");
    char type[64];
    if(httpd_req_get_hdr_value_str(r,"Content-Type",type,sizeof(type))!=ESP_OK || strncmp(type,"application/json",16))
        return error(r,"415 Unsupported Media Type","需要 application/json");
    if(r->content_len<1 || r->content_len>2048) return error(r,"400 Bad Request","请求上限 2048 字节，每批最多 8 条命令");
    char *body=malloc(r->content_len+1);
    if(!body) return error(r,"503 Service Unavailable","内存不足");
    size_t n=0;
    while(n<r->content_len) {
        int got=httpd_req_recv(r,body+n,r->content_len-n);
        if(got<=0) { free(body); return error(r,"408 Request Timeout","请求未完整接收，未提交命令"); }
        n+=(size_t)got;
    }
    body[n]=0;
    cJSON *j=cJSON_ParseWithLengthOpts(body,n+1,NULL,true); free(body);
    cJSON *client=cJSON_GetObjectItemCaseSensitive(j,"client_id");
    cJSON *request=cJSON_GetObjectItemCaseSensitive(j,"request_id");
    cJSON *list=cJSON_GetObjectItemCaseSensitive(j,"commands");
    cJSON *hv=cJSON_GetObjectItemCaseSensitive(j,"allow_hv");
    zynq_control_job *job=calloc(1,sizeof(*job));
    if(!job) { cJSON_Delete(j); return error(r,"503 Service Unavailable","内存不足"); }
    bool valid=cJSON_IsObject(j)&&cJSON_IsString(client)&&cJSON_IsString(request)&&cJSON_IsArray(list)&&
        wc_hex8(client->valuestring,&job->client)&&wc_hex8(request->valuestring,&job->request);
    int count=cJSON_GetArraySize(list);
    if(count<1 || count>(int)WC_BATCH) valid=false;
    const char *why="请求格式错误";
    for(int i=0;valid && i<count;i++) {
        cJSON *line=cJSON_GetArrayItem(list,i);
        if(!cJSON_IsString(line) || strlen(line->valuestring)>=WC_COMMAND) { valid=false; break; }
        const char *reason=wc_validate(line->valuestring);
        if(reason) { valid=false; why=reason; break; }
        strcpy(job->commands[i],line->valuestring);
    }
    job->count=count>0?(unsigned)count:0;
    uint32_t id=0;
    esp_err_t result=valid?zynq_control_submit(job,cJSON_IsTrue(hv),&id):ESP_ERR_INVALID_ARG;
    cJSON_Delete(j); free(job);
    if(result!=ESP_OK) return error(r,result==ESP_ERR_TIMEOUT?"409 Conflict":"400 Bad Request",
        result==ESP_ERR_TIMEOUT?"控制任务忙碌或配置被其他客户端占用":valid?"参数无效；开启高压需要 allow_hv=true":why);
    j=cJSON_CreateObject(); char name[9]; snprintf(name,sizeof(name),"%08lx",(unsigned long)id);
    if(j) cJSON_AddStringToObject(j,"job_id",name);
    return json_reply(r,"202 Accepted",j);
}
static esp_err_t get_job(httpd_req_t *r) {
    if(!zynq_control_ready()) return error(r,"503 Service Unavailable","控制服务不可用");
    uint32_t id;
    if(!wc_hex8(r->uri+15,&id)||!id) return error(r,"400 Bad Request","任务编号格式错误");
    zynq_control_job *job=malloc(sizeof(*job));
    if(!job) return error(r,"503 Service Unavailable","内存不足");
    if(!zynq_control_job_get(id,job)) { free(job); return error(r,"404 Not Found","任务不存在或已退出有界缓存；不要重新提交旧命令"); }
    cJSON *j=cJSON_CreateObject();
    if(j) {
        cJSON_AddBoolToObject(j,"busy",job->busy); cJSON_AddStringToObject(j,"state",job->state);
        cJSON_AddStringToObject(j,"error",job->error);
        cJSON_AddNumberToObject(j,"count",job->count); cJSON_AddNumberToObject(j,"completed",job->completed);
        cJSON *lines=cJSON_AddArrayToObject(j,"results");
        for(unsigned i=0;lines && i<job->count;i++) {
            cJSON *item=cJSON_CreateObject(); if(!item) break;
            cJSON_AddStringToObject(item,"command",job->commands[i]);
            cJSON_AddStringToObject(item,"response",job->responses[i]);
            cJSON_AddItemToArray(lines,item);
        }
    }
    free(job); return json_reply(r,"200 OK",j);
}
esp_err_t zynq_control_http_register(httpd_handle_t server) {
    const httpd_uri_t routes[]={
        {.uri="/api/zynq/commands",.method=HTTP_POST,.handler=commands},
        {.uri="/api/zynq/jobs/*",.method=HTTP_GET,.handler=get_job}};
    for(unsigned i=0;i<2;i++) {
        esp_err_t e=httpd_register_uri_handler(server,&routes[i]); if(e!=ESP_OK) return e;
    }
    return ESP_OK;
}

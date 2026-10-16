#include "zynq_control.h"
#include "esp_http_server.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../main/zynq_control_http.c"
static unsigned submitted,registered;
static bool service_ready=true;
bool zynq_control_ready(void) { return service_ready; }
esp_err_t zynq_control_submit(const zynq_control_job *j,bool allow,uint32_t *id) {
    assert(j->client==1&&j->request==2&&j->count==1);submitted++;
    if(!strcmp(j->commands[0],"HV ENABLE")&&!allow)return ESP_ERR_INVALID_ARG;
    *id=1;return ESP_OK;
}
bool zynq_control_job_get(uint32_t id,zynq_control_job *j) {
    if(id!=1)return false;
    memset(j,0,sizeof(*j));strcpy(j->state,"succeeded");j->count=j->completed=1;
    strcpy(j->commands[0],"STATUS");strcpy(j->responses[0],"@STATUS v=1 cfg=0");return true;
}
esp_err_t httpd_resp_send_err(httpd_req_t *r,int status,const char *s) { snprintf(r->status,sizeof(r->status),"%d",status);snprintf(r->output,sizeof(r->output),"%s",s);return 0; }
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *s) { (void)r;(void)s;return 0; }
esp_err_t httpd_resp_set_status(httpd_req_t *r,const char *s) { snprintf(r->status,sizeof(r->status),"%s",s);return 0; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v) { (void)r;assert(!strcmp(k,"Cache-Control")&&!strcmp(v,"no-store"));return 0; }
esp_err_t httpd_resp_send(httpd_req_t *r,const char *s,int len) { (void)len;snprintf(r->output,sizeof(r->output),"%s",s);return 0; }
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r,const char *k,char *v,size_t cap) {
    assert(!strcmp(k,"Content-Type")); /* No control-token header read is permitted. */
    const char *s=r->type;
    if(!s || strlen(s)>=cap)return 1;
    strcpy(v,s);return 0;
}
int httpd_req_recv(httpd_req_t *r,char *v,size_t cap) {
    if(r->fail_receive)return 0;
    size_t n=r->content_len-r->received;if(n>cap)n=cap;if(n>7)n=7;
    memcpy(v,r->body+r->received,n);r->received+=n;return (int)n;
}
esp_err_t httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t *r) { (void)h;assert(r->uri&&r->handler);registered++;return 0; }
static httpd_req_t request(const char *body) {
    httpd_req_t r={.body=body,.type="application/json",.uri="/api/zynq/commands"};
    r.content_len=strlen(body);return r;
}
int main(void) {
    const char *good="{\"client_id\":\"00000001\",\"request_id\":\"00000002\",\"commands\":[\"STATUS\"]}";
    httpd_req_t r=request(good);service_ready=false;commands(&r);assert(strstr(r.status,"503")&&submitted==0);
    r.uri="/api/zynq/jobs/00000001";get_job(&r);assert(strstr(r.status,"503"));service_ready=true;
    r=request(good);r.type="text/plain";commands(&r);assert(strstr(r.status,"415")&&submitted==0);
    r=request(good);r.content_len=2049;commands(&r);assert(strstr(r.status,"400")&&submitted==0);
    r=request(good);r.fail_receive=1;commands(&r);assert(strstr(r.status,"408")&&submitted==0);
    const char *bad[]={"{bad", "{}", "{\"client_id\":\"00000001\",\"request_id\":\"00000002\",\"commands\":[\"BRAM WR t_timing 0 1 2048\"]}",
        "{\"client_id\":\"00000001\",\"request_id\":\"00000002\",\"commands\":[\"HV ENABLE\\nHV ENABLE\"]}"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){r=request(bad[i]);commands(&r);assert(strstr(r.status,"400")&&submitted==0);}
    r=request(good);commands(&r);assert(strstr(r.status,"202")&&submitted==1&&strstr(r.output,"00000001"));
    r=request(good);r.uri="/api/zynq/jobs/00000001";get_job(&r);assert(strstr(r.status,"200")&&strstr(r.output,"succeeded"));
    r=request(good);r.uri="/api/zynq/jobs/000000ff";get_job(&r);assert(strstr(r.status,"404"));
    r=request(good);r.uri="/api/zynq/jobs/no";get_job(&r);assert(strstr(r.status,"400"));
    r=request("{\"client_id\":\"00000001\",\"request_id\":\"00000002\",\"commands\":[\"HV ENABLE\"]}");
    commands(&r);assert(strstr(r.status,"400")); /* Removing authentication must not remove HV confirmation. */
    r=request("{\"client_id\":\"00000001\",\"request_id\":\"00000002\",\"commands\":[\"HV ENABLE\"],\"allow_hv\":true}");
    commands(&r);assert(strstr(r.status,"202"));
    assert(zynq_control_http_register(NULL)==0&&registered==2);
    puts("PASS actual HTTP: token-free commands and jobs/service readiness/type/size/fragmented body/invalid JSON/geometry/injection/HV confirmation/async submit/job bounds");return 0;
}

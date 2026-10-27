#define PROVISIONING_HOST_TEST 1
#include "provisioning_fake.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../main/config_store.c"
static uint8_t network[160],pending_network[160];static size_t network_n,pending_n;
static char factory[96],pending_factory[96];
static unsigned factory_writes,network_writes;
static int init_error,part_error,mac_error,alloc_error,read_error,open_error,write_error,commit_error,verify_error,ap_mac_error,network_error,network_commit_error;
static int factory_fault,network_set_error;
static const uint8_t mac[6]={0x10,0x20,0x30,0x40,0x50,0x60};
void fake_log(const char *tag,const char *fmt,...){(void)tag;assert(!strstr(fmt,"password="));}
SemaphoreHandle_t xSemaphoreCreateMutex(void){return alloc_error?NULL:(void*)1;}
int xSemaphoreTake(SemaphoreHandle_t p,unsigned t){(void)p;(void)t;return 1;}
int xSemaphoreGive(SemaphoreHandle_t p){(void)p;return 1;}
esp_err_t nvs_flash_init(void){return init_error;}
esp_err_t nvs_flash_init_partition(const char *p){assert(!strcmp(p,"device_nvs"));return part_error;}
esp_err_t esp_efuse_mac_get_default(uint8_t *p){memcpy(p,mac,6);return mac_error;}
esp_err_t esp_read_mac(uint8_t *p,int type){assert(type==ESP_MAC_WIFI_SOFTAP);memcpy(p,mac,6);p[5]++;return ap_mac_error;}
esp_err_t nvs_open(const char *s,int mode,nvs_handle_t *h){assert(!strcmp(s,"udp_config"));*h=1;return mode==NVS_READONLY&&!network_n?ESP_ERR_NVS_NOT_FOUND:network_error;}
esp_err_t nvs_open_from_partition(const char *p,const char *s,int mode,nvs_handle_t *h){assert(!strcmp(p,"device_nvs")&&!strcmp(s,"device"));*h=2;if(mode==NVS_READONLY&&(factory_fault==1||(factory_fault==3&&factory_writes)))return ESP_FAIL;return mode==NVS_READWRITE?open_error:ESP_OK;}
void nvs_close(nvs_handle_t h){assert(h==1||h==2);}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *k,void *p,size_t *n){assert(h==1&&!strcmp(k,"settings"));if(read_error)return read_error;if(*n<network_n)return ESP_ERR_NVS_INVALID_LENGTH;memcpy(p,network,network_n);*n=network_n;return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *k,const void *p,size_t n){assert(h==1&&!strcmp(k,"settings"));if(network_set_error)return network_set_error;if(network_error)return network_error;memcpy(pending_network,p,n);pending_n=n;return ESP_OK;}
esp_err_t nvs_get_str(nvs_handle_t h,const char *k,char *p,size_t *n){assert(h==2&&!strcmp(k,"identity"));if(factory_fault==2 || (verify_error&&factory_writes))return ESP_FAIL;if(!factory[0])return ESP_ERR_NVS_NOT_FOUND;if(*n<strlen(factory)+1)return ESP_ERR_NVS_INVALID_LENGTH;strcpy(p,factory);*n=strlen(factory)+1;if(factory_writes){if(factory_fault==4)(*n)--;if(factory_fault==5)p[5]^=1;}else{if(factory_fault==7)p[5]=0;if(factory_fault==8)p[*n-1]='X';if(factory_fault==9)*n=0;if(factory_fault==10)*n=120;}return ESP_OK;}
esp_err_t nvs_set_str(nvs_handle_t h,const char *k,const char *p){assert(h==2&&!strcmp(k,"identity"));if(write_error)return write_error;strcpy(pending_factory,p);return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){if(h==2){if(commit_error)return commit_error;strcpy(factory,pending_factory);factory_writes++;}else{if(network_commit_error)return network_commit_error;if(network_error)return network_error;memcpy(network,pending_network,pending_n);network_n=pending_n;network_writes++;}return ESP_OK;}
static void reset(void){memset(network,0,sizeof network);network_n=pending_n=0;factory[0]=pending_factory[0]=0;factory_writes=network_writes=0;init_error=part_error=mac_error=alloc_error=read_error=open_error=write_error=commit_error=verify_error=ap_mac_error=network_error=network_commit_error=factory_fault=network_set_error=0;identity_state="unprepared";}
static void old(void){config_v1_t v={.version=1,.ssid="router",.password="routerpass",.target_ip="192.168.1.50",.target_port=50001,.ap_password="23456789AbCdEfGh"};memcpy(network,&v,sizeof v);network_n=sizeof v;}
static void prepared(void){strcpy(factory,"DVC1|102030405060|Ultrasound-C61-405060|ABCDEFGH12345678");}
int main(void){
 reset();assert(config_store_init()==ESP_OK&&!device_store_get(NULL));assert(!strcmp(device_store_state(),"unprepared"));assert(current.target_port==50001&&network_n==sizeof(app_config_t));
 reset();prepared();old();assert(config_store_init()==ESP_OK);device_identity_t id;assert(device_store_get(&id));assert(!strcmp(id.password,"ABCDEFGH12345678")&&!strcmp(current.ssid,"router")&&network_n==sizeof(app_config_t)&&!factory_writes);
 factory_writes=network_writes=0;assert(config_store_init()==ESP_OK&&!network_writes&&!factory_writes); /* Normal boot is credential read only. */
 app_config_t c;config_store_get(&c);strcpy(c.ssid,"newrouter");assert(config_store_save(&c)==ESP_OK&&!strcmp(current.ssid,"newrouter")&&!factory_writes);
 c.target_port=0;assert(config_store_save(&c)==ESP_ERR_INVALID_ARG);
 reset();old();assert(config_store_init()==ESP_OK);assert(strstr(factory,"Ultrasound-C61-405061")&&strstr(factory,"23456789AbCdEfGh"));assert(factory_writes==1&&network_writes==1);
 char durable[96];strcpy(durable,factory);network_n=0;assert(config_store_init()==ESP_OK&&!strcmp(durable,factory)); /* User NVS reset doesn't rotate identity. */
 reset();old();commit_error=ESP_FAIL;assert(config_store_init()==ESP_OK&&!strcmp(device_store_state(),"migration_failed")&&network_n==140&&!factory[0]);config_store_get(&c);assert(config_store_save(&c)==ESP_ERR_INVALID_STATE);commit_error=0;assert(config_store_init()==ESP_OK&&device_store_get(NULL));
 reset();old();verify_error=1;assert(config_store_init()==ESP_OK&&network_n==140&&factory[0]);verify_error=0;assert(config_store_init()==ESP_OK&&network_n==sizeof(app_config_t)&&factory_writes==1); /* Reboot after identity commit. */
 reset();old();network_commit_error=ESP_FAIL;assert(config_store_init()==ESP_OK&&network_n==140&&factory_writes==1&&device_store_get(NULL));network_commit_error=0;assert(config_store_init()==ESP_OK&&network_n==sizeof(app_config_t)&&factory_writes==1);
 reset();prepared();old();factory[5]='2';assert(config_store_init()==ESP_OK&&!strcmp(device_store_state(),"mac_mismatch")&&!factory_writes&&network_n==140);config_store_get(&c);assert(config_store_save(&c)==ESP_ERR_INVALID_STATE);
 reset();prepared();factory[0]='X';assert(config_store_init()==ESP_OK&&!strcmp(device_store_state(),"invalid")&&!factory_writes);
 reset();old();part_error=ESP_ERR_NOT_FOUND;assert(config_store_init()==ESP_OK&&!strcmp(device_store_state(),"missing_partition")&&network_n==140);
 reset();old();mac_error=ESP_FAIL;assert(config_store_init()==ESP_OK&&!strcmp(device_store_state(),"invalid"));
 reset();old();open_error=ESP_FAIL;assert(config_store_init()==ESP_OK&&!strcmp(device_store_state(),"migration_failed")&&network_n==140);
 reset();old();write_error=ESP_FAIL;assert(config_store_init()==ESP_OK&&!factory_writes&&network_n==140);
 reset();old();ap_mac_error=ESP_FAIL;assert(config_store_init()==ESP_OK&&!factory_writes&&network_n==140);
 reset();prepared();init_error=ESP_FAIL;assert(config_store_init()==ESP_FAIL);
 reset();alloc_error=1;assert(config_store_init()==ESP_ERR_NO_MEM);
 reset();old();read_error=ESP_FAIL;assert(config_store_init()==ESP_FAIL);
 reset();old();read_error=ESP_ERR_NVS_INVALID_LENGTH;assert(config_store_init()==ESP_OK&&!device_store_get(NULL));
 reset();prepared();assert(config_store_init()==ESP_OK);network_error=ESP_FAIL;config_store_get(&c);strcpy(c.ssid,"x");assert(config_store_save(&c)==ESP_FAIL&&current.ssid[0]==0);
 reset();old();((config_v1_t*)network)->ap_password[0]=0;assert(config_store_init()==ESP_OK&&!factory_writes);
 reset();old();((config_v1_t*)network)->version=3;assert(config_store_init()==ESP_OK&&!factory_writes);
 reset();old();memset(((config_v1_t*)network)->ap_password,'x',17);assert(config_store_init()==ESP_OK&&!factory_writes);
 reset();old();((config_v1_t*)network)->target_port=0;assert(config_store_init()==ESP_OK&&!factory_writes);
 reset();old();((config_v1_t*)network)->ap_password[0]='!';assert(config_store_init()==ESP_OK&&!factory_writes&&network_n==140);
 reset();old();part_error=ESP_FAIL;assert(config_store_init()==ESP_OK&&!strcmp(identity_state,"invalid"));
 for(int f=1;f<=10;f++){if(f==6)continue;reset();old();factory_fault=f;if(f==1||f==2||f>=7)prepared();assert(config_store_init()==ESP_OK&&!device_store_get(NULL)&&network_n==140);}
 reset();prepared();network_set_error=ESP_FAIL;assert(config_store_init()==ESP_FAIL);
 reset();prepared();network_commit_error=ESP_FAIL;assert(config_store_init()==ESP_FAIL);
 reset();app_config_t invalid={.version=3};memcpy(network,&invalid,sizeof invalid);network_n=sizeof invalid;assert(config_store_init()==ESP_OK);
 reset();network[0]=42;network_n=1;assert(config_store_init()==ESP_OK);
 device_identity_t v={.mac={0x10,2,3,4,5,6},.ssid="valid_name",.password="12345678ABCDEFGH"},r;char text[96];assert(device_identity_format(&v,text,sizeof text)&&device_identity_parse(text,&r));assert(!memcmp(&v,&r,sizeof v));
 assert(!device_identity_valid(NULL)&&!device_identity_parse(NULL,&r)&&!device_identity_parse(text,NULL)&&!device_identity_format(&v,NULL,0));assert(!device_identity_format(&v,text,1));
 const char *bad[]={"","DVC2|102030405060|x|12345678ABCDEFGH","DVC1|GG2030405060|x|12345678ABCDEFGH","DVC1|102030405060|x|short","DVC1|102030405060||12345678ABCDEFGH","DVC1|102030405060|x;|12345678ABCDEFGH","DVC1|102030405060|x|12345678ABCDEFG|"};
 for(unsigned i=0;i<sizeof bad/sizeof *bad;i++)assert(!device_identity_parse(bad[i],&r));
 v.mac[0]|=1;assert(!device_identity_valid(&v));memset(v.mac,0,6);assert(!device_identity_valid(&v));v.mac[0]=2;v.password[0]=';';assert(!device_identity_valid(&v));v.password[0]='a';memset(v.ssid,'x',sizeof v.ssid);assert(!device_identity_valid(&v));
 strcpy(v.ssid,"x");
 for(unsigned ch=1;ch<128;ch++){v.password[0]=(char)ch;bool ok=(ch>='0'&&ch<='9')||(ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z');assert(device_identity_valid(&v)==ok);}
 v.password[0]=0;assert(!device_identity_valid(&v));memset(v.password,'x',sizeof v.password);assert(!device_identity_valid(&v));strcpy(v.password,"ABCDEFGH12345678");v.ssid[0]=0;assert(!device_identity_valid(&v));strcpy(v.ssid,"x");assert(device_identity_format(&v,text,sizeof text)&&device_identity_parse(text,&r));
 const char *extra[]={"DVC1|102030405060_x|12345678ABCDEFGH","DVC1|102030405060|xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx|12345678ABCDEFGH","DVC1|1G2030405060|x|12345678ABCDEFGH","DVC1|102030405060|xxxxxxxxxxxxxxxxxxxxxxxxxxxxx"};
 for(unsigned i=0;i<sizeof extra/sizeof *extra;i++)assert(!device_identity_parse(extra[i],&r));
 char long_text[120];memset(long_text,'x',sizeof long_text);long_text[119]=0;assert(!device_identity_parse(long_text,&r));assert(!device_identity_format(NULL,text,sizeof text));
 puts("PASS actual provisioning store: independent credentials, legacy ABI, durable migration/reboot, failures, reset, MAC binding and network-only writes");return 0;
}

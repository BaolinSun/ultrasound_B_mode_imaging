#define PROVISIONING_HOST_TEST 1
#include "provisioning_fake.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../main/wifi_manager.c"
static app_config_t config;
static device_identity_t id={.mac={0x10,2,3,4,5,6},.ssid="Ultrasound-C61-040506",.password="ABCDEFGH12345678"};
static bool ready,link;static uint64_t clock_ms;static unsigned modes,ap_sets,sta_sets,connects,disconnects;static int mode,fail_mode,fail_config,fail_connect,fail_info,alloc_fail,init_fail;
static unsigned startup_n,startup_fault;static bool generation_race;
static unsigned locks,reconfigure_on_lock;
static int attempt(void){return ++startup_n==startup_fault?ESP_FAIL:ESP_OK;}
void fake_log(const char *t,const char *f,...){(void)t;assert(!strstr(f,"password="));}
const char *esp_err_to_name(int e){(void)e;return "mock";}
SemaphoreHandle_t xSemaphoreCreateMutex(void){return alloc_fail?NULL:(void*)1;}
int xSemaphoreTake(SemaphoreHandle_t p,unsigned t){(void)p;(void)t;if(++locks==reconfigure_on_lock){request_generation++;state.reconfiguring=true;}return 1;}
int xSemaphoreGive(SemaphoreHandle_t p){(void)p;return 1;}
int64_t esp_timer_get_time(void){return (int64_t)clock_ms*1000;}
void config_store_get(app_config_t *p){*p=config;if(generation_race){request_generation++;generation_race=false;}}
bool device_store_get(device_identity_t *p){if(ready&&p)*p=id;return ready;}
void psram_udp_set_link(bool b){link=b;}
esp_err_t esp_netif_init(void){return init_fail?init_fail:attempt();}
esp_err_t esp_event_loop_create_default(void){return attempt();}
esp_netif_t *esp_netif_create_default_wifi_sta(void){static int x;return attempt()==ESP_OK?&x:NULL;}
esp_netif_t *esp_netif_create_default_wifi_ap(void){static int x;return attempt()==ESP_OK?&x:NULL;}
esp_err_t esp_wifi_init(const wifi_init_config_t *p){(void)p;return attempt();}
esp_err_t esp_wifi_set_storage(int x){assert(x==WIFI_STORAGE_RAM);return attempt();}
esp_err_t esp_event_handler_register(int b,int i,void(*f)(void*,int,int32_t,void*),void *p){(void)p;assert((b==WIFI_EVENT||b==IP_EVENT)&&i==ESP_EVENT_ANY_ID&&f==event_handler);return attempt();}
esp_err_t esp_wifi_set_mode(int x){modes++;if(fail_mode||attempt()!=ESP_OK)return ESP_FAIL;mode=x;return ESP_OK;}
esp_err_t esp_wifi_set_config(int x,const wifi_config_t *p){if(fail_config||attempt()!=ESP_OK)return ESP_FAIL;if(x==WIFI_IF_AP){ap_sets++;assert(ready&&p->ap.authmode==WIFI_AUTH_WPA2_PSK&&p->ap.max_connection==2);assert(!memcmp(p->ap.password,id.password,16)&&!memcmp(p->ap.ssid,id.ssid,strlen(id.ssid)));}else{sta_sets++;assert(!memcmp(p->sta.ssid,config.ssid,strlen(config.ssid)));}return ESP_OK;}
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *p){memset(p,0,sizeof *p);strcpy((char*)p->ssid,fail_info==2?"old-router":config.ssid);return fail_info==1?ESP_FAIL:ESP_OK;}
esp_err_t esp_wifi_start(void){return attempt();}
esp_err_t esp_wifi_set_ps(int x){assert(x==WIFI_PS_NONE);return attempt();}
esp_err_t esp_wifi_connect(void){connects++;return fail_connect?ESP_FAIL:ESP_OK;}
esp_err_t esp_wifi_disconnect(void){disconnects++;return ESP_OK;}
int xTaskCreate(void(*f)(void*),const char *n,unsigned s,void *p,unsigned pri,void *h){(void)p;(void)h;assert(f==supervisor&&!strcmp(n,"wifi_manager")&&s==4096&&pri==4);return pdPASS;}
void vTaskDelay(unsigned n){(void)n;}
static void reset(bool credential,bool station){memset(&state,0,sizeof state);ready=credential;config=(app_config_t){.version=2,.target_port=50001};if(station){strcpy(config.ssid,"router");strcpy(config.password,"password");}disconnected_at=connected_at=retry_at=apply_at=0;retry_delay_ms=1000;request_generation=applied_generation=0;clock_ms=100;ap_sets=sta_sets=connects=disconnects=modes=startup_n=startup_fault=locks=reconfigure_on_lock=0;fail_mode=fail_config=fail_connect=fail_info=alloc_fail=init_fail=0;link=false;generation_race=false;}
static void ip(void){ip_event_got_ip_t got={.ip_info={.ip={0xC0A8031B}}};event_handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&got);}
int main(void){
 reset(true,false);assert(wifi_manager_init()==ESP_OK&&state.ap_active&&ap_sets==1&&!connects&&!sta_sets);assert(!strcmp(state.ap_ssid,id.ssid));
 reset(false,false);assert(wifi_manager_init()==ESP_OK&&!state.ap_active);clock_ms+=60000;supervisor_step();assert(!ap_sets&&!connects);
 reset(false,true);assert(wifi_manager_init()==ESP_OK&&sta_sets==1);supervisor_step();assert(connects==1&&!state.ap_active);ip();assert(link&&state.connected); /* Existing STA works even without identity. */
 reset(true,true);assert(wifi_manager_init()==ESP_OK&&!state.ap_active);supervisor_step();assert(connects==1);clock_ms+=999;supervisor_step();assert(connects==1);clock_ms++;supervisor_step();assert(connects==2);
 clock_ms=30099;supervisor_step();assert(!state.ap_active);clock_ms=30100;supervisor_step();assert(state.ap_active&&ap_sets==1);
 fail_info=1;ip();assert(!state.connected);fail_info=2;ip();assert(!state.connected);fail_info=0;ip();assert(state.connected&&link&&!strcmp(state.sta_ip,"192.168.3.27"));
 wifi_manager_status_t status;wifi_manager_get_status(&status);assert(status.ap_close_remaining_ms==30000);
 clock_ms+=29999;supervisor_step();wifi_manager_get_status(&status);assert(state.ap_active&&status.ap_close_remaining_ms==1);
 clock_ms++;supervisor_step();assert(!state.ap_active&&mode==WIFI_MODE_STA);wifi_manager_get_status(&status);assert(!status.ap_close_remaining_ms);
 wifi_event_sta_disconnected_t bad={.reason=202};event_handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&bad);assert(!state.connected&&!link&&state.disconnect_reason==202);clock_ms+=30000;supervisor_step();assert(state.ap_active);ip();assert(state.disconnect_reason==0);
 strcpy(config.ssid,"new-router");wifi_manager_reconfigure();assert(state.reconfiguring&&!link);clock_ms+=499;supervisor_step();assert(!disconnects);clock_ms++;supervisor_step();assert(disconnects==1&&!state.connected&&!state.reconfiguring&&sta_sets==2);
 event_handler(NULL,IP_EVENT,IP_EVENT_STA_LOST_IP,NULL);assert(!link);fail_connect=1;supervisor_step();assert(!state.connected);
 fail_config=1;wifi_manager_reconfigure();clock_ms+=500;supervisor_step();assert(state.reconfiguring);fail_config=0;clock_ms+=1000;supervisor_step();assert(!state.reconfiguring);
 state.reconfiguring=true;ip();assert(!state.connected);state.reconfiguring=false;
 reset(true,false);fail_mode=1;assert(wifi_manager_init()==ESP_FAIL&&!state.ap_active);
 reset(true,false);fail_config=1;assert(wifi_manager_init()==ESP_FAIL&&!state.ap_active&&mode==WIFI_MODE_STA);
 reset(true,false);alloc_fail=1;assert(wifi_manager_init()==ESP_ERR_NO_MEM);
 reset(true,false);init_fail=ESP_FAIL;assert(wifi_manager_init()==ESP_FAIL);
 reset(false,false);assert(enable_ap()==ESP_ERR_INVALID_STATE&&!modes);
 for(unsigned f=1;f<=13;f++){reset(true,false);startup_fault=f;assert(wifi_manager_init()!=ESP_OK);}
 reset(true,true);startup_fault=10;assert(wifi_manager_init()!=ESP_OK); /* STA configuration failure. */
 reset(true,true);assert(wifi_manager_init()==ESP_OK);generation_race=true;supervisor_step();assert(!connects);supervisor_step();assert(!state.reconfiguring);
 event_handler(NULL,WIFI_EVENT,99,NULL);event_handler(NULL,IP_EVENT,99,NULL);event_handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
 state.ap_active=true;state.connected=true;connected_at=200;clock_ms=100;wifi_manager_get_status(&status);assert(!status.ap_close_remaining_ms);clock_ms=30200;wifi_manager_get_status(&status);assert(!status.ap_close_remaining_ms);fail_mode=1;supervisor_step();assert(state.ap_active);fail_mode=0;
 state.reconfiguring=true;wifi_manager_get_status(&status);assert(!status.ap_close_remaining_ms);state.reconfiguring=false;state.connected=false;wifi_manager_get_status(&status);assert(!status.ap_close_remaining_ms);
 clock_ms+=30000;state.ap_active=false;fail_config=1;supervisor_step();assert(!state.ap_active);fail_config=0;
 ip();ip();assert(state.connected);state.ap_active=true;supervisor_step();assert(state.connected);
 reset(true,true);assert(wifi_manager_init()==ESP_OK);state.ap_active=true;state.connected=true;clock_ms=30000;connected_at=0;reconfigure_on_lock=locks+3;supervisor_step();assert(state.ap_active&&state.reconfiguring);
 puts("PASS actual Wi-Fi supervisor: credential gating, first boot, 30s fallback/close, retry, wrong password, stale IP, deferred apply and faults");return 0;
}

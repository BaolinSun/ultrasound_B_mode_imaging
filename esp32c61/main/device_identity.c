#include "device_identity.h"
#include <stdio.h>
#include <string.h>
static bool alnum_ascii(unsigned char c) {
    return (c>='0'&&c<='9')||(c>='A'&&c<='Z')||(c>='a'&&c<='z');
}
bool device_identity_valid(const device_identity_t *id) {
    if(!id || (id->mac[0]&1U) || !memchr(id->ssid,0,sizeof id->ssid) ||
       !memchr(id->password,0,sizeof id->password))return false;
    unsigned nonzero=0;for(unsigned i=0;i<6;i++)nonzero|=id->mac[i];
    size_t n=strlen(id->ssid);
    if(!nonzero || !n || n>32 || strlen(id->password)!=16)return false;
    for(size_t i=0;i<n;i++)if(!alnum_ascii((unsigned char)id->ssid[i]) && id->ssid[i]!='-' && id->ssid[i]!='_')return false;
    for(unsigned i=0;i<16;i++)if(!alnum_ascii((unsigned char)id->password[i]))return false;
    return true;
}
static int hex(char c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
bool device_identity_parse(const char *text,device_identity_t *id) {
    if(!text || !id || strlen(text)>=DEVICE_IDENTITY_BYTES || strncmp(text,"DVC1|",5))return false;
    size_t n=strlen(text);if(n<36 || text[17]!='|')return false;
    const char *sep=strchr(text+18,'|');
    if(!sep || sep==text+18 || sep-text-18>32 || strlen(sep+1)!=16)return false;
    device_identity_t v={0};
    for(unsigned i=0;i<6;i++) {int a=hex(text[5+2*i]),b=hex(text[6+2*i]);if(a<0||b<0)return false;v.mac[i]=(uint8_t)(a*16+b);}
    memcpy(v.ssid,text+18,(size_t)(sep-text-18));memcpy(v.password,sep+1,16);
    if(!device_identity_valid(&v))return false;
    *id=v;return true;
}
bool device_identity_format(const device_identity_t *id,char *text,size_t capacity) {
    if(!text || !device_identity_valid(id))return false;
    int n=snprintf(text,capacity,"DVC1|%02X%02X%02X%02X%02X%02X|%s|%s",id->mac[0],id->mac[1],id->mac[2],id->mac[3],id->mac[4],id->mac[5],id->ssid,id->password);
    return n>0 && (size_t)n<capacity;
}

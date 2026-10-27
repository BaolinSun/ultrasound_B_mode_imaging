/* Execute the production startup/runtime adapter and FSBL hook. Replace only
 * the ARM platform boundary; the reset/clock controller has its own full tests. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../../common/cpu1_control.h"
#define UB_CPU1_PLATFORM_H
static uint32_t control;
static unsigned actions,writes,logs;
static cpu1_error failure;
static char last[200];
uint32_t Xil_In32(uintptr_t address) { assert(address==CPU1_RESET_CTRL);return control; }
int xil_printf(const char *format, ...) {
    va_list ap;va_start(ap,format);int n=vsnprintf(last,sizeof last,format,ap);va_end(ap);logs++;return n;
}
static cpu1_result cpu1_platform_disable(void) {
    actions++;
    cpu1_result r={.before=control,.error=failure,.lock_restored=1};
    if(!failure&&(control&CPU1_DISABLED_MASK)!=CPU1_DISABLED_MASK){control|=CPU1_DISABLED_MASK;writes+=2;}
    r.after=control;return r;
}
#include "../../build/src/ps/system_ctrl/cpu1_power.c"
#include "../../build/src/fsbl/cpu1_fsbl.h"
int main(void) {
    const uint32_t cases[]={0,2,32,34,0xA5000100,0xA5000122};
    for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++) {
        control=cases[i];unsigned previous=writes,prior_actions=actions;
        cpu1_fsbl_before_handoff();assert(strstr(last,"startup=observe"));
        cpu1_power_init();assert(strstr(last,"startup=observe"));
        assert(cpu1_power_is_disabled()==((cases[i]&34)==34));
        assert(control==cases[i]&&writes==previous&&actions==prior_actions);
    }
    control=0xA5000100;assert(cpu1_power_disable()==NULL);
    assert(control==0xA5000122&&cpu1_power_is_disabled());
    unsigned previous=writes;assert(cpu1_power_disable()==NULL&&writes==previous);
    for(cpu1_error e=CPU1_NOT_CPU0;e<=CPU1_LOCK_RESTORE_FAILED;e++) {
        control=0xA5000100;failure=e;
        assert(!strcmp(cpu1_power_disable(),cpu1_error_name(e)));
        assert(!cpu1_power_is_disabled()&&control==0xA5000100&&writes==previous);
        assert(strstr(last,"WARNING"));
    }
    assert(logs>10);
    puts("PASS CPU1: app/FSBL startup and status read-only; explicit shutdown; repeated command; failures remain warnings");
    return 0;
}

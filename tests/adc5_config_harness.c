/* SPDX-License-Identifier: AGPL-3.0-only */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
#define ARRAY_SIZE(a) ((int)(sizeof(a)/sizeof((a)[0])))
#define ADC5_USR_DIG_PARAM 0x42
#define ADC5_USR_CH_SEL_CTL 0x44
#define ADC5_USR_CONV_REQ 0x47
#define ADC5_USR_CONV_REQ_REQ 0x80
#define ADC5_USR_FAST_AVG_CTL_SAMPLES_MASK 7
#define ADC5_USR_HW_SETTLE_DELAY_MASK 15
#define ADC5_USR_EN_CTL1_ADC_EN 0x80
#define ADC5_USR_STATUS1 8
#define ADC5_USR_STATUS1_CONV_FAULT 0x80
#define ADC5_USR_STATUS1_REQ_STS_EOC_MASK 3
#define ADC5_USR_STATUS1_EOC 1
struct adc5_chip { bool poll_eoc; int complete; };
struct adc5_channel_prop { int channel, avg_samples, hw_settle_time; };
static u8 registers[256];
static int accesses, fail_at, writes, requests, completions, corrupt_channel;
static int adc5_read(struct adc5_chip *c, int reg, u8 *data, int len)
{
 (void)c;
 if (++accesses == fail_at) return -EIO;
 memcpy(data, registers + reg, len);
 if (corrupt_channel && reg == ADC5_USR_CH_SEL_CTL) *data ^= 1;
 return 0;
}
static int adc5_write(struct adc5_chip *c, int reg, u8 *data, int len)
{
 (void)c;
 if (++accesses == fail_at) return -EIO;
 /* Model a channel/config update which does not latch in a bundled request.
  * Separate writes must finish before triggering conversion. */
 if (len != 1) return 0;
 if (reg == ADC5_USR_CONV_REQ) {
  assert(writes == 5 && completions == 1);
  assert(registers[ADC5_USR_CH_SEL_CTL] == 7 && *data == 0x80);
  requests++;
 } else {
  assert(reg == ADC5_USR_DIG_PARAM + writes);
  writes++;
 }
 registers[reg] = *data;
 return 0;
}
static void adc5_update_dig_param(struct adc5_chip *c,
 struct adc5_channel_prop *p,u8 *v) { (void)c;(void)p;*v=0x28; }
static void reinit_completion(int *v) { (void)v;completions++; }
/* DRIVER_UNDER_TEST */
static void reset(void)
{
 memset(registers, 0, sizeof(registers));
 accesses=fail_at=writes=requests=completions=corrupt_channel=0;
}
int main(void)
{
 struct adc5_chip c={0};
 struct adc5_channel_prop p={.channel=7};
 reset(); assert(!adc5_configure(&c,&p) && requests==1 && writes==5);
 int total=accesses;
 for(int i=1;i<=total;i++) {
  reset();fail_at=i;
  assert(adc5_configure(&c,&p)==-EIO && requests==0);
 }
 reset();corrupt_channel=1;
 assert(adc5_configure(&c,&p)==-EIO && !requests);
 reset();registers[8]=1;registers[0x44]=7;
 assert(!adc5_validate_result(&c,&p));
 for(int status=0;status<256;status++) {
  registers[8]=status;
  int expected=((status&0x80) || (status&3)!=1)?-EIO:0;
  assert(adc5_validate_result(&c,&p)==expected);
 }
 registers[8]=1;registers[0x44]=8;
 assert(adc5_validate_result(&c,&p)==-EIO);
 registers[0x44]=7;
 for(int i=1;i<=2;i++) {
  accesses=0;fail_at=i;
  assert(adc5_validate_result(&c,&p)==-EIO);
 }
 return 0;
}

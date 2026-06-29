#include "runefetch.h"

#include <string.h>

typedef struct {
	u32 animation;
	u8 red_pattern[32];
	u8 green_pattern[32];
	u8 blue_pattern[32];
} RfLedPattern;

#define RF_LED_ANIMATION(delay, smoothing, loop_delay) \
	(((delay) & 0xFF) | (((smoothing) & 0xFF) << 8) | (((loop_delay) & 0xFF) << 16))

static bool g_led_failed;
static int g_last_step = -1;

static Result set_pattern(RfLedPattern *pattern)
{
	u32 *cmdbuf = getThreadCommandBuffer();
	cmdbuf[0] = 0x08010640;
	cmdbuf[1] = pattern->animation;
	memcpy(&cmdbuf[2], pattern->red_pattern, 32);
	memcpy(&cmdbuf[10], pattern->green_pattern, 32);
	memcpy(&cmdbuf[18], pattern->blue_pattern, 32);
	return svcSendSyncRequest(*ptmSysmGetSessionHandle());
}

static void solid(u8 r, u8 g, u8 b)
{
	if(g_led_failed) return;

	RfLedPattern p;
	memset(&p, 0, sizeof(p));
	p.animation = RF_LED_ANIMATION(0, 0xFF, 0);
	memset(p.red_pattern, r, sizeof(p.red_pattern));
	memset(p.green_pattern, g, sizeof(p.green_pattern));
	memset(p.blue_pattern, b, sizeof(p.blue_pattern));

	Result res = set_pattern(&p);
	if(R_FAILED(res))
		g_led_failed = true;
}

static void blink(u8 r, u8 g, u8 b)
{
	if(g_led_failed) return;

	RfLedPattern p;
	memset(&p, 0, sizeof(p));
	p.animation = RF_LED_ANIMATION(0x20, 0x00, 0xFF);
	for(size_t i = 0; i < 32; ++i)
	{
		bool on = (i / 4) % 2 == 0;
		p.red_pattern[i] = on ? r : 0;
		p.green_pattern[i] = on ? g : 0;
		p.blue_pattern[i] = on ? b : 0;
	}

	Result res = set_pattern(&p);
	if(R_FAILED(res))
		g_led_failed = true;
}

void rf_led_init(void)
{
	g_led_failed = false;
	g_last_step = -1;
	rf_led_off();
}

void rf_led_progress(u64 done, u64 total)
{
	int percent = total ? (int)((done * 100) / total) : 0;
	if(percent < 0) percent = 0;
	if(percent > 100) percent = 100;

	int step = percent / 5;
	if(step == g_last_step)
		return;
	g_last_step = step;

	u8 green = (u8)((percent * 255) / 100);
	u8 blue = (u8)(255 - green);
	solid(0, green, blue);
}

void rf_led_ready(void)
{
	g_last_step = -1;
	blink(0, 0xFF, 0);
}

void rf_led_error(void)
{
	g_last_step = -1;
	blink(0xFF, 0, 0);
}

void rf_led_paused(void)
{
	g_last_step = -1;
	solid(0x80, 0, 0xFF);
}

void rf_led_off(void)
{
	if(g_led_failed) return;

	RfLedPattern p;
	memset(&p, 0, sizeof(p));
	p.animation = RF_LED_ANIMATION(0, 0, 0);
	Result res = set_pattern(&p);
	if(R_FAILED(res))
		g_led_failed = true;
}

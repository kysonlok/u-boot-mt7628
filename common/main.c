/*
 * (C) Copyright 2000
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/* #define	DEBUG	*/

#include <common.h>
#include <watchdog.h>
#include <command.h>
#include <malloc.h>

#ifdef CFG_HUSH_PARSER
#include <hush.h>
#endif

#include <post.h>

#include <gpio.h>
extern void LED_INIT(void);
extern void ALL_LED_STATUS(enum led_status status);
extern void LED_STATUS(enum led_gpio led, enum led_status status);

extern int eth_initialize(bd_t *bis); /* Initialize network subsystem */
extern int NetLoopHttpd(void);

#if defined(CONFIG_BOOT_RETRY_TIME) && defined(CONFIG_RESET_TO_RETRY)
extern int do_reset (cmd_tbl_t *cmdtp, int flag, int argc, char *argv[]);		/* for do_reset() prototype */
#endif

extern int do_bootd (cmd_tbl_t *cmdtp, int flag, int argc, char *argv[]);


#define MAX_DELAY_STOP_STR 32

static char * delete_char (char *buffer, char *p, int *colp, int *np, int plen);
static int parse_line (char *, char *[]);
#if defined(CONFIG_BOOTDELAY) && (CONFIG_BOOTDELAY >= 0)
static int abortboot(int);
#endif

#undef DEBUG_PARSER

extern char console_buffer[CFG_CBSIZE];		/* console I/O buffer	*/

#define CONFIG_CMD_HISTORY
#ifdef CONFIG_CMD_HISTORY
#define     HISTORY_SIZE	10
char 		console_history[HISTORY_SIZE][CFG_CBSIZE];
static  int	history_cur_idx = -1;
static  int	history_last_idx = -1;
static  int history_counter = 0;
static  int history_enable = 0;
#endif

#ifdef CONFIG_BOOT_RETRY_TIME
static uint64_t endtime = 0;  /* must be set, default is instant timeout */
static int      retry_time = -1; /* -1 so can call readline before main_loop */
#endif

#define	endtick(seconds) (get_ticks() + (uint64_t)(seconds) * get_tbclk())

#ifndef CONFIG_BOOT_RETRY_MIN
#define CONFIG_BOOT_RETRY_MIN CONFIG_BOOT_RETRY_TIME
#endif

#ifdef CONFIG_MODEM_SUPPORT
int do_mdm_init = 0;
extern void mdm_init(void); /* defined in board.c */
#endif

/***************************************************************************
 * Watch for 'delay' seconds for autoboot stop or autoboot delay string.
 * returns: 0 -  no key string, allow autoboot
 *          1 - got key string, abort
 */
#if defined(CONFIG_BOOTDELAY) && (CONFIG_BOOTDELAY >= 0)
# if defined(CONFIG_AUTOBOOT_KEYED)
static __inline__ int abortboot(int bootdelay)
{
	int abort = 0;
	uint64_t etime = endtick(bootdelay);
	struct
	{
		char* str;
		u_int len;
		int retry;
	}
	delaykey [] =
	{
		{ str: getenv ("bootdelaykey"),  retry: 1 },
		{ str: getenv ("bootdelaykey2"), retry: 1 },
		{ str: getenv ("bootstopkey"),   retry: 0 },
		{ str: getenv ("bootstopkey2"),  retry: 0 },
	};

	char presskey [MAX_DELAY_STOP_STR];
	u_int presskey_len = 0;
	u_int presskey_max = 0;
	u_int i;

#ifdef CONFIG_SILENT_CONSOLE
	{
		DECLARE_GLOBAL_DATA_PTR;

		if (gd->flags & GD_FLG_SILENT) {
			/* Restore serial console */
			console_assign (stdout, "serial");
			console_assign (stderr, "serial");
		}
	}
#endif

#  ifdef CONFIG_AUTOBOOT_PROMPT
	printf (CONFIG_AUTOBOOT_PROMPT, bootdelay);
#  endif

#  ifdef CONFIG_AUTOBOOT_DELAY_STR
	if (delaykey[0].str == NULL)
		delaykey[0].str = CONFIG_AUTOBOOT_DELAY_STR;
#  endif
#  ifdef CONFIG_AUTOBOOT_DELAY_STR2
	if (delaykey[1].str == NULL)
		delaykey[1].str = CONFIG_AUTOBOOT_DELAY_STR2;
#  endif
#  ifdef CONFIG_AUTOBOOT_STOP_STR
	if (delaykey[2].str == NULL)
		delaykey[2].str = CONFIG_AUTOBOOT_STOP_STR;
#  endif
#  ifdef CONFIG_AUTOBOOT_STOP_STR2
	if (delaykey[3].str == NULL)
		delaykey[3].str = CONFIG_AUTOBOOT_STOP_STR2;
#  endif

	for (i = 0; i < sizeof(delaykey) / sizeof(delaykey[0]); i ++) {
		delaykey[i].len = delaykey[i].str == NULL ?
				    0 : strlen (delaykey[i].str);
		delaykey[i].len = delaykey[i].len > MAX_DELAY_STOP_STR ?
				    MAX_DELAY_STOP_STR : delaykey[i].len;

		presskey_max = presskey_max > delaykey[i].len ?
				    presskey_max : delaykey[i].len;

#  if DEBUG_BOOTKEYS
		printf("%s key:<%s>\n",
		       delaykey[i].retry ? "delay" : "stop",
		       delaykey[i].str ? delaykey[i].str : "NULL");
#  endif
	}

	/* In order to keep up with incoming data, check timeout only
	 * when catch up.
	 */
	while (!abort && get_ticks() <= etime) {
		for (i = 0; i < sizeof(delaykey) / sizeof(delaykey[0]); i ++) {
			if (delaykey[i].len > 0 &&
			    presskey_len >= delaykey[i].len &&
			    memcmp (presskey + presskey_len - delaykey[i].len,
				    delaykey[i].str,
				    delaykey[i].len) == 0) {
#  if DEBUG_BOOTKEYS
				printf("got %skey\n",
				       delaykey[i].retry ? "delay" : "stop");
#  endif

#  ifdef CONFIG_BOOT_RETRY_TIME
				/* don't retry auto boot */
				if (! delaykey[i].retry)
					retry_time = -1;
#  endif
				abort = 1;
			}
		}

		if (tstc()) {
			if (presskey_len < presskey_max) {
				presskey [presskey_len ++] = getc();
			}
			else {
				for (i = 0; i < presskey_max - 1; i ++)
					presskey [i] = presskey [i + 1];

				presskey [i] = getc();
			}
		}
	}
#  if DEBUG_BOOTKEYS
	if (!abort)
		puts ("key timeout\n");
#  endif

#ifdef CONFIG_SILENT_CONSOLE
	{
		DECLARE_GLOBAL_DATA_PTR;

		if (abort) {
			/* permanently enable normal console output */
			gd->flags &= ~(GD_FLG_SILENT);
		} else if (gd->flags & GD_FLG_SILENT) {
			/* Restore silent console */
			console_assign (stdout, "nulldev");
			console_assign (stderr, "nulldev");
		}
	}
#endif

	return abort;
}

# else	/* !defined(CONFIG_AUTOBOOT_KEYED) */

#ifdef CONFIG_MENUKEY
static int menukey = 0;
#endif

static __inline__ int abortboot(int bootdelay)
{
	int abort = 0;

#ifdef CONFIG_SILENT_CONSOLE
	{
		DECLARE_GLOBAL_DATA_PTR;

		if (gd->flags & GD_FLG_SILENT) {
			/* Restore serial console */
			console_assign (stdout, "serial");
			console_assign (stderr, "serial");
		}
	}
#endif
#if 1 /* Add by ksyon */

	
	printf("   *** ***         ** ** *****  ***   ***   ***    *    ** ***\n");
	printf("  *  *  *          ** ** * * * *   * *   * *   *   *     *  * \n");
	printf(" *      *          ** **   *       * *   * *   *   **    ** * \n");
	printf(" *      *    ***** ** **   *     **  *   * *   *  * *    ** * \n");
	printf(" *  *** *          * * *   *       * *   * *   *  * *    * ** \n");
	printf(" *   *  *          * * *   *       * *   * *   *  ****   * ** \n");
	printf("  *  *  *   *      * * *   *   *   * *   * *   *  *  *   *  * \n");
	printf("   **  ******      * * *  ***   ***   ***   ***  **  ** *** * \n");

	printf("\n\n");
#endif /* Add end */

#ifdef CONFIG_MENUPROMPT
	printf(CONFIG_MENUPROMPT, bootdelay);
#else
	/* printf("Hit any key to stop autoboot: %2d ", bootdelay); */
	printf("Hit SPACE to stop autoboot: %2d ", bootdelay);
#endif

#if defined CONFIG_ZERO_BOOTDELAY_CHECK
	/*
	 * Check if key already pressed
	 * Don't check if bootdelay < 0
	 */
	if (bootdelay >= 0) {
		if (tstc()) {	/* we got a key press	*/
			(void) getc();  /* consume input	*/
			puts ("\b\b\b 0");
			abort = 1; 	/* don't auto boot	*/
		}
	}
#endif

#if 0 /* Delete by ksyon */
	while ((bootdelay > 0) && (!abort)) {
		int i;

		--bootdelay;
		/* delay 100 * 10ms */
		for (i=0; !abort && i<100; ++i) {
			if (tstc()) {	/* we got a key press	*/
				abort  = 1;	/* don't auto boot	*/
				bootdelay = 0;	/* no more delay	*/
# ifdef CONFIG_MENUKEY
				menukey = getc();
# else
				(void) getc();  /* consume input	*/
# endif
				break;
			}
			udelay (10000);
		}

		printf ("\b\b\b%2d ", bootdelay);
	}
#endif /* Delete end */
#if 1 /* Add by ksyon */
	int key = 0;

	while ((bootdelay > 0) && (!abort)) {
			int i;

			--bootdelay;
			/* delay 100 * 10ms */
			for (i=0; !abort && i<100; ++i) {
				if (tstc()) {	/* we got a key press	*/
					key = getc();/* consume input	*/
					if(key == 32){
						abort  = 1;	/* don't auto boot	*/
						bootdelay = 0;	/* no more delay	*/
						break;
					}
				}
				udelay (10000);
			}

			printf ("\b\b\b%2d ", bootdelay);
		}
#endif /* Add end*/

	putc ('\n');

#ifdef CONFIG_SILENT_CONSOLE
	{
		DECLARE_GLOBAL_DATA_PTR;

		if (abort) {
			/* permanently enable normal console output */
			gd->flags &= ~(GD_FLG_SILENT);
		} else if (gd->flags & GD_FLG_SILENT) {
			/* Restore silent console */
			console_assign (stdout, "nulldev");
			console_assign (stderr, "nulldev");
		}
	}
#endif

	return abort;
}
# endif	/* CONFIG_AUTOBOOT_KEYED */
#endif	/* CONFIG_BOOTDELAY >= 0  */

void test_read_flash(void)
{
	int i = 0;
	u8 *p = NULL;

	p = (char *)malloc(2);
	raspi_read(p, 0xbca50000, 2);
	for (i = 0; i < 2; i++) {
		printf("%x ", p[i]);
	}
	printf("\n");
	free(p);
}

/**
 * find_calibration_data
 * @return: 
 *		0: miss default calibration data
 *		1: miss default calibration data
 */
int find_calibration_data(void)
{
	int ret = 0;
	volatile unsigned short *cal_data = (volatile unsigned short *)0xbc040000;
	if (*cal_data == 0x7628) {
		ret = 1;
		printf("Device have ART,checking calibration status...\n");
	} else {
		ret = 0;
		printf("Device don't have ART,please write the default ART first...\n");
	}

	return ret;
}

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/**
 * check_calibration
 * @return:
 *		0: have calibration
 *		other: have not calibration
 */
int check_calibration(void)
{
	int ret = 0;
	int i = 0;
	unsigned int flash_cal_addr[] = {0xbc040058, 0xbc04005e,};
	unsigned int len = 0;
	u8 *p = NULL;

	len = ARRAY_SIZE(flash_cal_addr);
	p = (u8 *)malloc(len);

	for (i = 0; i < len; i++) {
		ret = raspi_read(&p[i], flash_cal_addr[i], 1);
		if (ret < 0) {
			goto error;
		}
	}

	if ((p[0] == 0x27) && (p[1] == 0x27)) {
		ret = -1;
		printf("Device haven't calibrated,booting the calibration firmware...\n");
	} else {
		ret = 0;
		printf("Device have calibrated,checking MAC info...\n");
	}

error:
	if (p) {
		free(p);
	}
	return ret;
}

/**
 * check_config
 * @return:
 *		0: have config data
 *		other: have not config data
 */
int check_config(void)
{
	int ret = 0;
	int i = 0;
	unsigned int flash_addr = 0xbc048004;
	unsigned int len = 0;
	u8 *p = NULL;

	union mac_add {
		u8 mac_add_str[6];
		unsigned int mac_add_hex;
	};

	union mac_add mac;

	len = 6;
	p = (u8 *)malloc(len);
	
	ret = raspi_read(p, flash_addr, 6);
	if (ret < 0) {
		goto error;
	}

	for (i = 0; i < len; i++) {
		mac.mac_add_str[i] = p[i];
	}

	if (mac.mac_add_hex == 0xffffffff) {
		ret = -1;
		printf("Device don't have MAC info,please write MAC in calibration firmware...\n");
	} else {
		ret = 0;
		printf("Device have MAC info,starting firmware...\n");
	}

error:
	if (p) {
		free(p);
	}
	return ret;
}

/**
 * check_test
 * @return:
 *		0: have not test
 *		1: have test
 */
int check_test(void) 
{
	int ret = 0;
	volatile unsigned char *f1f = (volatile unsigned char *)0xbc04ff00;
	volatile unsigned char *f2i = (volatile unsigned char *)0xbc04ff01;
	volatile unsigned char *f3r = (volatile unsigned char *)0xbc04ff02;
	volatile unsigned char *f4s = (volatile unsigned char *)0xbc04ff03;
	volatile unsigned char *f5t = (volatile unsigned char *)0xbc04ff04;
	volatile unsigned char *f6t = (volatile unsigned char *)0xbc04ff05;
	volatile unsigned char *f7e = (volatile unsigned char *)0xbc04ff06;
	volatile unsigned char *f8s = (volatile unsigned char *)0xbc04ff07;
	volatile unsigned char *f9t = (volatile unsigned char *)0xbc04ff08;

	volatile unsigned char *s0s = (volatile unsigned char *)0xbc04ff10;
	volatile unsigned char *s1e = (volatile unsigned char *)0xbc04ff11;
	volatile unsigned char *s2c = (volatile unsigned char *)0xbc04ff12;
	volatile unsigned char *s3o = (volatile unsigned char *)0xbc04ff13;
	volatile unsigned char *s4n = (volatile unsigned char *)0xbc04ff14;
	volatile unsigned char *s5d = (volatile unsigned char *)0xbc04ff15;
	volatile unsigned char *s6t = (volatile unsigned char *)0xbc04ff16;
	volatile unsigned char *s7e = (volatile unsigned char *)0xbc04ff17;
	volatile unsigned char *s8s = (volatile unsigned char *)0xbc04ff18;
	volatile unsigned char *s9t = (volatile unsigned char *)0xbc04ff19;

	if (*f1f==0x66 && *f2i==0x69 && *f3r==0x72 && *f4s==0x73 && \
			*f5t==0x74 && *f6t==0x74 && *f7e==0x65 && *f8s==0x73 && \
			*f9t==0x74 && \
			*s0s==0x73 && *s1e==0x65 && *s2c==0x63 && *s3o==0x6f && \
			*s4n==0x6e && *s5d==0x64 && *s6t==0x74 && *s7e==0x65 && \
			*s8s==0x73 && *s9t==0x74) {
		ret = 1;
		printf("Device haven tested,starting firmware...\n");
	} else {
		ret = 0;
		printf("Device haven't tested,please test device in calibration firmware...\n");
	}

	return ret;
}

/****************************************************************************/


void main_loop (void)
{
#ifndef CFG_HUSH_PARSER
	static char lastcommand[CFG_CBSIZE] = { 0, };
	int len;
	int rc = 1;
	int flag;
#endif

#if defined(CONFIG_BOOTDELAY) && (CONFIG_BOOTDELAY >= 0)
	char *s;
	int bootdelay;
#endif
#ifdef CONFIG_PREBOOT
	char *p;
#endif
#ifdef CONFIG_BOOTCOUNT_LIMIT
	unsigned long bootcount = 0;
	unsigned long bootlimit = 0;
	char *bcs;
	char bcs_set[16];
#endif /* CONFIG_BOOTCOUNT_LIMIT */
#if defined(CONFIG_VFD) && defined(VFD_TEST_LOGO)
	ulong bmp = 0;		/* default bitmap */
	extern int trab_vfd (ulong bitmap);

#ifdef CONFIG_MODEM_SUPPORT
	if (do_mdm_init)
		bmp = 1;	/* alternate bitmap */
#endif
	trab_vfd (bmp);
#endif	/* CONFIG_VFD && VFD_TEST_LOGO */

#ifdef CONFIG_BOOTCOUNT_LIMIT
	bootcount = bootcount_load();
	bootcount++;
	bootcount_store (bootcount);
	sprintf (bcs_set, "%lu", bootcount);
	setenv ("bootcount", bcs_set);
	bcs = getenv ("bootlimit");
	bootlimit = bcs ? simple_strtoul (bcs, NULL, 10) : 0;
#endif /* CONFIG_BOOTCOUNT_LIMIT */

#ifdef CONFIG_MODEM_SUPPORT
	debug ("DEBUG: main_loop:   do_mdm_init=%d\n", do_mdm_init);
	if (do_mdm_init) {
		uchar *str = strdup(getenv("mdm_cmd"));
		setenv ("preboot", str);  /* set or delete definition */
		if (str != NULL)
			free (str);
		mdm_init(); /* wait for modem connection */
	}
#endif  /* CONFIG_MODEM_SUPPORT */

#ifdef CONFIG_VERSION_VARIABLE
	{
		extern char version_string[];

		setenv ("ver", version_string);  /* set version variable */
	}
#endif /* CONFIG_VERSION_VARIABLE */

#ifdef CFG_HUSH_PARSER
	u_boot_hush_start ();
#endif

#ifdef CONFIG_AUTO_COMPLETE
	install_auto_complete();
#endif

#ifdef CONFIG_PREBOOT
	if ((p = getenv ("preboot")) != NULL) {
# ifdef CONFIG_AUTOBOOT_KEYED
		int prev = disable_ctrlc(1);	/* disable Control C checking */
# endif

# ifndef CFG_HUSH_PARSER
		run_command (p, 0);
# else
        printf("\n parse_string_outer \n");
		parse_string_outer(p, FLAG_PARSE_SEMICOLON |
				    FLAG_EXIT_FROM_LOOP);
# endif

# ifdef CONFIG_AUTOBOOT_KEYED
		disable_ctrlc(prev);	/* restore Control C checking */
# endif
	}
#endif /* CONFIG_PREBOOT */
#if defined(CONFIG_BOOTDELAY) && (CONFIG_BOOTDELAY >= 0)
	s = getenv ("bootdelay");
	bootdelay = s ? (int)simple_strtol(s, NULL, 10) : CONFIG_BOOTDELAY;

	//debug ("### main_loop entered: bootdelay=%d\n", bootdelay);

# ifdef CONFIG_BOOT_RETRY_TIME
	init_cmd_timeout ();
# endif	/* CONFIG_BOOT_RETRY_TIME */

#ifdef CONFIG_BOOTCOUNT_LIMIT
	if (bootlimit && (bootcount > bootlimit)) {
		printf ("Warning: Bootlimit (%u) exceeded. Using altbootcmd.\n",
		        (unsigned)bootlimit);
		s = getenv ("altbootcmd");
	}
	else
#endif /* CONFIG_BOOTCOUNT_LIMIT */
		s = getenv ("bootcmd");

	LED_INIT();
	ALL_LED_STATUS(LED_OFF);

#if 1 /* Support upgrade for web by kyson<luoweilong@gl-inet.com> */
	int counter = 0;
	// wait 0,5s
	udelay(500000);
	
	printf( "\nPress press WPS button for more than 2 seconds to run web failsafe mode\n\n" );
	
	printf( "WPS button is pressed for: %2d second(s)", counter);
	
	while( DETECT_BTN_RESET() ) {
	
		// LED ON and wait 0.2s
		LED_STATUS(WLAN_LED, LED_ON);
		udelay( 200000 );
	
		// LED OFF and wait 0.8s
		LED_STATUS(WLAN_LED, LED_OFF);
		udelay( 800000 );
	
		counter++;
	
		// how long the WPS button is pressed?
		printf("\b\b\b\b\b\b\b\b\b\b\b\b%2d second(s)", counter);
	
		if ( !DETECT_BTN_RESET() ){
			break;
		}
	
		if ( counter >= 4 ){
			break;
		}
	}
	
	/* LEDOFF(); */

	if ( counter > 0 ) {
	
		printf( "\n\nWPS button was pressed for %d seconds\nHTTP server is starting for firmware update...\n\n", counter );
		/* eth_initialize(gd->bd); */
		LED_STATUS(WLAN_LED, LED_ON);
		NetLoopHttpd();
	} else {
		printf( "\n\nCatution: WPS button wasn't pressed or not long enough!\nContinuing normal boot...\n\n" );
	}
#endif /* Support upgrade for web end */


	//debug ("### main_loop: bootcmd=\"%s\"\n", s ? s : "<UNDEFINED>");
	/* bootdelay = -1; */
	bootdelay = CONFIG_BOOTDELAY;
	if (bootdelay >= 0 && s && !abortboot (bootdelay)) {
		/* test_read_flash(); */
		/* Check does calibration data exist or not */
#if 0
		if (find_calibration_data()) {
			if (check_calibration()) {
				if (check_config()) {
					if (check_test()) {
						run_command("bootm 0xbc050000", 0);
					} else {
						run_command("bootm 0xbca50000", 0);
						goto mainloop;
					}
				} else {
					run_command("bootm 0xbca50000", 0);
					goto mainloop;
				}
			} else {
				run_command("bootm 0xbca50000", 0);
				goto mainloop;
			}
		} else {
			goto mainloop;
		}
#endif
		/*
		if (find_calibration_data() && 
				check_calibration() &&
				check_config() &&
				check_test()) {
			run_command("bootm 0xbc050000", 0);
		} else {
			run_command("bootm 0xbca00000",0);
			goto mainloop;
		} */
	if (!check_calibration()) {
		if (!check_config()) {
			run_command("bootm 0xbc050000", 0);
		} else {
			run_command("run lc", 0);
			run_command("re", 0);
			goto mainloop;
		}
	} else {
		run_command("bootm 0xbca50000", 0);
		goto mainloop;
	}

#if 1 /* Add by kyson */
	run_command(s, 0);
#endif /* Add end */

# ifdef CONFIG_AUTOBOOT_KEYED
		int prev = disable_ctrlc(1);	/* disable Control C checking */
# endif

# ifndef CFG_HUSH_PARSER
		run_command (s, 0);
# else
		parse_string_outer(s, FLAG_PARSE_SEMICOLON |
				    FLAG_EXIT_FROM_LOOP);
# endif

# ifdef CONFIG_AUTOBOOT_KEYED
		disable_ctrlc(prev);	/* restore Control C checking */
# endif
	}

# ifdef CONFIG_MENUKEY
	if (menukey == CONFIG_MENUKEY) {
	    s = getenv("menucmd");
	    if (s) {
# ifndef CFG_HUSH_PARSER
		run_command (s, 0);
# else
		parse_string_outer(s, FLAG_PARSE_SEMICOLON |
				    FLAG_EXIT_FROM_LOOP);
# endif
	    }
	}
#endif /* CONFIG_MENUKEY */
#endif	/* CONFIG_BOOTDELAY */

#ifdef CONFIG_AMIGAONEG3SE
	{
	    extern void video_banner(void);
	    video_banner();
	}
#endif

	/*
	 * Main Loop for Monitor Command Processing
	 */
mainloop:

#ifdef CFG_HUSH_PARSER
	parse_file_outer();
	/* This point is never reached */
	for (;;);
#else
	for (;;) {
#ifdef CONFIG_BOOT_RETRY_TIME
		if (rc >= 0) {
			/* Saw enough of a valid command to
			 * restart the timeout.
			 */
			reset_cmd_timeout();
		}
#endif
		len = readline (CFG_PROMPT, 0);

		flag = 0;	/* assume no special flags for now */
		if (len > 0)
			strcpy (lastcommand, console_buffer);
		else if (len == 0)
			flag |= CMD_FLAG_REPEAT;
#ifdef CONFIG_BOOT_RETRY_TIME
		else if (len == -2) {
			/* -2 means timed out, retry autoboot
			 */
			puts ("\nTimed out waiting for command\n");
# ifdef CONFIG_RESET_TO_RETRY
			/* Reinit board to run initialization code again */
			do_reset (NULL, 0, 0, NULL);
# else
			return;		/* retry autoboot */
# endif
		}
#endif
		if (len == -1)
			puts ("<INTERRUPT>\n");
		else
			rc = run_command (lastcommand, flag);

		if (rc <= 0) {
			/* invalid command or not repeatable, forget it */
			lastcommand[0] = 0;
		}
	}
#endif /*CFG_HUSH_PARSER*/
}

#ifdef CONFIG_BOOT_RETRY_TIME
/***************************************************************************
 * initialise command line timeout
 */
void init_cmd_timeout(void)
{
	char *s = getenv ("bootretry");

	if (s != NULL)
		retry_time = (int)simple_strtol(s, NULL, 10);
	else
		retry_time =  CONFIG_BOOT_RETRY_TIME;

	if (retry_time >= 0 && retry_time < CONFIG_BOOT_RETRY_MIN)
		retry_time = CONFIG_BOOT_RETRY_MIN;
}

/***************************************************************************
 * reset command line timeout to retry_time seconds
 */
void reset_cmd_timeout(void)
{
	endtime = endtick(retry_time);
}
#endif

/****************************************************************************/

int parse_line (char *line, char *argv[])
{
	int nargs = 0;

#ifdef DEBUG_PARSER
	printf ("parse_line: \"%s\"\n", line);
#endif
	while (nargs < CFG_MAXARGS) {

		/* skip any white space */
		while ((*line == ' ') || (*line == '\t')) {
			++line;
		}

		if (*line == '\0') {	/* end of line, no more args	*/
			argv[nargs] = NULL;
#ifdef DEBUG_PARSER
		printf ("parse_line: nargs=%d\n", nargs);
#endif
			return (nargs);
		}

		argv[nargs++] = line;	/* begin of argument string	*/

		/* find end of string */
		while (*line && (*line != ' ') && (*line != '\t')) {
			++line;
		}

		if (*line == '\0') {	/* end of line, no more args	*/
			argv[nargs] = NULL;
#ifdef DEBUG_PARSER
		printf ("parse_line: nargs=%d\n", nargs);
#endif
			return (nargs);
		}

		*line++ = '\0';		/* terminate current arg	 */
	}

	printf ("** Too many args (max. %d) **\n", CFG_MAXARGS);

#ifdef DEBUG_PARSER
	printf ("parse_line: nargs=%d\n", nargs);
#endif
	return (nargs);
}

/****************************************************************************/

static void process_macros (const char *input, char *output)
{
	char c, prev;
	const char *varname_start = NULL;
	int inputcnt  = strlen (input);
	int outputcnt = CFG_CBSIZE;
	int state = 0;	/* 0 = waiting for '$'	*/
			/* 1 = waiting for '(' or '{' */
			/* 2 = waiting for ')' or '}' */
			/* 3 = waiting for '''  */
#ifdef DEBUG_PARSER
	char *output_start = output;

	printf ("[PROCESS_MACROS] INPUT len %d: \"%s\"\n", strlen(input), input);
#endif

	prev = '\0';			/* previous character	*/

	while (inputcnt && outputcnt) {
	    c = *input++;
	    inputcnt--;

	    if (state!=3) {
	    /* remove one level of escape characters */
	    if ((c == '\\') && (prev != '\\')) {
		if (inputcnt-- == 0)
			break;
		prev = c;
		c = *input++;
	    }
	    }

	    switch (state) {
	    case 0:			/* Waiting for (unescaped) $	*/
		if ((c == '\'') && (prev != '\\')) {
			state = 3;
			break;
		}
		if ((c == '$') && (prev != '\\')) {
			state++;
		} else {
			*(output++) = c;
			outputcnt--;
		}
		break;
	    case 1:			/* Waiting for (	*/
		if (c == '(' || c == '{') {
			state++;
			varname_start = input;
		} else {
			state = 0;
			*(output++) = '$';
			outputcnt--;

			if (outputcnt) {
				*(output++) = c;
				outputcnt--;
			}
		}
		break;
	    case 2:			/* Waiting for )	*/
		if (c == ')' || c == '}') {
			int i;
			char envname[CFG_CBSIZE], *envval;
			int envcnt = input-varname_start-1; /* Varname # of chars */

			/* Get the varname */
			for (i = 0; i < envcnt; i++) {
				envname[i] = varname_start[i];
			}
			envname[i] = 0;

			/* Get its value */
			envval = getenv (envname);

			/* Copy into the line if it exists */
			if (envval != NULL)
				while ((*envval) && outputcnt) {
					*(output++) = *(envval++);
					outputcnt--;
				}
			/* Look for another '$' */
			state = 0;
		}
		break;
	    case 3:			/* Waiting for '	*/
		if ((c == '\'') && (prev != '\\')) {
			state = 0;
		} else {
			*(output++) = c;
			outputcnt--;
		}
		break;
	    }
	    prev = c;
	}

	if (outputcnt)
		*output = 0;

#ifdef DEBUG_PARSER
	printf ("[PROCESS_MACROS] OUTPUT len %d: \"%s\"\n",
		strlen(output_start), output_start);
#endif
}


/****************************************************************************
 * returns:
 *	1  - command executed, repeatable
 *	0  - command executed but not repeatable, interrupted commands are
 *	     always considered not repeatable
 *	-1 - not executed (unrecognized, bootd recursion or too many args)
 *           (If cmd is NULL or "" or longer than CFG_CBSIZE-1 it is
 *           considered unrecognized)
 *
 * WARNING:
 *
 * We must create a temporary copy of the command since the command we get
 * may be the result from getenv(), which returns a pointer directly to
 * the environment data, which may change magicly when the command we run
 * creates or modifies environment variables (like "bootp" does).
 */
/****************************************************************************
 * returns:
 *	1  - command executed, repeatable
 *	0  - command executed but not repeatable, interrupted commands are
 *	     always considered not repeatable
 *	-1 - not executed (unrecognized, bootd recursion or too many args)
 *           (If cmd is NULL or "" or longer than CFG_CBSIZE-1 it is
 *           considered unrecognized)
 *
 * WARNING:
 *
 * We must create a temporary copy of the command since the command we get
 * may be the result from getenv(), which returns a pointer directly to
 * the environment data, which may change magicly when the command we run
 * creates or modifies environment variables (like "bootp" does).
 */

int run_command (const char *cmd, int flag)
{
	cmd_tbl_t *cmdtp;
	char cmdbuf[CFG_CBSIZE];	/* working copy of cmd		*/
	char *token;			/* start of token in cmdbuf	*/
	char *sep;			/* end of token (separator) in cmdbuf */
	char finaltoken[CFG_CBSIZE];
	char *str = cmdbuf;
	char *argv[CFG_MAXARGS + 1];	/* NULL terminated	*/
	int argc, inquotes;
	int repeatable = 1;
	int rc = 0;

#ifdef DEBUG_PARSER
	printf ("[RUN_COMMAND] cmd[%p]=\"", cmd);
	puts (cmd ? cmd : "NULL");	/* use puts - string may be loooong */
	puts ("\"\n");
#endif

	clear_ctrlc();		/* forget any previous Control C */

	if (!cmd || !*cmd) {
		return -1;	/* empty command */
	}

	if (strlen(cmd) >= CFG_CBSIZE) {
		puts ("## Command too long!\n");
		return -1;
	}

	strcpy (cmdbuf, cmd);

	/* Process separators and check for invalid
	 * repeatable commands
	 */

#ifdef DEBUG_PARSER
	printf ("[PROCESS_SEPARATORS] %s\n", cmd);
#endif
	while (*str) {

		/*
		 * Find separator, or string end
		 * Allow simple escape of ';' by writing "\;"
		 */
		for (inquotes = 0, sep = str; *sep; sep++) {
			if ((*sep=='\'') &&
			    (*(sep-1) != '\\'))
				inquotes=!inquotes;

			if (!inquotes &&
			    (*sep == ';') &&	/* separator		*/
			    ( sep != str) &&	/* past string start	*/
			    (*(sep-1) != '\\'))	/* and NOT escaped	*/
				break;
		}

		/*
		 * Limit the token to data between separators
		 */
		token = str;
		if (*sep) {
			str = sep + 1;	/* start of command for next pass */
			*sep = '\0';
		}
		else
			str = sep;	/* no more commands for next pass */
#ifdef DEBUG_PARSER
		printf ("token: \"%s\"\n", token);
#endif

		/* find macros in this token and replace them */
		process_macros (token, finaltoken);

		/* Extract arguments */
		argc = parse_line (finaltoken, argv);

		/* Look up command in command table */
		if ((cmdtp = find_cmd(argv[0])) == NULL) {
			printf ("Unknown command '%s' - try 'help'\n", argv[0]);
			rc = -1;	/* give up after bad command */
			continue;
		}

		/* found - check max args */
		if (argc > cmdtp->maxargs) {
			printf ("Usage:\n%s\n", cmdtp->usage);
			rc = -1;
			continue;
		}

#if (CONFIG_COMMANDS & CFG_CMD_BOOTD)
		/* avoid "bootd" recursion */
		if (cmdtp->cmd == do_bootd) {
#ifdef DEBUG_PARSER
			printf ("[%s]\n", finaltoken);
#endif
			if (flag & CMD_FLAG_BOOTD) {
				puts ("'bootd' recursion detected\n");
				rc = -1;
				continue;
			}
			else
				flag |= CMD_FLAG_BOOTD;
		}
#endif	/* CFG_CMD_BOOTD */

		/* OK - call function to do the command */
		if ((cmdtp->cmd) (cmdtp, flag, argc, argv) != 0) {
			rc = -1;
		}

		repeatable &= cmdtp->repeatable;

		/* Did the user stop this? */
		if (had_ctrlc ())
			return 0;	/* if stopped then not repeatable */
	}

	return rc ? rc : repeatable;
}

/****************************************************************************/

#if (CONFIG_COMMANDS & CFG_CMD_RUN)
int do_run (cmd_tbl_t * cmdtp, int flag, int argc, char *argv[])
{
	int i;

	if (argc < 2) {
		printf ("Usage:\n%s\n", cmdtp->usage);
		return 1;
	}

	for (i=1; i<argc; ++i) {
		char *arg;

		if ((arg = getenv (argv[i])) == NULL) {
			printf ("## Error: \"%s\" not defined\n", argv[i]);
			return 1;
		}
#ifndef CFG_HUSH_PARSER
		if (run_command (arg, flag) == -1)
			return 1;
#else
		if (parse_string_outer(arg,
		    FLAG_PARSE_SEMICOLON | FLAG_EXIT_FROM_LOOP) != 0)
			return 1;
#endif
	}
	return 0;
}
#endif	/* CFG_CMD_RUN */

/* Copyright 2006-2010 iTuner Corporation */
/* Common routines between picolcd 20x2, 20x4, 256x64 */
/* npavel@ituner.com */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#include "driver.h"
#include "picolcd.h"
#include "debug.h"
#include "util.h"

/* private functions */
static usblcd_font_data * get_font_data(char *fontfile, unsigned int max_fonts, unsigned int font_width, unsigned int font_height)
{
    FILE *file;
    usblcd_font_data *chars, *font;
    char *line;
    int index = 0;
    int i = 0;
    unsigned int found = 0;
    
    if ((file = fopen(fontfile, "r")) == NULL)
	return NULL;
	
    line = (char *) malloc (255);
    if (line == NULL)
	return NULL;
    
    chars = (usblcd_font_data *) malloc (max_fonts * sizeof(usblcd_font_data));
    if (chars == NULL)
	return NULL;

    font = chars;
    
    while (index < max_fonts) 
    {
	for (i = 0; i < font_height; i++) 
	{
	    font->line[i] = (char *) malloc (font_width + 1);
	    memset(font->line[i],'0', font_width);
	}
	font->id = 0;
	index++;
	font++;
    }
    
    while(fgets(line, 255, file))
    {
	if (line[0] == '#')
	    continue;
	if (strncmp(line, FONT_HEADER, 5) == 0) {
	    found = 1;
	    break;
	}
    }
    
    if (!found) {
	fprintf(stderr,"Can't find font file header for file: %s.\n", fontfile);
	return NULL;
    }
    
    font = chars;

    while (fgets(line, 255, file)) 
    {
	if ((line[0] == '#') || (line[0]=='\n') || (line[0]==' '))
	    continue;
	font->id = atoi(line);
	for (i = 0; i < font_height ; i++) {
	    if (!fgets(line, 255, file)) 
		break;
	    snprintf(font->line[i], font_width + 1, "%s",line);
	}
	font++;
    }
    
    return chars;
}

/* public functions */


void picolcd_unimplemented(void)
{
    printf("Function not implemented \n");
    return;
}


void picolcd_match(void) 
{
    
}

void picolcd_send(usblcd_operations *self, char *data, int size)
{
    hid_params *params;
    
    if ((params = (hid_params *) malloc (sizeof(hid_params))) == NULL)
	return;
    
    params->endpoint = USB_ENDPOINT_OUT + 1;
    params->timeout = HID_TIMEOUT;
    params->packetlen = size;
    params->packet = data;
    
    self->hid->interrupt_write(self->hid->hiddev->handle, params);
    
    free(params);
    return;
}


void picolcd_init(usblcd_operations *self) 
{
    /* no longer needed */
    #ifdef USB_DEV_TEST
    char const SETUP_PACKET_1[] = { 0x14, 0x03, 0x41, 0x00, 0x43, 0x00, 0x4d, 0x00 };
    char const SETUP_PACKET_2[] = { 0x20, 0x03, 0x41, 0x00, 0x43, 0x00, 0x4d, 0x00 };
    char read_packet[255];
    hid_params *params;
    
    params =(hid_params *) malloc (sizeof(hid_params));
    params->endpoint = USB_ENDPOINT_OUT + 1;
    params->packetlen = 8;
    params->timeout = HID_TIMEOUT;
    params->packet =(char *) SETUP_PACKET_1;
    self->hid->interrupt_write(self->hid->hid_id, params);
    params->packet = (char *)SETUP_PACKET_2;
    self->hid->interrupt_write(self->hid->hid_id, params);
    #endif
    
    self->getversion(self);
}

void picolcd_debug(int level) 
{
    debug_level = level;
}

void picolcd_setled(usblcd_operations *self, unsigned int led, unsigned int status) 
{
    char packet[3];
    
    if (led > self->max_leds) led = self->max_leds;
    if (status > 1 || status < 0) status = 0;
    
    /* set led bit to 1 or 0 */
    if (status)	self->leds |= 1 << led;
    else self->leds &= ~ (1 << led);

    snprintf(packet, 3 , "%c%c", OUT_REPORT_LED_STATE, self->leds);
    picolcd_send(self, packet, 2);
}

void picolcd_backlight(usblcd_operations *self, unsigned int status) 
{
    char packet[3];
    
    snprintf(packet, 2 + 1 , "%c%c", OUT_REPORT_LCD_BACKLIGHT, status);
    picolcd_send(self, packet, 2);
}

void picolcd_contrast(usblcd_operations *self, unsigned int level)
{
    char packet[3];
    snprintf (packet, 2 + 1 , "%c%c", OUT_REPORT_LCD_CONTRAST,  level);
    picolcd_send(self, packet, 2);
}

void picolcd_set_cursor(usblcd_operations *self, unsigned int status)
{
    if (status) self->state->usblcd_cursor = _USBLCD_CURSOR_ON;
    else self->state->usblcd_cursor = 0;
    self->control(self);    
}

void picolcd_set_cursor_blink(usblcd_operations *self, unsigned int status)
{
    if (status) self->state->usblcd_cursor_blink = _USBLCD_CURSOR_BLINK_ON;
    else self->state->usblcd_cursor_blink = 0;
    
    self->control(self);    
}

void picolcd_set_switch(usblcd_operations *self, unsigned int status)
{
    if (status) self->state->usblcd_switch = _USBLCD_SWITCH_ON;
    else self->state->usblcd_switch = 0;
    
    self->control(self);    
}

void picolcd_control(usblcd_operations *self)
{

    char packet[3];
    snprintf(packet, 2 + 1 , "%c%c", OUT_REPORT_LCD_CONTROL, self->state->usblcd_switch | self->state->usblcd_cursor | self->state->usblcd_cursor_blink);
    picolcd_send(self, packet, 2);
}

void picolcd_clear(usblcd_operations *self)
{
    char packet[2];
    snprintf (packet, 1 + 1 , "%c", OUT_REPORT_LCD_CLEAR);
    picolcd_send(self, packet, 2);
}

void picolcd_setchar(usblcd_operations *self, unsigned int row, unsigned int column, char character)
{
    char packet[6];
    
    if (row > self->rows) row = self->rows;
    if (column > self->cols) column = self->cols;
    
    /* message + 4 control bytes row, column,length, character*/
    snprintf (packet, 5 + 1 , "%c%c%c%c%c", OUT_REPORT_LCD_TEXT,  row, column, 1, character);
    picolcd_send(self, packet, 5);
}

void picolcd_settext(usblcd_operations *self, unsigned int row, unsigned int column, char *text)
{
    char packet[64];
    unsigned int len;
    
    len = strlen(text);
    
    if (len > 20) len = 20;
    if (row > self->rows) row = self->rows;
    if (column > self->cols) column = self->cols;
    
    /* 3 control bytes row, column, text length*/
    snprintf(packet, len + 4  + 1 , "%c%c%c%c%s", OUT_REPORT_LCD_TEXT,  row, column, len, text);
    picolcd_send(self, packet, len + 4);
}

void picolcd_getversion(usblcd_operations *self) 
{
    char packet[2];    
    snprintf (packet, 1 + 1 , "%c", HID_REPORT_GET_VERSION);
    picolcd_send(self, packet, 1);
}

void picolcd_enter_flasher_mode(usblcd_operations *self)
{
    char packet[4];
    snprintf(packet, 3 + 1, "%c%c%c", HID_REPORT_EXIT_KEYBOARD, FLASHER_TIMEOUT & 0xFF , (FLASHER_TIMEOUT >> 8) & 0xFF);
    picolcd_send(self, packet, 3);
}

void picolcd_exit_flasher_mode(usblcd_operations *self)
{
    char packet[4];
    
    snprintf(packet, 3 + 1, "%c%c%c", HID_REPORT_EXIT_FLASHER, FLASHER_TIMEOUT & 0xFF , (FLASHER_TIMEOUT >> 8) & 0xFF);
    picolcd_send(self, packet, 3);
}

void picolcd_setfont(usblcd_operations *self, char *filename)
{
    char packet[11];
    usblcd_font_data *buffer, *font;
    buffer = get_font_data(filename, self->max_custom_chars, self->custom_char_width, self->custom_char_height);
    
    if (buffer == NULL) 
	return;
    font = buffer;
    
    while (font->id <= self->max_custom_chars && font->id > 0 ) 
    {
	snprintf(packet, 10 + 1, "%c%c%c%c%c%c%c%c%c%c", OUT_REPORT_LCD_FONT, font->id - 1, 
		bin2int(font->line[0]),
		bin2int(font->line[1]), 
		bin2int(font->line[2]), 
		bin2int(font->line[3]), 
		bin2int(font->line[4]), 
		bin2int(font->line[5]),
		bin2int(font->line[6]), 
		bin2int(font->line[7]));
	
	font++;
	#ifdef DEBUG
	print_buffer(packet, 10);
	#endif
	picolcd_send(self, packet, 10);
    }    	
}

void picolcd_setfont_memory(usblcd_operations *self, int fontlines[], int nrchars)
{
    char packet[11];
    usblcd_font_data *font;
    int i;
    
    for (i = 0; i < nrchars; i++) 
    {
	snprintf(packet, 11 + 1, "%c%c%c%c%c%c%c%c%c%c", OUT_REPORT_LCD_FONT, i, 
		fontlines[i * 8],
		fontlines[i * 8 + 1],
		fontlines[i * 8 + 2],
		fontlines[i * 8 + 3],
		fontlines[i * 8 + 4],
		fontlines[i * 8 + 5],
		fontlines[i * 8 + 6],
		fontlines[i * 8 + 7]);
	font++;
	#ifdef DEBUG
	print_buffer(params->packet, params->packetlen);
	#endif
	picolcd_send(self, packet, 10);
    }   
}


void picolcd_flash(usblcd_operations *self)
{
    return;
}


/* todo: get keystate from circular buffer */
void picolcd_keystate(usblcd_operations *self)
{

}

/* todo: get irdata from circular buffer */
void picolcd_irdata(usblcd_operations *self)
{

}
/* todo: get powerstate from circular buffer */
void picolcd_powerstate(usblcd_operations *self)
{

}

usblcd_event *picolcd_read_events_timeout(usblcd_operations *self, int timeout_ms)
{
    unsigned char packet[256];
    usblcd_event *event;
    int ret, offset, length, type;
    errno = 0;
    if (!self || self->max_data_len > sizeof(packet) || self->max_data_len < 3 || timeout_ms <= 0) {
        errno = EINVAL;
        return NULL;
    }
    ret = usb_interrupt_read(self->hid->hiddev->handle, USB_ENDPOINT_IN + 1,
                             (char *)packet, self->max_data_len, timeout_ms);
    if (ret <= 0) {
        errno = ret < 0 ? -ret : 0;
        return NULL;
    }
    if (packet[0] == IN_REPORT_KEY_STATE) {
        if (ret < 3) return NULL;
        type = 0; offset = 1; length = 2;
    } else if (packet[0] == IN_REPORT_IR_DATA) {
        if (ret < 2 || packet[1] == 0 || packet[1] > ret - 2) return NULL;
        type = 1; offset = 2; length = packet[1];
    } else {
        return NULL;
    }
    event = malloc(sizeof(*event));
    if (!event) return NULL;
    event->data = malloc(length);
    if (!event->data) { free(event); return NULL; }
    event->type = type;
    event->length = length;
    memcpy(event->data, packet + offset, length);
    return event;
}

usblcd_event *picolcd_read_events(usblcd_operations *self)
{
    return picolcd_read_events_timeout(self, 10000);
}

void picolcd_close(usblcd_operations *self)
{
    if (self->hid->hiddev->handle) 
	self->hid->close(self->hid->hiddev->handle);
    if (self->hid)
	free(self->hid);
}

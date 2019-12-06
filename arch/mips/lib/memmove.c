/*
 * Copyright (C) 2016 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/export.h>
#include <linux/string.h>

void *memmove(void *dest, const void *src, size_t count)
{
	const char *s = src;
	const char *s_end = s + count;
	char *d = dest;
	char *d_end = dest + count;

	/* Use optimised memcpy when there's no overlap */
	if ((d_end <= s) || (s_end <= d))
		return memcpy(dest, src, count);

	if (d <= s) {
		/* Incrementing copy loop */
		while (count--)
			*d++ = *s++;
	} else {
		/* Decrementing copy loop */
		d = d_end;
		s = s_end;
		while (count--)
			*--d = *--s;
	}

	return dest;
}
EXPORT_SYMBOL(memmove);

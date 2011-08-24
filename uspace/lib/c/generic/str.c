/*
 * Copyright (c) 2005 Martin Decky
 * Copyright (c) 2008 Jiri Svoboda
 * Copyright (c) 2011 Martin Sucha
 * Copyright (c) 2011 Oleg Romanenko
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup libc
 * @{
 */
/** @file
 */

#include <str.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <ctype.h>
#include <malloc.h>
#include <errno.h>
#include <align.h>
#include <mem.h>
#include <str.h>

/** Byte mask consisting of lowest @n bits (out of 8) */
#define LO_MASK_8(n)  ((uint8_t) ((1 << (n)) - 1))

/** Byte mask consisting of lowest @n bits (out of 32) */
#define LO_MASK_32(n)  ((uint32_t) ((1 << (n)) - 1))

/** Byte mask consisting of highest @n bits (out of 8) */
#define HI_MASK_8(n)  (~LO_MASK_8(8 - (n)))

/** Number of data bits in a UTF-8 continuation byte */
#define CONT_BITS  6

/** Decode a single character from a string.
 *
 * Decode a single character from a string of size @a size. Decoding starts
 * at @a offset and this offset is moved to the beginning of the next
 * character. In case of decoding error, offset generally advances at least
 * by one. However, offset is never moved beyond size.
 *
 * @param str    String (not necessarily NULL-terminated).
 * @param offset Byte offset in string where to start decoding.
 * @param size   Size of the string (in bytes).
 *
 * @return Value of decoded character, U_SPECIAL on decoding error or
 *         NULL if attempt to decode beyond @a size.
 *
 */
wchar_t str_decode(const char *str, size_t *offset, size_t size)
{
	if (*offset + 1 > size)
		return 0;
	
	/* First byte read from string */
	uint8_t b0 = (uint8_t) str[(*offset)++];
	
	/* Determine code length */
	
	unsigned int b0_bits;  /* Data bits in first byte */
	unsigned int cbytes;   /* Number of continuation bytes */
	
	if ((b0 & 0x80) == 0) {
		/* 0xxxxxxx (Plain ASCII) */
		b0_bits = 7;
		cbytes = 0;
	} else if ((b0 & 0xe0) == 0xc0) {
		/* 110xxxxx 10xxxxxx */
		b0_bits = 5;
		cbytes = 1;
	} else if ((b0 & 0xf0) == 0xe0) {
		/* 1110xxxx 10xxxxxx 10xxxxxx */
		b0_bits = 4;
		cbytes = 2;
	} else if ((b0 & 0xf8) == 0xf0) {
		/* 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
		b0_bits = 3;
		cbytes = 3;
	} else {
		/* 10xxxxxx -- unexpected continuation byte */
		return U_SPECIAL;
	}
	
	if (*offset + cbytes > size)
		return U_SPECIAL;
	
	wchar_t ch = b0 & LO_MASK_8(b0_bits);
	
	/* Decode continuation bytes */
	while (cbytes > 0) {
		uint8_t b = (uint8_t) str[(*offset)++];
		
		/* Must be 10xxxxxx */
		if ((b & 0xc0) != 0x80)
			return U_SPECIAL;
		
		/* Shift data bits to ch */
		ch = (ch << CONT_BITS) | (wchar_t) (b & LO_MASK_8(CONT_BITS));
		cbytes--;
	}
	
	return ch;
}

/** Encode a single character to string representation.
 *
 * Encode a single character to string representation (i.e. UTF-8) and store
 * it into a buffer at @a offset. Encoding starts at @a offset and this offset
 * is moved to the position where the next character can be written to.
 *
 * @param ch     Input character.
 * @param str    Output buffer.
 * @param offset Byte offset where to start writing.
 * @param size   Size of the output buffer (in bytes).
 *
 * @return EOK if the character was encoded successfully, EOVERFLOW if there
 *         was not enough space in the output buffer or EINVAL if the character
 *         code was invalid.
 */
int chr_encode(const wchar_t ch, char *str, size_t *offset, size_t size)
{
	if (*offset >= size)
		return EOVERFLOW;
	
	if (!chr_check(ch))
		return EINVAL;
	
	/* Unsigned version of ch (bit operations should only be done
	   on unsigned types). */
	uint32_t cc = (uint32_t) ch;
	
	/* Determine how many continuation bytes are needed */
	
	unsigned int b0_bits;  /* Data bits in first byte */
	unsigned int cbytes;   /* Number of continuation bytes */
	
	if ((cc & ~LO_MASK_32(7)) == 0) {
		b0_bits = 7;
		cbytes = 0;
	} else if ((cc & ~LO_MASK_32(11)) == 0) {
		b0_bits = 5;
		cbytes = 1;
	} else if ((cc & ~LO_MASK_32(16)) == 0) {
		b0_bits = 4;
		cbytes = 2;
	} else if ((cc & ~LO_MASK_32(21)) == 0) {
		b0_bits = 3;
		cbytes = 3;
	} else {
		/* Codes longer than 21 bits are not supported */
		return EINVAL;
	}
	
	/* Check for available space in buffer */
	if (*offset + cbytes >= size)
		return EOVERFLOW;
	
	/* Encode continuation bytes */
	unsigned int i;
	for (i = cbytes; i > 0; i--) {
		str[*offset + i] = 0x80 | (cc & LO_MASK_32(CONT_BITS));
		cc = cc >> CONT_BITS;
	}
	
	/* Encode first byte */
	str[*offset] = (cc & LO_MASK_32(b0_bits)) | HI_MASK_8(8 - b0_bits - 1);
	
	/* Advance offset */
	*offset += cbytes + 1;
	
	return EOK;
}

/** Get size of string.
 *
 * Get the number of bytes which are used by the string @a str (excluding the
 * NULL-terminator).
 *
 * @param str String to consider.
 *
 * @return Number of bytes used by the string
 *
 */
size_t str_size(const char *str)
{
	size_t size = 0;
	
	while (*str++ != 0)
		size++;
	
	return size;
}

/** Get size of wide string.
 *
 * Get the number of bytes which are used by the wide string @a str (excluding the
 * NULL-terminator).
 *
 * @param str Wide string to consider.
 *
 * @return Number of bytes used by the wide string
 *
 */
size_t wstr_size(const wchar_t *str)
{
	return (wstr_length(str) * sizeof(wchar_t));
}

/** Get size of string with length limit.
 *
 * Get the number of bytes which are used by up to @a max_len first
 * characters in the string @a str. If @a max_len is greater than
 * the length of @a str, the entire string is measured (excluding the
 * NULL-terminator).
 *
 * @param str     String to consider.
 * @param max_len Maximum number of characters to measure.
 *
 * @return Number of bytes used by the characters.
 *
 */
size_t str_lsize(const char *str, size_t max_len)
{
	size_t len = 0;
	size_t offset = 0;
	
	while (len < max_len) {
		if (str_decode(str, &offset, STR_NO_LIMIT) == 0)
			break;
		
		len++;
	}
	
	return offset;
}

/** Get size of wide string with length limit.
 *
 * Get the number of bytes which are used by up to @a max_len first
 * wide characters in the wide string @a str. If @a max_len is greater than
 * the length of @a str, the entire wide string is measured (excluding the
 * NULL-terminator).
 *
 * @param str     Wide string to consider.
 * @param max_len Maximum number of wide characters to measure.
 *
 * @return Number of bytes used by the wide characters.
 *
 */
size_t wstr_lsize(const wchar_t *str, size_t max_len)
{
	return (wstr_nlength(str, max_len * sizeof(wchar_t)) * sizeof(wchar_t));
}

/** Get number of characters in a string.
 *
 * @param str NULL-terminated string.
 *
 * @return Number of characters in string.
 *
 */
size_t str_length(const char *str)
{
	size_t len = 0;
	size_t offset = 0;
	
	while (str_decode(str, &offset, STR_NO_LIMIT) != 0)
		len++;
	
	return len;
}

/** Get number of characters in a wide string.
 *
 * @param str NULL-terminated wide string.
 *
 * @return Number of characters in @a str.
 *
 */
size_t wstr_length(const wchar_t *wstr)
{
	size_t len = 0;
	
	while (*wstr++ != 0)
		len++;
	
	return len;
}

/** Get number of characters in a string with size limit.
 *
 * @param str  NULL-terminated string.
 * @param size Maximum number of bytes to consider.
 *
 * @return Number of characters in string.
 *
 */
size_t str_nlength(const char *str, size_t size)
{
	size_t len = 0;
	size_t offset = 0;
	
	while (str_decode(str, &offset, size) != 0)
		len++;
	
	return len;
}

/** Get number of characters in a string with size limit.
 *
 * @param str  NULL-terminated string.
 * @param size Maximum number of bytes to consider.
 *
 * @return Number of characters in string.
 *
 */
size_t wstr_nlength(const wchar_t *str, size_t size)
{
	size_t len = 0;
	size_t limit = ALIGN_DOWN(size, sizeof(wchar_t));
	size_t offset = 0;
	
	while ((offset < limit) && (*str++ != 0)) {
		len++;
		offset += sizeof(wchar_t);
	}
	
	return len;
}

/** Check whether character is plain ASCII.
 *
 * @return True if character is plain ASCII.
 *
 */
bool ascii_check(wchar_t ch)
{
	if ((ch >= 0) && (ch <= 127))
		return true;
	
	return false;
}

/** Check whether character is valid
 *
 * @return True if character is a valid Unicode code point.
 *
 */
bool chr_check(wchar_t ch)
{
	if ((ch >= 0) && (ch <= 1114111))
		return true;
	
	return false;
}

/** Compare two NULL terminated strings.
 *
 * Do a char-by-char comparison of two NULL-terminated strings.
 * The strings are considered equal iff they consist of the same
 * characters on the minimum of their lengths.
 *
 * @param s1 First string to compare.
 * @param s2 Second string to compare.
 *
 * @return 0 if the strings are equal, -1 if first is smaller,
 *         1 if second smaller.
 *
 */
int str_cmp(const char *s1, const char *s2)
{
	wchar_t c1 = 0;
	wchar_t c2 = 0;
	
	size_t off1 = 0;
	size_t off2 = 0;

	while (true) {
		c1 = str_decode(s1, &off1, STR_NO_LIMIT);
		c2 = str_decode(s2, &off2, STR_NO_LIMIT);

		if (c1 < c2)
			return -1;
		
		if (c1 > c2)
			return 1;

		if (c1 == 0 || c2 == 0)
			break;		
	}

	return 0;
}

/** Compare two NULL terminated strings with length limit.
 *
 * Do a char-by-char comparison of two NULL-terminated strings.
 * The strings are considered equal iff they consist of the same
 * characters on the minimum of their lengths and the length limit.
 *
 * @param s1      First string to compare.
 * @param s2      Second string to compare.
 * @param max_len Maximum number of characters to consider.
 *
 * @return 0 if the strings are equal, -1 if first is smaller,
 *         1 if second smaller.
 *
 */
int str_lcmp(const char *s1, const char *s2, size_t max_len)
{
	wchar_t c1 = 0;
	wchar_t c2 = 0;
	
	size_t off1 = 0;
	size_t off2 = 0;
	
	size_t len = 0;

	while (true) {
		if (len >= max_len)
			break;

		c1 = str_decode(s1, &off1, STR_NO_LIMIT);
		c2 = str_decode(s2, &off2, STR_NO_LIMIT);

		if (c1 < c2)
			return -1;

		if (c1 > c2)
			return 1;

		if (c1 == 0 || c2 == 0)
			break;

		++len;	
	}

	return 0;

}

/** Copy string.
 *
 * Copy source string @a src to destination buffer @a dest.
 * No more than @a size bytes are written. If the size of the output buffer
 * is at least one byte, the output string will always be well-formed, i.e.
 * null-terminated and containing only complete characters.
 *
 * @param dest  Destination buffer.
 * @param count Size of the destination buffer (must be > 0).
 * @param src   Source string.
 */
void str_cpy(char *dest, size_t size, const char *src)
{
	/* There must be space for a null terminator in the buffer. */
	assert(size > 0);
	
	size_t src_off = 0;
	size_t dest_off = 0;
	
	wchar_t ch;
	while ((ch = str_decode(src, &src_off, STR_NO_LIMIT)) != 0) {
		if (chr_encode(ch, dest, &dest_off, size - 1) != EOK)
			break;
	}
	
	dest[dest_off] = '\0';
}

/** Copy size-limited substring.
 *
 * Copy prefix of string @a src of max. size @a size to destination buffer
 * @a dest. No more than @a size bytes are written. The output string will
 * always be well-formed, i.e. null-terminated and containing only complete
 * characters.
 *
 * No more than @a n bytes are read from the input string, so it does not
 * have to be null-terminated.
 *
 * @param dest  Destination buffer.
 * @param count Size of the destination buffer (must be > 0).
 * @param src   Source string.
 * @param n     Maximum number of bytes to read from @a src.
 */
void str_ncpy(char *dest, size_t size, const char *src, size_t n)
{
	/* There must be space for a null terminator in the buffer. */
	assert(size > 0);
	
	size_t src_off = 0;
	size_t dest_off = 0;
	
	wchar_t ch;
	while ((ch = str_decode(src, &src_off, n)) != 0) {
		if (chr_encode(ch, dest, &dest_off, size - 1) != EOK)
			break;
	}
	
	dest[dest_off] = '\0';
}

/** Append one string to another.
 *
 * Append source string @a src to string in destination buffer @a dest.
 * Size of the destination buffer is @a dest. If the size of the output buffer
 * is at least one byte, the output string will always be well-formed, i.e.
 * null-terminated and containing only complete characters.
 *
 * @param dest   Destination buffer.
 * @param count Size of the destination buffer.
 * @param src   Source string.
 */
void str_append(char *dest, size_t size, const char *src)
{
	size_t dstr_size;

	dstr_size = str_size(dest);
	if (dstr_size >= size)
		return;
	
	str_cpy(dest + dstr_size, size - dstr_size, src);
}

/** Convert space-padded ASCII to string.
 *
 * Common legacy text encoding in hardware is 7-bit ASCII fitted into
 * a fixed-with byte buffer (bit 7 always zero), right-padded with spaces
 * (ASCII 0x20). Convert space-padded ascii to string representation.
 *
 * If the text does not fit into the destination buffer, the function converts
 * as many characters as possible and returns EOVERFLOW.
 *
 * If the text contains non-ASCII bytes (with bit 7 set), the whole string is
 * converted anyway and invalid characters are replaced with question marks
 * (U_SPECIAL) and the function returns EIO.
 *
 * Regardless of return value upon return @a dest will always be well-formed.
 *
 * @param dest		Destination buffer
 * @param size		Size of destination buffer
 * @param src		Space-padded ASCII.
 * @param n		Size of the source buffer in bytes.
 *
 * @return		EOK on success, EOVERFLOW if the text does not fit
 *			destination buffer, EIO if the text contains
 *			non-ASCII bytes.
 */
int spascii_to_str(char *dest, size_t size, const uint8_t *src, size_t n)
{
	size_t sidx;
	size_t didx;
	size_t dlast;
	uint8_t byte;
	int rc;
	int result;

	/* There must be space for a null terminator in the buffer. */
	assert(size > 0);
	result = EOK;

	didx = 0;
	dlast = 0;
	for (sidx = 0; sidx < n; ++sidx) {
		byte = src[sidx];
		if (!ascii_check(byte)) {
			byte = U_SPECIAL;
			result = EIO;
		}

		rc = chr_encode(byte, dest, &didx, size - 1);
		if (rc != EOK) {
			assert(rc == EOVERFLOW);
			dest[didx] = '\0';
			return rc;
		}

		/* Remember dest index after last non-empty character */
		if (byte != 0x20)
			dlast = didx;
	}

	/* Terminate string after last non-empty character */
	dest[dlast] = '\0';
	return result;
}

/** Convert wide string to string.
 *
 * Convert wide string @a src to string. The output is written to the buffer
 * specified by @a dest and @a size. @a size must be non-zero and the string
 * written will always be well-formed.
 *
 * @param dest	Destination buffer.
 * @param size	Size of the destination buffer.
 * @param src	Source wide string.
 */
void wstr_to_str(char *dest, size_t size, const wchar_t *src)
{
	wchar_t ch;
	size_t src_idx;
	size_t dest_off;

	/* There must be space for a null terminator in the buffer. */
	assert(size > 0);
	
	src_idx = 0;
	dest_off = 0;

	while ((ch = src[src_idx++]) != 0) {
		if (chr_encode(ch, dest, &dest_off, size - 1) != EOK)
			break;
	}

	dest[dest_off] = '\0';
}

/** Convert UTF16 string to string.
 *
 * Convert utf16 string @a src to string. The output is written to the buffer
 * specified by @a dest and @a size. @a size must be non-zero and the string
 * written will always be well-formed. Surrogate pairs also supported.
 *
 * @param dest	Destination buffer.
 * @param size	Size of the destination buffer.
 * @param src	Source utf16 string.
 *
 * @return EOK, if success, negative otherwise.
 */
int utf16_to_str(char *dest, size_t size, const uint16_t *src)
{
	size_t idx=0, dest_off=0;
	wchar_t ch;
	int rc = EOK;

	/* There must be space for a null terminator in the buffer. */
	assert(size > 0);

	while (src[idx]) {
		if ((src[idx] & 0xfc00) == 0xd800) {
			if (src[idx+1] && (src[idx+1] & 0xfc00) == 0xdc00) {
				ch = 0x10000;
				ch += (src[idx] & 0x03FF) << 10;
				ch += (src[idx+1] & 0x03FF);
				idx += 2;
			}
			else
				break;
		} else {
			ch = src[idx];
			idx++;
		}
		rc = chr_encode(ch, dest, &dest_off, size-1);
		if (rc != EOK)
			break;
	}
	dest[dest_off] = '\0';
	return rc;
}

int str_to_utf16(uint16_t *dest, size_t size, const char *src)
{
	int rc=EOK;
	size_t offset=0;
	size_t idx=0;
	wchar_t c;

	assert(size > 0);
	
	while ((c = str_decode(src, &offset, STR_NO_LIMIT)) != 0) {
		if (c > 0x10000) {
			if (idx+2 >= size-1) {
				rc=EOVERFLOW;
				break;
			}
			c = (c - 0x10000);
			dest[idx] = 0xD800 | (c >> 10);
			dest[idx+1] = 0xDC00 | (c & 0x3FF);
			idx++;
		} else {
			 dest[idx] = c;
		}

		idx++;
		if (idx >= size-1) {
			rc=EOVERFLOW;
			break;
		}
	}

	dest[idx] = '\0';
	return rc;
}


/** Convert wide string to new string.
 *
 * Convert wide string @a src to string. Space for the new string is allocated
 * on the heap.
 *
 * @param src	Source wide string.
 * @return	New string.
 */
char *wstr_to_astr(const wchar_t *src)
{
	char dbuf[STR_BOUNDS(1)];
	char *str;
	wchar_t ch;

	size_t src_idx;
	size_t dest_off;
	size_t dest_size;

	/* Compute size of encoded string. */

	src_idx = 0;
	dest_size = 0;

	while ((ch = src[src_idx++]) != 0) {
		dest_off = 0;
		if (chr_encode(ch, dbuf, &dest_off, STR_BOUNDS(1)) != EOK)
			break;
		dest_size += dest_off;
	}

	str = malloc(dest_size + 1);
	if (str == NULL)
		return NULL;

	/* Encode string. */

	src_idx = 0;
	dest_off = 0;

	while ((ch = src[src_idx++]) != 0) {
		if (chr_encode(ch, str, &dest_off, dest_size) != EOK)
			break;
	}

	str[dest_size] = '\0';
	return str;
}


/** Convert string to wide string.
 *
 * Convert string @a src to wide string. The output is written to the
 * buffer specified by @a dest and @a dlen. @a dlen must be non-zero
 * and the wide string written will always be null-terminated.
 *
 * @param dest	Destination buffer.
 * @param dlen	Length of destination buffer (number of wchars).
 * @param src	Source string.
 */
void str_to_wstr(wchar_t *dest, size_t dlen, const char *src)
{
	size_t offset;
	size_t di;
	wchar_t c;

	assert(dlen > 0);

	offset = 0;
	di = 0;

	do {
		if (di >= dlen - 1)
			break;

		c = str_decode(src, &offset, STR_NO_LIMIT);
		dest[di++] = c;
	} while (c != '\0');

	dest[dlen - 1] = '\0';
}

/** Convert string to wide string.
 *
 * Convert string @a src to wide string. A new wide NULL-terminated
 * string will be allocated on the heap.
 *
 * @param src	Source string.
 */
wchar_t *str_to_awstr(const char *str)
{
	size_t len = str_length(str);
	
	wchar_t *wstr = calloc(len+1, sizeof(wchar_t));
	if (wstr == NULL)
		return NULL;
	
	str_to_wstr(wstr, len + 1, str);
	return wstr;
}

/** Find first occurence of character in string.
 *
 * @param str String to search.
 * @param ch  Character to look for.
 *
 * @return Pointer to character in @a str or NULL if not found.
 */
char *str_chr(const char *str, wchar_t ch)
{
	wchar_t acc;
	size_t off = 0;
	size_t last = 0;
	
	while ((acc = str_decode(str, &off, STR_NO_LIMIT)) != 0) {
		if (acc == ch)
			return (char *) (str + last);
		last = off;
	}
	
	return NULL;
}

/** Find last occurence of character in string.
 *
 * @param str String to search.
 * @param ch  Character to look for.
 *
 * @return Pointer to character in @a str or NULL if not found.
 */
char *str_rchr(const char *str, wchar_t ch)
{
	wchar_t acc;
	size_t off = 0;
	size_t last = 0;
	const char *res = NULL;
	
	while ((acc = str_decode(str, &off, STR_NO_LIMIT)) != 0) {
		if (acc == ch)
			res = (str + last);
		last = off;
	}
	
	return (char *) res;
}

/** Insert a wide character into a wide string.
 *
 * Insert a wide character into a wide string at position
 * @a pos. The characters after the position are shifted.
 *
 * @param str     String to insert to.
 * @param ch      Character to insert to.
 * @param pos     Character index where to insert.
 @ @param max_pos Characters in the buffer.
 *
 * @return True if the insertion was sucessful, false if the position
 *         is out of bounds.
 *
 */
bool wstr_linsert(wchar_t *str, wchar_t ch, size_t pos, size_t max_pos)
{
	size_t len = wstr_length(str);
	
	if ((pos > len) || (pos + 1 > max_pos))
		return false;
	
	size_t i;
	for (i = len; i + 1 > pos; i--)
		str[i + 1] = str[i];
	
	str[pos] = ch;
	
	return true;
}

/** Remove a wide character from a wide string.
 *
 * Remove a wide character from a wide string at position
 * @a pos. The characters after the position are shifted.
 *
 * @param str String to remove from.
 * @param pos Character index to remove.
 *
 * @return True if the removal was sucessful, false if the position
 *         is out of bounds.
 *
 */
bool wstr_remove(wchar_t *str, size_t pos)
{
	size_t len = wstr_length(str);
	
	if (pos >= len)
		return false;
	
	size_t i;
	for (i = pos + 1; i <= len; i++)
		str[i - 1] = str[i];
	
	return true;
}

int stricmp(const char *a, const char *b)
{
	int c = 0;
	
	while (a[c] && b[c] && (!(tolower(a[c]) - tolower(b[c]))))
		c++;
	
	return (tolower(a[c]) - tolower(b[c]));
}

/** Convert string to a number. 
 * Core of strtol and strtoul functions.
 *
 * @param nptr		Pointer to string.
 * @param endptr	If not NULL, function stores here pointer to the first
 * 			invalid character.
 * @param base		Zero or number between 2 and 36 inclusive.
 * @param sgn		It's set to 1 if minus found.
 * @return		Result of conversion.
 */
static unsigned long
_strtoul(const char *nptr, char **endptr, int base, char *sgn)
{
	unsigned char c;
	unsigned long result = 0;
	unsigned long a, b;
	const char *str = nptr;
	const char *tmpptr;
	
	while (isspace(*str))
		str++;
	
	if (*str == '-') {
		*sgn = 1;
		++str;
	} else if (*str == '+')
		++str;
	
	if (base) {
		if ((base == 1) || (base > 36)) {
			/* FIXME: set errno to EINVAL */
			return 0;
		}
		if ((base == 16) && (*str == '0') && ((str[1] == 'x') ||
		    (str[1] == 'X'))) {
			str += 2;
		}
	} else {
		base = 10;
		
		if (*str == '0') {
			base = 8;
			if ((str[1] == 'X') || (str[1] == 'x'))  {
				base = 16;
				str += 2;
			}
		} 
	}
	
	tmpptr = str;

	while (*str) {
		c = *str;
		c = (c >= 'a' ? c - 'a' + 10 : (c >= 'A' ? c - 'A' + 10 :
		    (c <= '9' ? c - '0' : 0xff)));
		if (c > base) {
			break;
		}
		
		a = (result & 0xff) * base + c;
		b = (result >> 8) * base + (a >> 8);
		
		if (b > (ULONG_MAX >> 8)) {
			/* overflow */
			/* FIXME: errno = ERANGE*/
			return ULONG_MAX;
		}
	
		result = (b << 8) + (a & 0xff);
		++str;
	}
	
	if (str == tmpptr) {
		/*
		 * No number was found => first invalid character is the first
		 * character of the string.
		 */
		/* FIXME: set errno to EINVAL */
		str = nptr;
		result = 0;
	}
	
	if (endptr)
		*endptr = (char *) str;

	if (nptr == str) { 
		/*FIXME: errno = EINVAL*/
		return 0;
	}

	return result;
}

/** Convert initial part of string to long int according to given base.
 * The number may begin with an arbitrary number of whitespaces followed by
 * optional sign (`+' or `-'). If the base is 0 or 16, the prefix `0x' may be
 * inserted and the number will be taken as hexadecimal one. If the base is 0
 * and the number begin with a zero, number will be taken as octal one (as with
 * base 8). Otherwise the base 0 is taken as decimal.
 *
 * @param nptr		Pointer to string.
 * @param endptr	If not NULL, function stores here pointer to the first
 * 			invalid character.
 * @param base		Zero or number between 2 and 36 inclusive.
 * @return		Result of conversion.
 */
long int strtol(const char *nptr, char **endptr, int base)
{
	char sgn = 0;
	unsigned long number = 0;
	
	number = _strtoul(nptr, endptr, base, &sgn);

	if (number > LONG_MAX) {
		if ((sgn) && (number == (unsigned long) (LONG_MAX) + 1)) {
			/* FIXME: set 0 to errno */
			return number;		
		}
		/* FIXME: set ERANGE to errno */
		return (sgn ? LONG_MIN : LONG_MAX);	
	}
	
	return (sgn ? -number : number);
}

/** Duplicate string.
 *
 * Allocate a new string and copy characters from the source
 * string into it. The duplicate string is allocated via sleeping
 * malloc(), thus this function can sleep in no memory conditions.
 *
 * The allocation cannot fail and the return value is always
 * a valid pointer. The duplicate string is always a well-formed
 * null-terminated UTF-8 string, but it can differ from the source
 * string on the byte level.
 *
 * @param src Source string.
 *
 * @return Duplicate string.
 *
 */
char *str_dup(const char *src)
{
	size_t size = str_size(src) + 1;
	char *dest = (char *) malloc(size);
	if (dest == NULL)
		return (char *) NULL;
	
	str_cpy(dest, size, src);
	return dest;
}

/** Duplicate string with size limit.
 *
 * Allocate a new string and copy up to @max_size bytes from the source
 * string into it. The duplicate string is allocated via sleeping
 * malloc(), thus this function can sleep in no memory conditions.
 * No more than @max_size + 1 bytes is allocated, but if the size
 * occupied by the source string is smaller than @max_size + 1,
 * less is allocated.
 *
 * The allocation cannot fail and the return value is always
 * a valid pointer. The duplicate string is always a well-formed
 * null-terminated UTF-8 string, but it can differ from the source
 * string on the byte level.
 *
 * @param src Source string.
 * @param n   Maximum number of bytes to duplicate.
 *
 * @return Duplicate string.
 *
 */
char *str_ndup(const char *src, size_t n)
{
	size_t size = str_size(src);
	if (size > n)
		size = n;
	
	char *dest = (char *) malloc(size + 1);
	if (dest == NULL)
		return (char *) NULL;
	
	str_ncpy(dest, size + 1, src, size);
	return dest;
}

/** Convert initial part of string to unsigned long according to given base.
 * The number may begin with an arbitrary number of whitespaces followed by
 * optional sign (`+' or `-'). If the base is 0 or 16, the prefix `0x' may be
 * inserted and the number will be taken as hexadecimal one. If the base is 0
 * and the number begin with a zero, number will be taken as octal one (as with
 * base 8). Otherwise the base 0 is taken as decimal.
 *
 * @param nptr		Pointer to string.
 * @param endptr	If not NULL, function stores here pointer to the first
 * 			invalid character
 * @param base		Zero or number between 2 and 36 inclusive.
 * @return		Result of conversion.
 */
unsigned long strtoul(const char *nptr, char **endptr, int base)
{
	char sgn = 0;
	unsigned long number = 0;
	
	number = _strtoul(nptr, endptr, base, &sgn);

	return (sgn ? -number : number);
}

char *strtok(char *s, const char *delim)
{
	static char *next;

	return strtok_r(s, delim, &next);
}

char *strtok_r(char *s, const char *delim, char **next)
{
	char *start, *end;

	if (s == NULL)
		s = *next;

	/* Skip over leading delimiters. */
	while (*s && (str_chr(delim, *s) != NULL)) ++s;
	start = s;

	/* Skip over token characters. */
	while (*s && (str_chr(delim, *s) == NULL)) ++s;
	end = s;
	*next = (*s ? s + 1 : s);

	if (start == end) {
		return NULL;	/* No more tokens. */
	}

	/* Overwrite delimiter with NULL terminator. */
	*end = '\0';
	return start;
}

/** Convert string to uint64_t (internal variant).
 *
 * @param nptr   Pointer to string.
 * @param endptr Pointer to the first invalid character is stored here.
 * @param base   Zero or number between 2 and 36 inclusive.
 * @param neg    Indication of unary minus is stored here.
 * @apram result Result of the conversion.
 *
 * @return EOK if conversion was successful.
 *
 */
static int str_uint(const char *nptr, char **endptr, unsigned int base,
    bool *neg, uint64_t *result)
{
	assert(endptr != NULL);
	assert(neg != NULL);
	assert(result != NULL);
	
	*neg = false;
	const char *str = nptr;
	
	/* Ignore leading whitespace */
	while (isspace(*str))
		str++;
	
	if (*str == '-') {
		*neg = true;
		str++;
	} else if (*str == '+')
		str++;
	
	if (base == 0) {
		/* Decode base if not specified */
		base = 10;
		
		if (*str == '0') {
			base = 8;
			str++;
			
			switch (*str) {
			case 'b':
			case 'B':
				base = 2;
				str++;
				break;
			case 'o':
			case 'O':
				base = 8;
				str++;
				break;
			case 'd':
			case 'D':
			case 't':
			case 'T':
				base = 10;
				str++;
				break;
			case 'x':
			case 'X':
				base = 16;
				str++;
				break;
			default:
				str--;
			}
		}
	} else {
		/* Check base range */
		if ((base < 2) || (base > 36)) {
			*endptr = (char *) str;
			return EINVAL;
		}
	}
	
	*result = 0;
	const char *startstr = str;
	
	while (*str != 0) {
		unsigned int digit;
		
		if ((*str >= 'a') && (*str <= 'z'))
			digit = *str - 'a' + 10;
		else if ((*str >= 'A') && (*str <= 'Z'))
			digit = *str - 'A' + 10;
		else if ((*str >= '0') && (*str <= '9'))
			digit = *str - '0';
		else
			break;
		
		if (digit >= base)
			break;
		
		uint64_t prev = *result;
		*result = (*result) * base + digit;
		
		if (*result < prev) {
			/* Overflow */
			*endptr = (char *) str;
			return EOVERFLOW;
		}
		
		str++;
	}
	
	if (str == startstr) {
		/*
		 * No digits were decoded => first invalid character is
		 * the first character of the string.
		 */
		str = nptr;
	}
	
	*endptr = (char *) str;
	
	if (str == nptr)
		return EINVAL;
	
	return EOK;
}

/** Convert string to uint64_t.
 *
 * @param nptr   Pointer to string.
 * @param endptr If not NULL, pointer to the first invalid character
 *               is stored here.
 * @param base   Zero or number between 2 and 36 inclusive.
 * @param strict Do not allow any trailing characters.
 * @param result Result of the conversion.
 *
 * @return EOK if conversion was successful.
 *
 */
int str_uint64(const char *nptr, char **endptr, unsigned int base,
    bool strict, uint64_t *result)
{
	assert(result != NULL);
	
	bool neg;
	char *lendptr;
	int ret = str_uint(nptr, &lendptr, base, &neg, result);
	
	if (endptr != NULL)
		*endptr = (char *) lendptr;
	
	if (ret != EOK)
		return ret;
	
	/* Do not allow negative values */
	if (neg)
		return EINVAL;
	
	/* Check whether we are at the end of
	   the string in strict mode */
	if ((strict) && (*lendptr != 0))
		return EINVAL;
	
	return EOK;
}

/** Convert string to size_t.
 *
 * @param nptr   Pointer to string.
 * @param endptr If not NULL, pointer to the first invalid character
 *               is stored here.
 * @param base   Zero or number between 2 and 36 inclusive.
 * @param strict Do not allow any trailing characters.
 * @param result Result of the conversion.
 *
 * @return EOK if conversion was successful.
 *
 */
int str_size_t(const char *nptr, char **endptr, unsigned int base,
    bool strict, size_t *result)
{
	assert(result != NULL);
	
	bool neg;
	char *lendptr;
	uint64_t res;
	int ret = str_uint(nptr, &lendptr, base, &neg, &res);
	
	if (endptr != NULL)
		*endptr = (char *) lendptr;
	
	if (ret != EOK)
		return ret;
	
	/* Do not allow negative values */
	if (neg)
		return EINVAL;
	
	/* Check whether we are at the end of
	   the string in strict mode */
	if ((strict) && (*lendptr != 0))
		return EINVAL;
	
	/* Check for overflow */
	size_t _res = (size_t) res;
	if (_res != res)
		return EOVERFLOW;
	
	*result = _res;
	
	return EOK;
}

void order_suffix(const uint64_t val, uint64_t *rv, char *suffix)
{
	if (val > UINT64_C(10000000000000000000)) {
		*rv = val / UINT64_C(1000000000000000000);
		*suffix = 'Z';
	} else if (val > UINT64_C(1000000000000000000)) {
		*rv = val / UINT64_C(1000000000000000);
		*suffix = 'E';
	} else if (val > UINT64_C(1000000000000000)) {
		*rv = val / UINT64_C(1000000000000);
		*suffix = 'T';
	} else if (val > UINT64_C(1000000000000)) {
		*rv = val / UINT64_C(1000000000);
		*suffix = 'G';
	} else if (val > UINT64_C(1000000000)) {
		*rv = val / UINT64_C(1000000);
		*suffix = 'M';
	} else if (val > UINT64_C(1000000)) {
		*rv = val / UINT64_C(1000);
		*suffix = 'k';
	} else {
		*rv = val;
		*suffix = ' ';
	}
}

void bin_order_suffix(const uint64_t val, uint64_t *rv, const char **suffix,
    bool fixed)
{
	if (val > UINT64_C(1152921504606846976)) {
		*rv = val / UINT64_C(1125899906842624);
		*suffix = "EiB";
	} else if (val > UINT64_C(1125899906842624)) {
		*rv = val / UINT64_C(1099511627776);
		*suffix = "TiB";
	} else if (val > UINT64_C(1099511627776)) {
		*rv = val / UINT64_C(1073741824);
		*suffix = "GiB";
	} else if (val > UINT64_C(1073741824)) {
		*rv = val / UINT64_C(1048576);
		*suffix = "MiB";
	} else if (val > UINT64_C(1048576)) {
		*rv = val / UINT64_C(1024);
		*suffix = "KiB";
	} else {
		*rv = val;
		if (fixed)
			*suffix = "B  ";
		else
			*suffix = "B";
	}
}

/** @}
 */

/*
 * Minimal iconv() implementation for PS Vita.
 *
 * vitasdk's newlib ships the <iconv.h> header (declaring the standard iconv_t /
 * iconv_open / iconv / iconv_close API) but no linkable implementation - and GNU
 * libiconv's autotools/gnulib build turned out to be a deep cross-compilation rabbit
 * hole on this newlib target (broken cross-compile detection for mbrtowc and a
 * generated signal.h replacement, on top of GCC 15's C23 default dialect breaking
 * old-style K&R declarations - see vita/README.md). Rather than keep patching around
 * a general-purpose library we only need a narrow slice of, this implements exactly
 * what VCMI actually converts between (lib/texts/TextOperations.cpp,
 * lib/texts/Languages.h): UTF-8 and the single-byte Windows codepages used by VCMI's
 * bundled translations (CP1250-1254, CP1257).
 *
 * NOT supported: CP932 (Japanese) / CP949 (Korean), which are multi-byte legacy
 * encodings - genuinely out of scope for a from-scratch implementation here. Loading
 * a Japanese/Korean language pack will fail the iconv_open() call and log an error
 * (TextOperations.cpp's existing error handling), not crash. See vita/README.md.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <iconv.h>

#include "iconv_shim_tables.h"

enum ShimEncoding
{
	ENC_UTF8,
	ENC_CP1250,
	ENC_CP1251,
	ENC_CP1252,
	ENC_CP1253,
	ENC_CP1254,
	ENC_CP1257,
	ENC_UNKNOWN
};

struct ShimConversion
{
	enum ShimEncoding from;
	enum ShimEncoding to;
};

static enum ShimEncoding resolveEncoding(const char * name)
{
	if(strcmp(name, "UTF-8") == 0)
		return ENC_UTF8;
	if(strcmp(name, "CP1250") == 0)
		return ENC_CP1250;
	if(strcmp(name, "CP1251") == 0)
		return ENC_CP1251;
	if(strcmp(name, "CP1252") == 0)
		return ENC_CP1252;
	if(strcmp(name, "CP1253") == 0)
		return ENC_CP1253;
	if(strcmp(name, "CP1254") == 0)
		return ENC_CP1254;
	if(strcmp(name, "CP1257") == 0)
		return ENC_CP1257;
	return ENC_UNKNOWN;
}

static const unsigned short * tableFor(enum ShimEncoding enc)
{
	switch(enc)
	{
		case ENC_CP1250: return cp1250_to_unicode;
		case ENC_CP1251: return cp1251_to_unicode;
		case ENC_CP1252: return cp1252_to_unicode;
		case ENC_CP1253: return cp1253_to_unicode;
		case ENC_CP1254: return cp1254_to_unicode;
		case ENC_CP1257: return cp1257_to_unicode;
		default: return NULL;
	}
}

/* Decodes one codepoint from a single-byte Windows codepage. Returns bytes consumed
 * (always 1), or 0 on an undefined byte (EILSEQ). */
static size_t decodeCodepage(const unsigned short * table, const char * in, size_t inLeft, unsigned int * outCp)
{
	(void)inLeft; /* always exactly 1 byte for these codepages */
	unsigned char byte = (unsigned char)in[0];
	if(byte < 0x80)
	{
		*outCp = byte;
		return 1;
	}
	unsigned short mapped = table[byte - 0x80];
	if(mapped == 0) /* no codepage maps anything to U+0000, so 0 unambiguously means "undefined byte" */
		return 0;
	*outCp = mapped;
	return 1;
}

/* Encodes one codepoint into a single-byte Windows codepage. Returns bytes written
 * (always 1), or 0 if the codepoint has no representation in this codepage (EILSEQ). */
static size_t encodeCodepage(const unsigned short * table, unsigned int cp, char * out)
{
	if(cp < 0x80)
	{
		out[0] = (char)cp;
		return 1;
	}
	for(int i = 0; i < 128; ++i)
	{
		if(table[i] == cp)
		{
			out[0] = (char)(0x80 + i);
			return 1;
		}
	}
	return 0;
}

/* Decodes one codepoint from UTF-8. Returns bytes consumed, or 0 on invalid/truncated
 * input (EILSEQ, or EINVAL for a sequence that is valid but cut off at the buffer end). */
static size_t decodeUtf8(const char * in, size_t inLeft, unsigned int * outCp, int * incomplete)
{
	unsigned char b0 = (unsigned char)in[0];
	size_t len;
	unsigned int cp;

	if(b0 < 0x80) { *outCp = b0; return 1; }
	else if((b0 & 0xE0) == 0xC0) { len = 2; cp = b0 & 0x1F; }
	else if((b0 & 0xF0) == 0xE0) { len = 3; cp = b0 & 0x0F; }
	else if((b0 & 0xF8) == 0xF0) { len = 4; cp = b0 & 0x07; }
	else return 0; /* invalid lead byte */

	if(inLeft < len)
	{
		*incomplete = 1;
		return 0;
	}
	for(size_t i = 1; i < len; ++i)
	{
		unsigned char cont = (unsigned char)in[i];
		if((cont & 0xC0) != 0x80)
			return 0; /* invalid continuation byte */
		cp = (cp << 6) | (cont & 0x3F);
	}
	*outCp = cp;
	return len;
}

static size_t encodeUtf8(unsigned int cp, char * out)
{
	if(cp < 0x80)
	{
		out[0] = (char)cp;
		return 1;
	}
	else if(cp < 0x800)
	{
		out[0] = (char)(0xC0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	}
	else if(cp < 0x10000)
	{
		out[0] = (char)(0xE0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	}
	else
	{
		out[0] = (char)(0xF0 | (cp >> 18));
		out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
		out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[3] = (char)(0x80 | (cp & 0x3F));
		return 4;
	}
}

iconv_t iconv_open(const char * tocode, const char * fromcode)
{
	enum ShimEncoding from = resolveEncoding(fromcode);
	enum ShimEncoding to = resolveEncoding(tocode);
	if(from == ENC_UNKNOWN || to == ENC_UNKNOWN)
	{
		errno = EINVAL;
		return (iconv_t)-1;
	}

	struct ShimConversion * conv = malloc(sizeof(struct ShimConversion));
	if(!conv)
	{
		errno = ENOMEM;
		return (iconv_t)-1;
	}
	conv->from = from;
	conv->to = to;
	return (iconv_t)conv;
}

size_t iconv(iconv_t cd, char ** inbuf, size_t * inbytesleft, char ** outbuf, size_t * outbytesleft)
{
	struct ShimConversion * conv = (struct ShimConversion *)cd;

	/* iconv(cd, NULL, ...) resets shift state; this shim is stateless, so no-op. */
	if(!inbuf || !*inbuf)
		return 0;

	while(*inbytesleft > 0)
	{
		unsigned int cp;
		size_t consumed;
		int incomplete = 0;

		if(conv->from == ENC_UTF8)
			consumed = decodeUtf8(*inbuf, *inbytesleft, &cp, &incomplete);
		else
			consumed = decodeCodepage(tableFor(conv->from), *inbuf, *inbytesleft, &cp);

		if(consumed == 0)
		{
			errno = incomplete ? EINVAL : EILSEQ;
			return (size_t)-1;
		}

		char encoded[4];
		size_t produced;
		if(conv->to == ENC_UTF8)
			produced = encodeUtf8(cp, encoded);
		else
			produced = encodeCodepage(tableFor(conv->to), cp, encoded);

		if(produced == 0)
		{
			errno = EILSEQ;
			return (size_t)-1;
		}
		if(*outbytesleft < produced)
		{
			errno = E2BIG;
			return (size_t)-1;
		}

		memcpy(*outbuf, encoded, produced);
		*inbuf += consumed;
		*inbytesleft -= consumed;
		*outbuf += produced;
		*outbytesleft -= produced;
	}

	return 0;
}

int iconv_close(iconv_t cd)
{
	free(cd);
	return 0;
}

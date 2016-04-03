
#ifndef _S5CONF_UTILS_H

#ifdef __S5_CONFUTILS_H
#error now c++ s5 conf utils is using.
#endif

#define ZERO_MEM(ptr, sz) 	memset((void*)(ptr), 0, (sz))
#define APPEND_CHAR(dst, ch)	{char _ch = (ch);\
				 char *_src = &_ch;\
				 strncat((dst), _src, 1);}

#ifndef __cplusplus
#define	bool 	int
#define true 	1
#define false 	0
#endif

#define DEFAULT_SEC	"global"

typedef struct _tag_CONFLINE
{
	char *key;
	char *val;
	char *section;
	char *comment;
	int    line_no;
	struct _tag_CONFLINE *next;
}CONFLINE;/*key/value*/

//maxlen of CONFLINE's field
#define LINE_LEN	1024

typedef struct _tag_CONFSECTION
{
	char *section;
	//per section has many key/values
	CONFLINE *lines;
	struct _tag_CONFSECTION *next;
}CONFSECTION;

typedef struct _tag_CONFFILE
{
	//one file has many sections;
	CONFSECTION *sections;
	//struct _tag_CONFFILE *next;
}CONFFILE;

int cf_parse_file(CONFFILE *cf, const char *fname, char *errors, char *warnings);
int cf_read(CONFFILE *cf, const char *section, const char *key, const char **val);
void cf_free(CONFFILE **cf);
void cf_print(CONFFILE *cf);

#endif


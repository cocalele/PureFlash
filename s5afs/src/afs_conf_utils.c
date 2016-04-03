
// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>
#include  "/usr/include/errno.h"
#include "asm-generic/errno-base.h"

#include "s5utf8.h"
#include "s5conf_utils.h"


#define MAX_CONFIG_FILE_SZ 0x40000000

//////////////////////////////// ConfLine //////////////////////////////
//ConfLine::
//ConfLine(const std::string &key_, const std::string val_,
//      const std::string newsection_, const std::string comment_, int line_no_)
//  : key(key_), val(val_), newsection(newsection_)
//{
//  // If you want to implement writable ConfFile support, you'll need to save
//  // the comment and line_no arguments here.
//}
//
//bool ConfLine::
//operator<(const ConfLine &rhs) const
//{
//  // We only compare keys.
//  // If you have more than one line with the same key in a given section, the
//  // last one wins.
//  if (key < rhs.key)
//    return true;
//  else
//    return false;
//}

//std::ostream &operator<<(std::ostream& oss, const ConfLine &l)
//{
//  oss << "ConfLine(key = '" << l.key << "', val='"
//      << l.val << "', newsection='" << l.newsection << "')";
//  return oss;
//}

int str_empty(const char *str)
{
  return (str && strlen(str)>0) ? 0 : 1;
}

void cl_print(CONFLINE *cl)
{
  if (!cl)
    {puts("NULL");return;}
  if (!str_empty(cl->section))
    {putchar('[');printf(cl->section);puts("]");}
  if (!str_empty(cl->key))
    {putchar('\t');printf(cl->key);putchar('=');}
  if (!str_empty(cl->val))
    {puts(cl->val);}
  if (!str_empty(cl->comment))
    {putchar('#');puts(cl->comment);}
}

//new cl node and member
CONFLINE *cl_new()
{
  CONFLINE *cl = malloc(sizeof(CONFLINE));
  ZERO_MEM(cl, sizeof(CONFLINE));
  cl->key = malloc(LINE_LEN + 1);
  ZERO_MEM(cl->key, LINE_LEN + 1);

  cl->val = malloc(LINE_LEN + 1);
  ZERO_MEM(cl->val, LINE_LEN + 1);

  cl->section= malloc(LINE_LEN + 1);
  ZERO_MEM(cl->section, LINE_LEN + 1);

  cl->comment= malloc(LINE_LEN + 1);
  ZERO_MEM(cl->comment, LINE_LEN + 1);

/*
  static int nNew = 0;
  printf("call cl_new() : %d\n", ++nNew);
*/

  return cl;
}

//release cl node and member
void cl_release(CONFLINE *cl)
{
  if (!cl) return;

  /*
  static int nDel = 0;
  printf("call cl_release() : %d\t", ++nDel);
  cl_print(*cl);
  puts("");
  */

  free((void*)cl->key);
  free((void*)cl->val);
  free((void*)cl->section);
  free((void*)cl->comment);
  free((void*)cl);
}

//clear cl list
void cl_clear(CONFLINE *cl)
{
  if (NULL == cl) return;

  //cl_print(cl);

  cl_clear(cl->next);
  cl_release(cl);
}

//release cs node and member
void cs_release(CONFSECTION *cs)
{
  if (!cs) return;
  cl_clear(cs->lines);
  free((void*)cs->section);
  free((void*)cs);
}

//clear cs list
void cs_clear(CONFSECTION *cs)
{
  if (NULL == cs) return;
  //printf("[%s]\n", cs->section);
  //CONFSECTION **tmp = &cs->next;

  cs_clear(cs->next);
  cs_release(cs);
}

void cf_clear(CONFFILE *cf)
{
  if (NULL == cf)return;
  cs_clear(cf->sections);
}

void cf_free(CONFFILE **cf)
{
  if (!cf || !*cf) return;
  cf_clear(*cf);
  free(*cf);
  *cf = NULL;
}

/* We load the whole file into memory and then parse it.  Although this is not
 * the optimal approach, it does mean that most of this code can be shared with
 * the bufferlist loading function. Since bufferlists are always in-memory, the
 * load_from_buffer interface works well for them.
 * In general, configuration files should be a few kilobytes at maximum, so
 * loading the whole configuration into memory shouldn't be a problem.
 */

/*
int ConfFile::
parse_bufferlist(ceph::bufferlist *bl, std::deque<std::string> *errors,
		 std::ostream *warnings)
{
  clear();

  load_from_buffer(bl->c_str(), bl->length(), errors, warnings);
  return 0;
}
*/

CONFSECTION *cf_find(CONFFILE *cf, const char *section)
{
  if (NULL == cf) return NULL;
  if (NULL == section) return NULL;
  CONFSECTION *tmp = cf->sections;
  while (tmp && strcmp(tmp->section, section)!=0)
  {
    tmp = tmp->next;
  }
  return tmp;
}

CONFLINE* cs_find(CONFSECTION *cs, const char *key)
{
  if (NULL == cs) return NULL;
  if (NULL == key) return NULL;
  CONFLINE *tmp = cs->lines;
  while (tmp && strcmp(tmp->key, key)!=0)
  {
    tmp = tmp->next;
  }
  return tmp;
}


void trim_whitespace(char *str, bool strip_internal)
{
  if (NULL == str) return;
  // strip preceding
  const char *in = str;
  while (true)
  {
    char c = *in;
    if ((!c) || (!isspace(c)))
      break;
    ++in;
  }
  char *output = malloc(strlen(in) + 1);
  strcpy(output, in);

  // strip trailing
  char *o = output + strlen(output);
  while (true) {
    if (o == output)
      break;
    --o;
    if (!isspace(*o)) {
      ++o;
      *o = '\0';
      break;
    }
  }

  if (!strip_internal)
  {
    strcpy(str,output);
    free((void*)output);
    return;
  }

  // strip internal
  char *output2 = malloc(strlen(output) + 1);
  char *out2 = output2;
  bool prev_was_space = false;
  char *u = output;
  for (; *u; ++u) {
    char c = *u;
    if (isspace(c)) {
      if (!prev_was_space)
	*out2++ = c;
      prev_was_space = true;
    }
    else {
      *out2++ = c;
      prev_was_space = false;
    }
  }
  *out2++ = '\0';
  strcpy(str, output2);
  free((void*)output);
  free((void*)output2);
}

/* Normalize a key name.
 *
 * Normalized key names have no leading or trailing whitespace, and all
 * whitespace is stored as underscores.  The main reason for selecting this
 * normal form is so that in common/config.cc, we can use a macro to stringify
 * the field names of md_config_t and get a key in normal form.
 */
char* normalize_key_name(const char *key, char *newkey)
{
  if (!key || !newkey) return NULL;
  //char *k = malloc(strlen(key) + 1);
  //strcpy(k, key);

  if (key != newkey)
    strcpy(newkey, key);
  trim_whitespace(newkey, true);
  char *p = newkey;
  while (*p)
  {
    if (' ' == *p)
      *p = '_';
    p++;
  }
  return newkey;
}

int cf_read(CONFFILE *cf, const char *section, const char *key, const char **val)
{
  if (!cf || !section || !key || !val) return -ENOENT;
  size_t nlen = strlen(key);
  char *k = malloc(nlen + 1);
  k[nlen] = 0;
  strcpy(k, key);
  normalize_key_name(k, k);

  CONFSECTION *cs = cf_find(cf, section);
  if (NULL == cs)
  {
    free((void*)k);
    return -ENOENT;
  }
  CONFLINE *cl = cs_find(cs, k);
  if (NULL == cl)
  {
    free((void*)k);
    return -ENOENT;
  }
  //strcpy(val, cl->key);
  *val = cl->val;
  free((void*)k);
  return 0;
}

void conf_write(FILE *oss, const CONFFILE *cf)
{
  if (/*!oss ||*/ !cf) return;
  const CONFSECTION *cs = cf->sections;
  for (; cs!=NULL; cs=cs->next) {
    //oss << "[" << s->first << "]\n";
    if (oss)
      fprintf(oss, "[%s]", cs->section);
    else
      printf("[%s]\n", cs->section);
    CONFLINE *cl = cs->lines;
    for (; cl!=NULL; cl=cl->next) {
      if (!str_empty(cl->key) && !str_empty(cl->val)) {
	//oss << "\t" << l->key << " = \"" << l->val << "\"\n";
	if (oss)
	  fprintf(oss, "\t%s = \"%s\"\n", cl->key, cl->val);
	else
	  printf("\t%s = \"%s\"\n", cl->key, cl->val);
      }
    }
  }
}

void cf_print(CONFFILE *cf)
{
  conf_write(NULL, cf);
}

void cs_erase(CONFSECTION *cs, const char *key)
{
  if (!cs || !key) return;
  CONFLINE *tmp = cs->lines;
  if (strcmp(tmp->key, key) == 0)
  {
    cs->lines = tmp->next;
    cl_release(tmp);
    return;
  }
  CONFLINE *tmpn = tmp->next;
  while (tmpn)
  {
    if (strcmp(tmpn->key, key) == 0)
      break;
    tmp = tmpn;
    tmpn = tmpn->next;
  }
  if (NULL == tmpn)
  {
    return;
  }
  tmp->next = tmpn->next;
  cl_release(tmpn);
}

void cs_push_back(CONFSECTION **cs_list, CONFSECTION *node)
{
  if (!node) return;
  if (!cs_list)
  {
    cs_release(node);
    return;
  }
  if (!*cs_list)
  {
    //head
    *cs_list = node;
    return;
  }
  //tail
  CONFSECTION *tmp = *cs_list;
  while (tmp->next)
  {
    tmp = tmp->next;
  }
  tmp->next = node;
}

CONFSECTION *cs_new_push_back(CONFSECTION **cs, const char *section)
{
  if (!cs || !section)
  {
    return NULL;
  }

  CONFSECTION *newsec = malloc(sizeof(CONFSECTION));
  memset((void*)newsec, 0, sizeof(CONFSECTION));
  newsec->section = malloc(strlen(section) + 1);
  strcpy(newsec->section, section);
  cs_push_back(cs, newsec);
  return newsec;
}

void cl_push_back(CONFLINE **cl_list, CONFLINE *node)
{
  if (!cl_list || !node) return;
  //cl_print(node);
  CONFLINE *tmp = *cl_list;
  if (NULL == tmp)
  {
  	*cl_list = node;
	return;
  }
  while (tmp->next)
  {
    tmp = tmp->next;
  }
  tmp->next = node;
}

/*
 * A simple state-machine based parser.
 * This probably could/should be rewritten with something like boost::spirit
 * or yacc if the grammar ever gets more complex.
 */

CONFLINE* process_line(int line_no, const char *line, char *errors)
{
  typedef enum
  {
    ACCEPT_INIT,
    ACCEPT_SECTION_NAME,
    ACCEPT_KEY,
    ACCEPT_VAL_START,
    ACCEPT_UNQUOTED_VAL,
    ACCEPT_QUOTED_VAL,
    ACCEPT_COMMENT_START,
    ACCEPT_COMMENT_TEXT,
  }acceptor_state_t;
  const char *l = line;
  acceptor_state_t state = ACCEPT_INIT;
  //string key, val, newsection, comment;
  CONFLINE *cl = cl_new();
  bool escaping = false;
  while (true) {
    char c = *l++;
    switch (state) {
      case ACCEPT_INIT:
	if (c == '\0')
	{
	  // blank line. Not an error, but not interesting either.
	  cl_release(cl);
	  return NULL;
	}
	else if (c == '[')
	  state = ACCEPT_SECTION_NAME;
	else if ((c == '#') || (c == ';'))
	  state = ACCEPT_COMMENT_TEXT;
	else if (c == ']')
	{
	  if (errors)
	  {
	    char oss[300];
	    sprintf(oss, "unexpected right bracket at char %ld, line %d\n", (l - line), line_no);
	    strcat(errors, oss);
	  }
	  cl_release(cl);
	  return NULL;
	}
	else if (isspace(c)) {
	  // ignore whitespace here
	}
	else {
	  // try to accept this character as a key
	  state = ACCEPT_KEY;
	  --l;
	}
	break;
      case ACCEPT_SECTION_NAME:
	if (c == '\0') {
	  if (errors)
	  {
	    char oss[300];
	    sprintf(oss, "error parsing new section name: expected right bracket at char %ld, line %d\n"
	      , (l - line), line_no);
	    strcat(errors, oss);
	  }
	  cl_release(cl);
	  return NULL;
	}
	else if ((c == ']') && (!escaping)) {
	  trim_whitespace(cl->section, true);
	  if (str_empty(cl->section)) {
	    if (errors)
	    {
	      char oss[300];
	      sprintf(oss, "error parsing new section name: no section name found? at char %ld, line %d\n"
		, (l - line), line_no);
	      strcat(errors, oss);
	    }
	    cl_release(cl);
	    return NULL;
	  }
	  state = ACCEPT_COMMENT_START;
	}
	else if (((c == '#') || (c == ';')) && (!escaping)) {
	  if (errors)
	  {
	      char oss[300];
	      sprintf(oss, "unexpected comment marker while parsing new section name, at char %ld, line %d\n"
		, (l - line), line_no);
	      strcat(errors, oss);
	  }
	  cl_release(cl);
	  return NULL;
	}
	else if ((c == '\\') && (!escaping)) {
	  escaping = true;
	}
	else {
	  escaping = false;
	  APPEND_CHAR(cl->section, c);
	}
	break;
      case ACCEPT_KEY:
	if ((((c == '#') || (c == ';')) && (!escaping)) || (c == '\0')) {
	  if (errors)
	  {
		  char oss[300];
		  if (c == '\0') {
		    sprintf(oss, "end of key=val line %d reached, no \"=val\" found...missing =?", line_no);
		  } else {
		    sprintf(oss, "unexpected character while parsing putative key value, at char %ld, line %d"
		      , (l - line), line_no);
		  }
		  strcat(errors, oss);
	  }
	  cl_release(cl);
	  return NULL;
	}
	else if ((c == '=') && (!escaping)) {
	  normalize_key_name(cl->key, cl->key);
	  if (str_empty(cl->key)) {
	    if (errors)
	    {
	      char oss[300];
	      sprintf(oss, "error parsing key name: no key name found? at char %ld\
		, line %d\n", (l-line), line_no);
	      strcat(errors, oss);
	    }
	    cl_release(cl);
	    return NULL;
	  }
	  state = ACCEPT_VAL_START;
	}
	else if ((c == '\\') && (!escaping)) {
	  escaping = true;
	}
	else {
	  escaping = false;
	  APPEND_CHAR(cl->key, c);
	}
	break;
      case ACCEPT_VAL_START:
	if (c == '\0')
	  return cl;
	else if ((c == '#') || (c == ';'))
	  state = ACCEPT_COMMENT_TEXT;
	else if (c == '"')
	  state = ACCEPT_QUOTED_VAL;
	else if (isspace(c)) {
	  // ignore whitespace
	}
	else {
	  // try to accept character as a val
	  state = ACCEPT_UNQUOTED_VAL;
	  --l;
	}
	break;
      case ACCEPT_UNQUOTED_VAL:
	if (c == '\0') {
	  if (escaping) {
	    if (errors)
	    {
	      char oss[300];
	      sprintf(oss, "error parsing value mane: unterminated escape sequence at char %ld, line %d\n"
		, l - line, line_no);
	      strcat(errors, oss);
	    }
	    cl_release(cl);
	    return NULL;
	  }
	  trim_whitespace(cl->val, false);
	  return cl;
	}
	else if (((c == '#') || (c == ';')) && (!escaping)) {
	  trim_whitespace(cl->val, false);
	  state = ACCEPT_COMMENT_TEXT;
	}
	else if ((c == '\\') && (!escaping)) {
	  escaping = true;
	}
	else {
	  escaping = false;
	  APPEND_CHAR(cl->val, c);
	}
	break;
      case ACCEPT_QUOTED_VAL:
	if (c == '\0') {
	  if (errors)
	  {
	    char oss[300];
	    sprintf(oss, "found opening quote for value, but not the closing quote. line %d"
	      , line_no);
	    strcat(errors, oss);
	  }
	  cl_release(cl);
	  return NULL;
	}
	else if ((c == '"') && (!escaping)) {
	  state = ACCEPT_COMMENT_START;
	}
	else if ((c == '\\') && (!escaping)) {
	  escaping = true;
	}
	else {
	  escaping = false;
	  // Add anything, including whitespace.
	  APPEND_CHAR(cl->val, c);
	}
	break;
      case ACCEPT_COMMENT_START:
	if (c == '\0') {
	  return cl;
	}
	else if ((c == '#') || (c == ';')) {
	  state = ACCEPT_COMMENT_TEXT;
	}
	else if (isspace(c)) {
	  // ignore whitespace
	}
	else {
	  if (errors)
	  {
	    char oss[300];
	    sprintf(oss, "unexpected character at char %ld of line %d\n", (l - line), line_no);
	    strcat(errors, oss);
	  }
	  cl_release(cl);
	  return NULL;
	}
	break;
      case ACCEPT_COMMENT_TEXT:
	if (c == '\0')
	  return cl;
	else
	  APPEND_CHAR(cl->comment, c);
	break;
      default:
//	assert(0);
	break;
    }
//    assert(c != '\0'); // We better not go past the end of the input string.
  }
}

void load_from_buffer(CONFFILE *cf, const char *buf, int sz, char *errors, char *warnings)
{
  if (!cf || !buf/* || !errors || !warnings*/)
    return;

  if (NULL != errors)
    errors[0] = 0;
  if (NULL != warnings)
    warnings[0] = 0;

  CONFSECTION *cur_cs = cs_new_push_back(&cf->sections, DEFAULT_SEC);
  //section_iter_t::value_type vt("global", ConfSection());
  //pair < section_iter_t, bool > vr(sections.insert(vt));
  //section_iter_t cur_section = vr.first;
  //std::string acc;

  char *acc = malloc(1024);
  acc[0] = 0;
  const char *b = buf;
  int line_no = 0;
  int line_len = -1;
  int rem = sz;
  while (1) {
    b += line_len + 1;
    rem -= line_len + 1;
    if (rem == 0)
      break;
    line_no++;

    // look for the next newline
    const char *end = (const char*)memchr(b, '\n', (size_t)rem);
    if (!end) {
      if (errors)
      {
	char oss[300];
	sprintf(oss, "read_conf: ignoring line %d because it doesn't end with a newline!\
	Please end the config file with a newline.", line_no);
	strcat(errors, oss);
      }
      break;
    }

    // find length of line, and search for NULLs
    line_len = 0;
    bool found_null = false;
    const char *tmp = b;
    for (; tmp != end; ++tmp) {
      line_len++;
      if (*tmp == '\0') {
	found_null = true;
      }
    }

    if (found_null) {
      if (errors)
	{
	      char oss[300];
	      sprintf(oss, "read_conf: ignoring line %d because it has an embedded null.", line_no);
	      strcat(errors, oss);
	}
	acc[0] = 0;
	continue;
    }

    if (check_utf8(b, line_len)) {
      if (errors)
	{
	      char oss[300];
	      sprintf(oss, "read_conf: ignoring line %d because it is not \
		valid UTF8.", line_no);
	      strcat(errors, oss);
	}
	acc[0] = 0;
	continue;
    }

    if ((line_len >= 1) && (b[line_len-1] == '\\')) {
      // A backslash at the end of a line serves as a line continuation marker.
      // Combine the next line with this one.
      // Remove the backslash itself from the text.
      strncat(acc, b, (size_t)line_len - 1);
      continue;
    }

    strncat(acc, b, (size_t)line_len);

    //printf("<%d>\t%s\n", line_no, acc);
    CONFLINE *cline = process_line(line_no, acc, errors);

    //printf("--------------------- %d ---------------\n", line_no);
    //cf_print(cf);
    //printf("--------------------- %d ---------------\n", line_no);

    acc[0] = 0;
    if (!cline)
      continue;
    const char *csection = cline->section;
    if (!str_empty(csection)){
      if (cf_find(cf, csection))
      {
	//if exists do nothing now
      }
      /*CONFSECTION *newsec = malloc(sizeof(CONFSECTION));
      memset((void*)newsec, 0, sizeof(CONFSECTION));
      newsec->section = malloc(strlen(cline->section) + 1);
      strcpy(newsec->section, cline->section);
      //newsec->lines = NULL;
      //newsec->next = NULL;
      cs_push_back(&cf->sections, newsec);*/
      cur_cs = cs_new_push_back(&cf->sections, cline->section);
      cl_release(cline);
    }
    else {
	if (cs_find(cur_cs, cline->key))
	{
	  //if exists do nothing now
	  // replace an existing key/line in this section, so that
	  //  [mysection]
	  //    foo = 1
	  //    foo = 2
	  // will result in foo = 2.
	  cs_erase(cur_cs, cline->key);
	  if (!str_empty(cline->key) && warnings)
	  {
	    char oss[300];
	    sprintf(oss, "warning: line : %d '%s' in section '%s' redefined \n"
	      , line_no, cline->key, (cur_cs)->section);
	    strcat(warnings, oss);
	  }
	}
      // add line to current section
      //std::cerr << "cur_section = " << cur_section->first << ", " << *cline << std::endl;
      cl_push_back(&(cur_cs)->lines, cline);
    }
    //cl_release(cline);
  }

  if (strlen(acc) != 0) {
    if (errors)
      strcat(errors, "read_conf: don't end with lines that end in backslashes!");
  }
  //cf_print(cf);
  free((void*)acc);
}


int cf_parse_file(CONFFILE *cf, const char *fname, char *errors, char *warnings)
{
  //if (NULL==errors || NULL==warnings)return -errno;
  if (!cf || !fname) return -errno;
  cf_clear(cf);

  int ret = 0;
  int sz;
  char *buf = NULL;
  FILE *fp = fopen(fname, "r");
  if (!fp) {
    ret = -errno;
    return ret;
  }

  struct stat st_buf;
  if (fstat(fileno(fp), &st_buf)) {
    if (errors)
    {
      char oss[100];
      sprintf(oss, "read_conf: failed to fstat '%s' : %d\n", fname , ret);
      strcat(errors, oss);
    }
    ret = -errno;
    goto done;
  }

  if (st_buf.st_size > MAX_CONFIG_FILE_SZ) {
    if (errors)
    {
      char oss[100];
      sprintf(oss, "read_conf: config file '%s' is %ld bytes, but the maximum is %d.\n"
	, fname, st_buf.st_size ,MAX_CONFIG_FILE_SZ);
      strcat(errors, oss);
    }
    ret = -EINVAL;
    goto done;
  }

  sz = (int)st_buf.st_size;
  buf = (char*)malloc((size_t)sz);
  if (!buf) {
    ret = -ENOMEM;
    goto done;
  }

  if (fread(buf, 1, (size_t)sz, fp) != sz) {
    if (ferror(fp)) {
      ret = -errno;
      if (errors)
      {
	char oss[100];
	sprintf(oss ,"read_conf: fread error while reading '%s' : %d", fname, ret);
	strcat(errors, oss);
      }
      goto done;
    }
    else {
      if (errors)
      {
	char oss[100];
	sprintf(oss ,"read_conf: unexpected EOF while reading '%s' : possible concurrent modification?\n", fname);
	strcat(errors, oss);
      }
      ret = -EIO;
      goto done;
    }
  }

  load_from_buffer(cf, buf, sz, errors, warnings);
  ret = 0;

done:
  free(buf);
  fclose(fp);
  return ret;
}



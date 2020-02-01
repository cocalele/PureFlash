// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2011 New Dream Network
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include <algorithm>
#include <errno.h>
#include <list>
#include <map>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include  "/usr/include/errno.h"
#include "asm-generic/errno-base.h"

//#include "include/buffer.h"
#include "s5errno.h"
#include "s5utf8.h"
#include "s5conf_utils.h"

//using std::cerr;
using std::ostringstream;
using std::pair;
using std::string;

#define MAX_CONFIG_FILE_SZ 0x40000000

////////////////////////////// ConfLine //////////////////////////////
ConfLine::
ConfLine(const std::string &key_, const std::string val_,
         const std::string newsection_, const std::string comment_, int line_no_)
	: key(key_), val(val_), newsection(newsection_)
{
	// If you want to implement writable ConfFile support, you'll need to save
	// the comment and line_no arguments here.
}

bool ConfLine::
operator<(const ConfLine &rhs) const
{
	// We only compare keys.
	// If you have more than one line with the same key in a given section, the
	// last one wins.
	if (key < rhs.key)
		return true;
	else
		return false;
}

std::ostream &operator<<(std::ostream& oss, const ConfLine &l)
{
	oss << "ConfLine(key = '" << l.key << "', val='"
	    << l.val << "', newsection='" << l.newsection << "')";
	return oss;
}
///////////////////////// ConfFile //////////////////////////
ConfFile::
ConfFile(const char* file):
	conf_file(file)
{
}

ConfFile::
~ConfFile()
{
}

void ConfFile::
clear()
{
	sections.clear();
}

int ConfFile::parse_file()
{
	clear();

	int ret = 0;
	size_t sz;
	char *buf = NULL;
	FILE *fp = fopen(conf_file.c_str(), "r");
	if (!fp)
	{
		ret = -errno;
		S5LOG_ERROR("Failed open file:%s, rc:%d", conf_file.c_str(), ret);
		return ret;
	}

	struct stat st_buf;
	if (fstat(fileno(fp), &st_buf))
	{
		ret = -errno;
		if (ret == -EBADF || ret == -EFAULT )
		{
			S5LOG_ERROR("Internal error when stat configuration file '%s'(errno: %d): %s.", conf_file.c_str(), ret, strerror(ret));
			ret = -S5_INTERNAL_ERR;
		}
		else
		{
			S5LOG_ERROR("Failed to read_conf: fstat '%s'(errno: %d): %s.", conf_file.c_str(), ret, strerror(ret));
		}
		goto done;
	}

	if (st_buf.st_size > MAX_CONFIG_FILE_SZ)
	{
		S5LOG_ERROR("Failed to read_conf: config file '%s'is %lu bytes, but the maximum is %d.", conf_file.c_str(), st_buf.st_size, MAX_CONFIG_FILE_SZ);
		ret = -EINVAL;
		goto done;
	}
	sz = (size_t)st_buf.st_size;
	buf = (char*)malloc(sz);
	if (!buf)
	{
		ret = -ENOMEM;
		S5LOG_ERROR("Out of memory, please check system environment to check memory usage status.");
		goto done;
	}

	if (fread(buf, 1, sz, fp) != sz)
	{
		if (ferror(fp))
		{
			ret = -S5_INTERNAL_ERR;
			S5LOG_ERROR("Failed to read_conf: fread error while reading '%s'(errno: %d): %s.", conf_file.c_str(), ret, strerror(ret));
			goto done;
		}
		else
		{
			S5LOG_ERROR("Failed to read_conf: unexpected EOF while reading '%s', possible concurrent modification.", conf_file.c_str());
			ret = -EIO;
			goto done;
		}
	}

	ret = load_from_buffer(buf, sz);
done:
	free(buf);
	fclose(fp);
	return ret;
}

int ConfFile::
read(const std::string &section, const std::string &key, std::string ** val) const
{
	string k(normalize_key_name(key));
	const_section_iter_t s = sections.find(section);
	if (s == sections.end())
		return -ENOENT;
	ConfLine exemplar(k, "", "", "", 0);
	ConfSection::const_line_iter_t l = s->second.lines.find(exemplar);
	if (l == s->second.lines.end())
		return -ENOENT;
	*val = (std::string*)&l->val;
	return 0;
}

ConfFile::const_section_iter_t ConfFile::
sections_begin() const
{
	return sections.begin();
}

ConfFile::const_section_iter_t ConfFile::
sections_end() const
{
	return sections.end();
}

void ConfFile::
trim_whitespace(std::string &str, bool strip_internal)
{
	// strip preceding
	const char *in = str.c_str();
	while (true)
	{
		char c = *in;
		if ((!c) || (!isspace(c)))
			break;
		++in;
	}
	char output[strlen(in) + 1];
	strcpy(output, in);

	// strip trailing
	char *o = output + strlen(output);
	while (true)
	{
		if (o == output)
			break;
		--o;
		if (!isspace(*o))
		{
			++o;
			*o = '\0';
			break;
		}
	}

	if (!strip_internal)
	{
		str.assign(output);
		return;
	}

	// strip internal
	char output2[strlen(output) + 1];
	char *out2 = output2;
	bool prev_was_space = false;
	for (char *u = output; *u; ++u)
	{
		char c = *u;
		if (isspace(c))
		{
			if (!prev_was_space)
				*out2++ = c;
			prev_was_space = true;
		}
		else
		{
			*out2++ = c;
			prev_was_space = false;
		}
	}
	*out2++ = '\0';
	str.assign(output2);
}

std::string ConfFile::
normalize_key_name(const std::string &key)
{
	string k(key);
	ConfFile::trim_whitespace(k, true);
	std::replace(k.begin(), k.end(), ' ', '_');
	return k;
}

std::ostream &operator<<(std::ostream &oss, const ConfFile &cf)
{
	for (ConfFile::const_section_iter_t s = cf.sections_begin();
	        s != cf.sections_end(); ++s)
	{
		oss << "[" << s->first << "]\n";
		for (ConfSection::const_line_iter_t l = s->second.lines.begin();
		        l != s->second.lines.end(); ++l)
		{
			if (!l->key.empty())
			{
				oss << "\t" << l->key << " = \"" << l->val << "\"\n";
			}
		}
	}
	return oss;
}

void ConfLine::set_val(const std::string &key_, const std::string val_, const std::string newsection_, const std::string comment_,
					  int line_no_)
{
	key = key_;
	val = val_;
	newsection = newsection_;
}

int ConfFile::load_from_buffer(const char *buf, size_t sz)
{
	int ret = 0;
	section_iter_t::value_type vt("global", ConfSection());
	pair < section_iter_t, bool > vr(sections.insert(vt));
	//  assert(vr.second);
	section_iter_t cur_section = vr.first;
	std::string acc;
	const char *b = buf;
	int line_no = 0;
	size_t line_len = 0;
	size_t rem = sz;
	ConfLine *cline = new ConfLine();
	while (1)
	{
		b += line_len;
		rem -= line_len;
		if (rem == 0)
			break;
		line_no++;

		// look for the next newline
		const char *end = (const char*)memchr(b, '\n', rem);
		if (!end)
		{
			S5LOG_ERROR("Ignoring line %d when read configure file, because it doesn't " \
                        "end with a newline! Please end the config file with a newline.", line_no);
			ret = -S5_CONF_ERR;
			break;
		}

		// find length of line, and search for NULLs
		line_len = 0;
		bool found_null = false;
		for (const char *tmp = b; tmp != end; ++tmp)
		{
			line_len++;
			if (*tmp == '\0')
			{
				found_null = true;
			}
		}

		if (found_null)
		{
			S5LOG_WARN("Ignoring line %d when read configure file, because it has an embedded null.", line_no);
			acc.clear();
			continue;
		}

		if (check_utf8(b, (int)line_len))
		{
			S5LOG_WARN("Ignoring line %d when read configure file, because it is not valid UTF8.", line_no);
			acc.clear();
			continue;
		}

		if ((line_len >= 1) && (b[line_len - 1] == '\\'))
		{
			// A backslash at the end of a line serves as a line continuation marker.
			// Combine the next line with this one.
			// Remove the backslash itself from the text.
			acc.append(b, line_len - 1);
			continue;
		}

		acc.append(b, line_len);

		//cerr << "acc = '" << acc << "'" << std::endl;
		ret = process_line(line_no, acc.c_str(), cline);
		acc.clear();
		line_len++;		// count '\n' into line length
		if (ret != 0)
		{
			if (ret == -ENOENT)
			{
				ret = 0;
				continue;
			}
			break;
		}
		const std::string &csection(cline->newsection);
		if (!csection.empty())
		{
			std::map <std::string, ConfSection>::value_type nt(csection, ConfSection());
			pair < section_iter_t, bool > nr(sections.insert(nt));
			cur_section = nr.first;
		}
		else
		{
			if (cur_section->second.lines.count(*cline))
			{
				cur_section->second.lines.erase(*cline);
				if (cline->key.length())
				{
					S5LOG_WARN("Redefined line %d: '%s' in section '%s'.", line_no, cline->key.c_str(), cur_section->first.c_str());
				}
			}
			cur_section->second.lines.insert(*cline);
		}
	}

	delete cline;
	if (!acc.empty())
	{
		S5LOG_ERROR("Don't end with lines that end in backslashes when read configure file!");
		ret = -S5_CONF_ERR;
	}
	return ret;
}

int ConfFile::process_line(int line_no, const char *line, ConfLine* conf_line)
{
	enum acceptor_state_t
	{
		ACCEPT_INIT,
		ACCEPT_SECTION_NAME,
		ACCEPT_KEY,
		ACCEPT_VAL_START,
		ACCEPT_UNQUOTED_VAL,
		ACCEPT_QUOTED_VAL,
		ACCEPT_COMMENT_START,
		ACCEPT_COMMENT_TEXT,
	};
	const char *l = line;
	acceptor_state_t state = ACCEPT_INIT;
	string key, val, newsection, comment;
	bool escaping = false;
	while (true)
	{
		char c = *l++;
		switch (state)
		{
		case ACCEPT_INIT:
			if (c == '\0')
				return -ENOENT; // blank line. Not an error, but not interesting either.
			else if (c == '[')
				state = ACCEPT_SECTION_NAME;
			else if ((c == '#') || (c == ';'))
				state = ACCEPT_COMMENT_TEXT;
			else if (c == ']')
			{
				S5LOG_ERROR("Unexpected right bracket at char %ld, line %d in configuration file %s.",
					(l - line), line_no, conf_file.c_str());
				return -S5_CONF_ERR;
			}
			else if (isspace(c))
			{
				// ignore whitespace here
			}
			else
			{
				// try to accept this character as a key
				state = ACCEPT_KEY;
				--l;
			}
			break;
		case ACCEPT_SECTION_NAME:
			if (c == '\0')
			{
				S5LOG_ERROR("Failed to  parsing new section name: expected right bracket at char %ld line %d in configuration file %s.",
					(l - line), line_no, conf_file.c_str());
				return -S5_CONF_ERR;
			}
			else if ((c == ']') && (!escaping))
			{
				trim_whitespace(newsection, true);
				if (newsection.empty())
				{
					S5LOG_ERROR("Failed to parsing new section name: no section name found at char %ld line %d in configuration file %s.",
						(l - line), line_no, conf_file.c_str());
					return -S5_CONF_ERR;
				}
				state = ACCEPT_COMMENT_START;
			}
			else if (((c == '#') || (c == ';')) && (!escaping))
			{
				S5LOG_ERROR("Unexpected comment marker while parsing new section name at char %ld line %d in configuration file %s.",
					(l - line), line_no, conf_file.c_str());
				return -S5_CONF_ERR;
			}
			else if ((c == '\\') && (!escaping))
			{
				escaping = true;
			}
			else
			{
				escaping = false;
				newsection += c;
			}
			break;
		case ACCEPT_KEY:
			if ((((c == '#') || (c == ';')) && (!escaping)) || (c == '\0'))
			{
				if (c == '\0')
				{
					S5LOG_ERROR("End of key=val line %d reached but no \"=val\" found...missing =?", line_no);
				}
				else
				{
					S5LOG_ERROR("Unexpected character while parsing putative key value at char %ld, line %d in configuration file %s.",
						(l - line), line_no, conf_file.c_str());
				}
				return -S5_CONF_ERR;
			}
			else if ((c == '=') && (!escaping))
			{
				key = normalize_key_name(key);
				if (key.empty())
				{
					S5LOG_ERROR("Failed to parse key name: no key name found at char %ld, line %d in configuration file %s.",
						(l - line), line_no, conf_file.c_str());
					return -S5_CONF_ERR;
				}
				state = ACCEPT_VAL_START;
			}
			else if ((c == '\\') && (!escaping))
			{
				escaping = true;
			}
			else
			{
				escaping = false;
				key += c;
			}
			break;
		case ACCEPT_VAL_START:
			if (c == '\0')
			{
				conf_line->set_val(key, val, newsection, comment, line_no);
				return 0;
			}
			else if ((c == '#') || (c == ';'))
				state = ACCEPT_COMMENT_TEXT;
			else if (c == '"')
				state = ACCEPT_QUOTED_VAL;
			else if (isspace(c))
			{
				// ignore whitespace
			}
			else
			{
				// try to accept character as a val
				state = ACCEPT_UNQUOTED_VAL;
				--l;
			}
			break;
		case ACCEPT_UNQUOTED_VAL:
			if (c == '\0')
			{
				if (escaping)
				{
					S5LOG_ERROR("Failed to parse value name: unterminated escape sequence at char %ld, line %d in configuration file %s.",
						(l - line), line_no, conf_file.c_str());
					return -S5_CONF_ERR;
				}
				trim_whitespace(val, false);
				conf_line->set_val(key, val, newsection, comment, line_no);
				return 0;
			}
			else if (((c == '#') || (c == ';')) && (!escaping))
			{
				trim_whitespace(val, false);
				state = ACCEPT_COMMENT_TEXT;
			}
			else if ((c == '\\') && (!escaping))
			{
				escaping = true;
			}
			else
			{
				escaping = false;
				val += c;
			}
			break;
		case ACCEPT_QUOTED_VAL:
			if (c == '\0')
			{
				S5LOG_ERROR("Failed to find opening quote for value, but not the closing quote at char %ld, line %d in configuration file %s.",
					(l - line), line_no, conf_file.c_str());
				return -S5_CONF_ERR;
			}
			else if ((c == '"') && (!escaping))
			{
				state = ACCEPT_COMMENT_START;
			}
			else if ((c == '\\') && (!escaping))
			{
				escaping = true;
			}
			else
			{
				escaping = false;
				// Add anything, including whitespace.
				val += c;
			}
			break;
		case ACCEPT_COMMENT_START:
			if (c == '\0')
			{
				conf_line->set_val(key, val, newsection, comment, line_no);
				return 0;
			}
			else if ((c == '#') || (c == ';'))
			{
				state = ACCEPT_COMMENT_TEXT;
			}
			else if (isspace(c))
			{
				// ignore whitespace
			}
			else
			{
				S5LOG_ERROR("Unexpected character at char %ld, line %d in configuration file %s.",
					(l - line), line_no, conf_file.c_str());
				return -S5_CONF_ERR;
			}
			break;
		case ACCEPT_COMMENT_TEXT:
			if (c == '\0')
			{
				conf_line->set_val(key, val, newsection, comment, line_no);
				return 0;
			}
			else
				comment += c;
			break;
		default:
			//	assert(0);
			break;
		}
		//    assert(c != '\0'); // We better not go past the end of the input string.
	}
}

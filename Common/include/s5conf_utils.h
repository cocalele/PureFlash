
/**
 * Copyright (C), 2014-2015.
 * @file
 *
 * Configuration parse module for S5 modules.
 *
 * All apis and macros about S5 configuration are all defined here.
 *
 * @author xxx
 */

#ifndef __S5_CONFUTILS_H
#define __S5_CONFUTILS_H

#include <deque>
#include <map>
#include <set>
#include <string>

#include "s5_utils.h"

/*
 * Line content parsed from S5 configuration file.
 *
 * A line in S5 configuration file may be consisted of comment, section name, key name, or key value.
 * After a line in S5 configuration file is parsed, section name, key name, and key value will be stored
 * in newsection, key, and val separately if any. ConfLine acts as a member variable in ConfFile object.
 * And user can use APIs of ConfFile to access it. Comment in S5 configuration will not be stored for now.
 */
class ConfLine
{
public:
	/**
	 * Constructor of ConfLine.
	 *
	 * @param[in]	key_			key name, and if no key name exists, it will be an empty string.
	 * @param[in]	val_			key value, and if no key value exists, it will be an empty string.
	 * @param[in]	newsection_	section name, and if no key value exists, it will be an empty string.
	 * @param[in]	comment_		comment, and if no key value exists, it will be an empty string. Besides,
	 *								it will not be stored for now.
	 * @param[in]	line_no_		line number.
	 */
	ConfLine(const std::string &key_, const std::string val_,
	         const std::string newsection_, const std::string comment_, int line_no_);

	/**
	 * Constructor with no arguments.
	 */
	ConfLine() {};

	/**
	 * Operator '<' overloading.
	 *
	 * It is used to compare key in ConfLine. If user has more than one line with the same key in a given
	 * section, the last one wins.
	 *
	 * @param[in]		rhs			ConfLine to compare
	 * @return	if key of object to compare is larger, true will be returned, otherwise, false will be returned.
	 */
	bool operator<(const ConfLine &rhs) const;

	/**
	 * Operator '<<' overloading.
	 *
	 * Friend function of ConfLine, and is used to redirect content of ConfLine to stream specified.
	 *
	 * @param[in]	oss		stream to which content of ConfLine will be redirected
	 * @param[in]	l		ConfLine to redirected
	 *
	 * @return	stream to which content of ConfLine is redirected
	 */
	friend std::ostream &operator<<(std::ostream& oss, const ConfLine &l);

	/**
	 * Initialize content of ConfLine.
	 *
	 * @param[in]	key_			key name, and if no key name exists, it will be an empty string.
	 * @param[in]	val_			key value, and if no key value exists, it will be an empty string.
	 * @param[in]	newsection_	section name, and if no key value exists, it will be an empty string.
	 * @param[in]	comment_		comment, and if no key value exists, it will be an empty string. Besides,
	 *								it will not be stored for now.
	 * @param[in]	line_no_		line number.
	 */
	void set_val(const std::string &key_, const std::string val_,
	             const std::string newsection_, const std::string comment_, int line_no_);

	std::string key;			///< string to store key name
	std::string val;			///< string to store key value
	std::string newsection;		///< string to store section name
};

/*
 * Section content parsed from S5 configuration file.
 *
 * Sections is basic units of a S5 ConfFile object. Sections is composed of multiple lines(ConfLine).
 */
class ConfSection
{
public:
	typedef std::set <ConfLine>::const_iterator const_line_iter_t;	///< iterator of set for lines(ConfLine) access

	std::set <ConfLine> lines;		///< lines(ConfLine) set
};

/*
 * S5 configuration file support.
 *
 * This class loads an INI-style configuration from a file or bufferlist, and
 * holds it in memory. In general, an INI configuration file is composed of
 * sections, which contain key/value pairs. You can put comments on the end of
 * lines by using either a hash mark (#) or the semicolon (;).
 *
 * You can get information out of ConfFile by calling get_key or by examining
 * individual sections.
 *
 * This class could be extended to support modifying configuration files and
 * writing them back out without too much difficulty. Currently, this is not
 * implemented, and the file is read-only.
 */
class ConfFile
{
public:
	typedef std::map <std::string, ConfSection>::iterator section_iter_t;					///< section iterator
	typedef std::map <std::string, ConfSection>::const_iterator const_section_iter_t;		///< const section iterator

	/**
	 * Constructor of ConfFile.
	 *
	 * @param[in] conf_file	configuration file to parse
	 */
	ConfFile(const char* conf_file);
	~ConfFile();		///< destructor
	void clear();		///< clean up all data in ConfFile

	/**
	 * Parse S5 configuration file and construct sections and lines.
	 *
	 * We load the whole file into memory and then parse it.  Although this is not
	 * the optimal approach, it does mean that most of this code can be shared with
	 * the bufferlist loading function. Since bufferlists are always in-memory, the
	 * load_from_buffer interface works well for them.
	 * In general, configuration files should be a few kilobytes at maximum, so
	 * loading the whole configuration into memory shouldn't be a problem.
	 *
	 * @return	0 on success and negative errno for errors
	 *
	 * @retval	0					Success.
	 * @retval	-EINVAL				The mode of S5 configuration file is invalid; size of configuration exceeds 0x40000000 bytes.
	 * @retval	-EACCES				Search permission is denied for one of the directories in the path prefix of
	 *								S5 configuration file path.
	 * @retval	-ENOMEM				Out of memory (i.e., kernel memory).
	 * @retval	-EOVERFLOW			path refers to a file whose size cannot be represented in the type off_t.
	 *								This can occur when an application compiled on a 32-bit platform without
	 *								-D_FILE_OFFSET_BITS=64 calls stat() on a file whose size exceeds (2<<31)-1 bits.
	 * @retval	-EIO				unexpected EOF while reading configuration file, possible concurrent modification may cause it.
	 * @retval	-S5_INTERNAL_ERR	errors may be caused by internal bugs of S5, user can refer to log and S5 manual for help.
	 * @retval	-S5_CONF_ERR		configuration file does not conform to configuration rules
	 */
	int parse_file();

	/**
	 * Read value of key in section specified
	 *
	 * If function successfully returned, value will be stored in parameter 'val'. 'val' is reference of pointer of string.
	 * User is forbidden to modify its buffer and can long term retain it.
	 *
	 * @param[in]		section		section name
	 * @param[in]		key			key name
	 * @param[in,out]	val			where result will be store if function returns successfully
	 *
	 * @return	0 on success and negative errno will be returned if errors occur
	 * @retval	0			success
	 * @retval	-ENOENT		section or key specified cannot be found in configuration data
	 */
	int read(const std::string &section, const std::string &key,
	         std::string** val) const;

	/**
	 * Get const head iterator of section in configuration data.
	 */
	const_section_iter_t sections_begin() const;
	/**
	 * Get const tail iterator of section in configuration data.
	 */
	const_section_iter_t sections_end() const;
	/**
	 * Static function to trim white space in a string,
	 *
	 * In the "C" and "POSIX" locales, white space includes space, form-feed ('\\f'), newline ('\\n'), carriage return
	 * ('\\r'), horizontal tab ('\\t'), and vertical tab ('\\v'). After trim, result will be assigned to original string
	 * object.
	 *
	 * @param[in,out]		str				string to trim, and trimmed string will also be assigned to it.
	 * @param[in]			strip_internal	if false, only white space in head and tail of string will be trimmed, or
	 *										else, all white space will be trimmed.
	 */
	static void trim_whitespace(std::string &str, bool strip_internal);

	/* Normalize a key name.
	 *
	 * Normalized key names have no leading or trailing whitespace, and all
	 * whitespace is stored as underscores.  The main reason for selecting this
	 * normal form is so that in common/config.cc, we can use a macro to stringify
	 * the field names of md_config_t and get a key in normal form.
	 *
	 * @param[in]		key			string to normalize
	 *
	 * @return	string normalized
	 */
	static std::string normalize_key_name(const std::string &key);
	friend std::ostream &operator<<(std::ostream &oss, const ConfFile &cf);

	std::string conf_file;			///< configuration file path
private:
	/**
	 * Parse configuration file content in buf and construct structured data.
	 *
	 * Before this api is called, configuration file has been loaded into memory space. This function is just
	 * used to process configuration file in buffer line by line and init structured data of configuration data
	 * in ConfFile object.
	 *
	 * @param[in]		buf			buffer of configuration file in memory spcae
	 * @param[in]		sz			buffer size in bytes
	 *
	 * @return	0 on success and negative errno if errors occur.
	 * @retval		0				success
	 * @retval		-S5_CONF_ERR	configuration file does not conform to configuration rules
	 */
	int load_from_buffer(const char *buf, size_t sz);

	/*
	 * A simple state-machine based parser.
	 * This probably could/should be rewritten with something like boost::spirit
	 * or yacc if the grammar ever gets more complex.
	 * If no errors occur, a pointer of line content object of ConfLine type will be returned. User can retain this pointer
	 * for long term usage. Until user will not use it anymore, user needs release it with 'delete' method.
	 * If line to process is blank or errors occur when processing line content, a NULL pointer. For errors detailed info,
	 * user can turn to log associated.
	 *
	 * @param[in]		line_no		line number in configuration
	 * @param[in]		line		line to parse
	 * @param[in,out]	conf_line	ConfLine object buffer parse result, user need allocate and free it.
	 *
	 * @return 0 on success and negative errno if errors occur.
	 * @retval		0				success
	 * @retval		-ENOENT			line to process is blank
	 * @retval		-S5_CONF_ERR	line to process does not conform to configuration rules
	 */
	int process_line(int line_no, const char *line, ConfLine* conf_line);
	std::map <std::string, ConfSection> sections;		///< sections in configuration file

	ConfFile();		///< leave undefined
};

#endif

/**
* Copyright (C), 2014-2015.
* @file
* APIs to convert a string to a long integer.
*
* @author xxx
*/

#ifndef __S5_STRTOL_H__
#define __S5_STRTOL_H__

#include <iostream>
#include <string>

/**
 * The function converts the initial part of the string in str to a 64-bit integer value according to the given base,
 * which must be between 2 and 36 inclusive, or be the special value 0.
 *
 * The string may begin with an arbitrary amount of white space (as determined by isspace(3)) followed by a single
 * optional '+' or '-' sign.  If  base is  zero  or 16, the string may then include a "0x" prefix, and the number
 * will be read in base 16; otherwise, a zero base is taken as 10 (decimal) unless the next character is '0', in
 * which case it is taken as 8 (octal).
 * The remainder of the string is converted to a long int value in the obvious manner, stopping at the first
 * character which is not a valid digit in the given base. (In bases above 10, the letter 'A' in either upper or
 * lower case represents 10, 'B' represents 11, and so forth, with 'Z' repre-senting 35.)
 *
 * If this conversion is successful, integer expected will be returned, otherwise, 0 will be returned and error info
 * will be store err.
 *
 * @param[in]		str			string to parse
 * @param[in]		base		base of destination integer number
 * @param[in,out]	err			error information
 *
 * @return		destination integer number with base number specified on success. Or else, 0 will be returned and
 *				error info will be store err.
 */
long long strict_strtoll(const char *str, int base, std::string *err);

/**
 * The function converts the initial part of the string in str to a 32-bit integer value according to the given base,
 * which must be between 2 and 36 inclusive, or be the special value 0.
 *
 * The string may begin with an arbitrary amount of white space (as determined by isspace(3)) followed by a single
 * optional '+' or '-' sign.  If  base is  zero  or 16, the string may then include a "0x" prefix, and the number
 * will be read in base 16; otherwise, a zero base is taken as 10 (decimal) unless the next character is '0', in
 * which case it is taken as 8 (octal).
 * The remainder of the string is converted to a long int value in the obvious manner, stopping at the first
 * character which is not a valid digit in the given base. (In bases above 10, the letter 'A' in either upper or
 * lower case represents 10, 'B' represents 11, and so forth, with 'Z' repre-senting 35.)
 *
 * If this conversion is successful, integer expected will be returned, otherwise, 0 will be returned and error info
 * will be store err.
 *
 * @param[in]		str			string to parse
 * @param[in]		base		base of destination integer number
 * @param[in,out]	err			error information
 *
 * @return		destination integer number with base number specified on success. Or else, 0 will be returned and
 *				error info will be store err.
 */
int strict_strtol(const char *str, int base, std::string *err);

/**
 * The function converts the initial part of the string in str to a 64-bit float value according to the given base,
 * which must be between 2 and 36 inclusive, or be the special value 0.
 *
 * The string may begin with an arbitrary amount of white space (as determined by isspace(3)) followed by a single
 * optional '+' or '-' sign.  If  base is  zero  or 16, the string may then include a "0x" prefix, and the number
 * will be read in base 16; otherwise, a zero base is taken as 10 (decimal) unless the next character is '0', in
 * which case it is taken as 8 (octal).
 * The remainder of the string is converted to a long int value in the obvious manner, stopping at the first
 * character which is not a valid digit in the given base. (In bases above 10, the letter 'A' in either upper or
 * lower case represents 10, 'B' represents 11, and so forth, with 'Z' repre-senting 35.)
 *
 * If this conversion is successful, integer expected will be returned, otherwise, 0 will be returned and error info
 * will be store err.
 *
 * @param[in]		str			string to parse
 * @param[in,out]	err			error information
 *
 * @return		destination integer number with base number specified on success. Or else, 0 will be returned and
 *				error info will be store err.
 */
double strict_strtod(const char *str, std::string *err);

/**
 * The function converts the initial part of the string in str to a 32-bit float value according to the given base,
 * which must be between 2 and 36 inclusive, or be the special value 0.
 *
 * The string may begin with an arbitrary amount of white space (as determined by isspace(3)) followed by a single
 * optional '+' or '-' sign.  If  base is  zero  or 16, the string may then include a "0x" prefix, and the number
 * will be read in base 16; otherwise, a zero base is taken as 10 (decimal) unless the next character is '0', in
 * which case it is taken as 8 (octal).
 * The remainder of the string is converted to a long int value in the obvious manner, stopping at the first
 * character which is not a valid digit in the given base. (In bases above 10, the letter 'A' in either upper or
 * lower case represents 10, 'B' represents 11, and so forth, with 'Z' repre-senting 35.)
 *
 * If this conversion is successful, integer expected will be returned, otherwise, 0 will be returned and error info
 * will be store err.
 *
 * @param[in]		str			string to parse
 * @param[in,out]	err			error information
 *
 * @return		destination integer number with base number specified on success. Or else, 0 will be returned and
 *				error info will be store err.
 */
float strict_strtof(const char *str, std::string *err);

#endif

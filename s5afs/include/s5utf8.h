#ifndef __S5_UTF8_H
#define __S5_UTF8_H

#ifdef __cplusplus
extern "C" {
#endif

/* Checks if a buffer is valid UTF-8.
 * Returns 0 if it is, and one plus the offset of the first invalid byte
 * if it is not.
 */
int check_utf8(const char *buf, int len);

/* Checks if a null-terminated string is valid UTF-8.
 * Returns 0 if it is, and one plus the offset of the first invalid byte
 * if it is not.
 */
int check_utf8_cstr(const char *buf);

/* Returns true if 'ch' is a control character.
 * We do count newline as a control character, but not NULL.
 */
int is_control_character(int ch);

/* Checks if a buffer contains control characters.
 */
int check_for_control_characters(const char *buf, int len);

/* Checks if a null-terminated string contains control characters.
 */
int check_for_control_characters_cstr(const char *buf);

#ifdef __cplusplus
}
#endif

#endif

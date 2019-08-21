

#ifndef _CMDOPT_H
#define _CMDOPT_H
#ifdef __cplusplus
extern "C" {
#endif



/**
 * initialize the option file library
 *
 * An argument in command line start with '-' or '--' consider as parameter name,
 * the argument follow it immediately is consider as value for that. For example,
 * '-bs 4096' or '--bs 4096', the parameter name is "bs", value is "4096". While
 * the following is illegal 'bs 4096', '---bs 4096', or '-bs -4096'.
 *
 * If a parameter has no value attached, then it is a flag parameter. The program
 * only instreated in whether it has presented or not.
 * <B> For a flag parameter, the client code should use opt_flagValue() to get
 * it's value but not the opt_value()</B>.
 *
 * opt_next() may return NULL if a command line argument is not a named parameter.
 *
 * @par Example
 * @code
 * int main(int argc , const char** argv)
 * {
 *	int id = NULL;
 *	const char* objName = NULL;
 *	const char* conf = NULL;
 *	opt_initialize(argc, argv);
 *	while(opt_error_code()==0 && opt_has_next())
 *	{
 *		const char* name = opt_next();
 *		if( name == NULL)
 *		{
 *			objName = opt_value();
 *		}
 *		else if(strcmp(name, "id") == 0)
 *		{
 *			id = opt_value_as_int();
 *		}
 *		else if(strcmp(name, "c") == 0)
 *		{
 *			conf = opt_value();
 *		}
 *		else if(strcmp(name, "h") == 0)
 *		{
 *			printUsage();
 *			opt_uninitialize();
 *			exit(1);
 *		}
 *		else
 *		{
 *			fprintf(stderr, "Invalid parameter '%s' \n", name);
 *			printUsage();
 *			opt_uninitialize();
 *			exit(1);
 *		}
 *	}
 *	if(opt_error_code() != 0)
 *	{
 *		fprintf(stderr, "%s", opt_errorMessage());
 *		printUsage();
 *		opt_uninitialize();
 *		exit(1);
 *	}
 *	opt_uninitialize();
 * @endcode
 * @return 0 for successful
 *         other for failed
 * @note	If the same parameter present both in command line and option file, or present multi-times in
 * 			command line or option file, it will be returned by the iterator as time as it appearance.
 * 			Call opt_uninitializa() after complete use of this library.
 */
int opt_initialize(int argc, const char** argv);

/**
 * Call this function after complete command line and option file read. This function will close
 * all opened files and free all dynamically allocated memorys
 */
void opt_uninitialize();

/*
 * Iterator function to determine whether there's more parameter available.
 * @return 1 if there's more parameter
 *         0 if there's no more
 */
int opt_has_next();

/*
 * This function return the next parameter name that is currently iterated.
 * The option lib use a global buffer to store the parameter name. The buffer
 * will be rewrite each time opt_next() is called. This function
 * return the address of the global buffer.
 * Do not store the returned char* for later use, it's value will change after next
 * call to opt_next(). Instead, copy the content to your own buffer with strcpy().
 *
 * @retrun pointer to a buffer where the parameter name is stored
 *         NULL if a parameter has no name.
 */
const char* opt_next();

/*
 * This function return the string that is currently iterated.
 * The option lib use a global buffer to store the parameter value. The buffer
 * will be rewrite each time opt_value() or opt_valueAsXXX is called. This function
 * return the address of the global buffer.
 * Do not store the returned char* for later use, it's value will change after next
 * call to opt_value(). Instead, copy the content to your own buffer with strcpy().
 *
 * @retrun pointer to a buffer where the parameter value is stored
 *         NULL if a parameter has no value.
 */
const char* opt_value();

/*
 * This function return the value of flag parameter that is currently iterated.
 * @return 1 if a flag parameter presented in command line
 *         0 if a flag parameter not present or present but with value "false" "no" "0".
 */
int opt_flag_value();

/*
 * This function try to convert the string value returned by opt_value() to an
 * integer. If the convert failed, error code will be set to OPT_FAILCONVERT.
 * Client code should check error code after this function call before use
 * the return value.
 */
int opt_value_as_int();

long long opt_value_as_ull();


/*
* This function try to convert the string value returned by opt_value() to a
* double. If the convert failed, error code will be set to OPT_FAILCONVERT.
* Client code should check error code after this function call before use
* the return value.
*/
double opt_value_as_double();

/*
* This function try to convert the string value returned by opt_value() to a
* double. If the convert failed, error code will be set to OPT_FAILCONVERT.
* Client code should check error code after this function call before use
* the return value.
* @ return If the value is not present in command line or option file, return 1
*          If value
*/
int opt_value_as_bool();

/*
 * @return error code of the previous option lib call. An error code a negative
 *            integer value
 *         0 if no error.
 */
int opt_error_code();

/*
 * return the error of previous option lib call in human readable message.
 */
const char* opt_error_message();

#define OPT_TOOMANYARG -1
#define OPT_FAILCONVERT -2
#ifdef __cplusplus
}
#endif
#endif //__OPT_OPTFILE_H

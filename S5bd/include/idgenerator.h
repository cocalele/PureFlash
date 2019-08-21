/**
 * Copyright (C), 2014-2015.
 * @file
 * The file declares the operations for id generator
 */

#ifndef __ID_GENERATOR_H___
#define __ID_GENERATOR_H___

#ifdef __cplusplus
extern "C" {
#endif

#ifndef INVALID_ID
#define INVALID_ID -1
#endif

typedef void *idgenerator;

/**
 * Init id generator.
 *
 * This function is used to init id generator.
 *
 * @param[in]			sz		init sz as id could be alloced.
 * @param[in, out]		idg		idgenerator to be initlized.
 *
 * @return		0 on success, negative error code on failure
 * @retval		0			success
 * @retval		-ENOMEM 	run out of memory.
 */
int init_id_generator(size_t sz, idgenerator *idg);

/**
 * Release id generator.
 *
 * This function is used to release id generator.
 *
 * @param[in, out]		idg		idgenerator to release.
 *
 * @return		0 on success, negative error code on failure
 * @retval		0			success
 * @retval		-EINVAL		invalid argument.
 */
int release_id_generator(idgenerator idg);

/**
 * Alloc id.
 *
 * This function is used to alloc id.
 *
 * @param[in, out]		idg 	idgenerator to alloc.
 *
 * @return		0<=id<=sz on success, INVALID_ID on failure
 * @retval		0<=id<=sz	success
 * @retval		INVALID_ID 	no more id.
 */
int alloc_id(idgenerator idg);

/**
 * Free id.
 *
 * This function is used to free id.
 *
 * @param[in]		idg 	id generator.
 * @param[in]		id 	id to be free.
 */
void free_id(idgenerator idg, int id);


#ifdef __cplusplus
}
#endif

#endif //__ID_GENERATOR_H___

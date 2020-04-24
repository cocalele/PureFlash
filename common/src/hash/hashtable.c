/// @cond PRIVATE
/// @file hashtable.c
/// @copyright BSD 2-clause. See LICENSE.txt for the complete license text
/// @author Dane Larsen

#include "hashtable.h"
#include "hashfunc.h"
#include "murmur.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
uint32_t global_seed = 2976579765;


//----------------------------------
// Debug macro
//----------------------------------

#ifdef DEBUG
#define debug(M, ...) fprintf(stderr, "%s:%d - " M, __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define debug(M, ...)
#endif

//----------------------------------
// HashEntry functions
//----------------------------------

/// @brief Creates a new hash entry.
/// @param flags Hash table flags.
/// @param key A pointer to the key.
/// @param key_size The size of the key in bytes.
/// @param value A pointer to the value.
/// @param value_size The size of the value in bytes.
/// @returns A pointer to the hash entry.
hash_entry *he_create(int flags, void *key, size_t key_size, void *value,
        size_t value_size);

/// @brief Destroys the hash entry and frees all associated memory.
/// @param flags The hash table flags.
/// @param hash_entry A pointer to the hash entry.
void he_destroy(int flags, hash_entry *entry);

/// @brief Compare two hash entries.
/// @param e1 A pointer to the first entry.
/// @param e2 A pointer to the second entry.
/// @returns 1 if both the keys and the values of e1 and e2 match, 0 otherwise.
///          This is a "deep" compare, rather than just comparing pointers.
int he_key_compare(hash_entry *e1, hash_entry *e2);

/// @brief Sets the value on an existing hash entry.
/// @param flags The hashtable flags.
/// @param entry A pointer to the hash entry.
/// @param value A pointer to the new value.
/// @param value_size The size of the new value in bytes.
void he_set_value(int flags, hash_entry *entry, void *value, size_t value_size);

//-----------------------------------
// HashTable functions
//-----------------------------------

int ht_init(hash_table *table, ht_flags flags, double max_load_factor, int size
#ifndef __WITH_MURMUR
        , HashFunc *for_x86_32, HashFunc *for_x86_128, HashFunc *for_x64_128
#endif //__WITH_MURMUR
        )
{
#ifdef __WITH_MURMUR
    table->hashfunc_x86_32  = MurmurHash3_x86_32;
    table->hashfunc_x86_128 = MurmurHash3_x86_128;
    table->hashfunc_x64_128 = MurmurHash3_x64_128;

#else //__WITH_MURMUR
    table->hashfunc_x86_32  = for_x86_32;
    table->hashfunc_x86_128 = for_x86_128;
    table->hashfunc_x64_128 = for_x64_128;

#endif //__WITH_MURMUR

	table->array_size = size;// HT_INITIAL_SIZE;
    table->array        = malloc(table->array_size * sizeof(*(table->array)));

    if(table->array == NULL) {
        debug("ht_init failed to allocate memory\n");
		return -ENOMEM;
    }

    table->key_count            = 0;
    table->collisions           = 0;
    table->flags                = flags;
    table->max_load_factor      = max_load_factor;
    table->current_load_factor  = 0.0;

    unsigned int i;
    for(i = 0; i < table->array_size; i++)
    {
        table->array[i] = NULL;
    }

    hash_lock_init(&table->lock);

    return 0;
}

void ht_destroy(hash_table *table)
{
    unsigned int i;
    hash_entry *entry;
    hash_entry *tmp;

    if(table->array == NULL) {
        debug("ht_destroy got a bad table\n");
    }

    // crawl the entries and delete them
    for(i = 0; i < table->array_size; i++) {
        entry = table->array[i];

        while(entry != NULL) {
            tmp = entry->next;
            he_destroy(table->flags, entry);
            entry = tmp;
        }
    }

    table->hashfunc_x86_32 = NULL;
    table->hashfunc_x86_128 = NULL;
    table->hashfunc_x64_128 = NULL;
    table->array_size = 0;
    table->key_count = 0;
    table->collisions = 0;
    free(table->array);
    table->array = NULL;

    hash_lock_destroy(&table->lock);
}

void ht_insert(hash_table *table, void *key, size_t key_size, void *value,
        size_t value_size)
{
    hash_entry *entry = he_create(table->flags, key, key_size, value,
            value_size);

    ht_insert_he(table, entry);
}

// this was separated out of the regular ht_insert
// for ease of copying hash entries around
void ht_insert_he(hash_table *table, hash_entry *entry){
    hash_entry *tmp;
    unsigned int index;

    entry->next = NULL;
    index = ht_index(table, entry->key, entry->key_size);
    tmp = table->array[index];

    // if true, no collision
    if(tmp == NULL)
    {
        table->array[index] = entry;
        table->key_count++;
        return;
    }

    // walk down the chain until we either hit the end
    // or find an identical key (in which case we replace
    // the value)
    while(tmp->next != NULL)
    {
        if(he_key_compare(tmp, entry))
            break;
        else
            tmp = tmp->next;
    }

    if(he_key_compare(tmp, entry))
    {
        // if the keys are identical, throw away the old entry
        // and stick the new one into the table
        he_set_value(table->flags, tmp, entry->value, entry->value_size);
        he_destroy(table->flags, entry);
    }
    else
    {
        // else tack the new entry onto the end of the chain
        tmp->next = entry;
        table->collisions += 1;
        table->key_count ++;
        table->current_load_factor = (double)table->collisions / table->array_size;

        // double the size of the table if autoresize is on and the
        // load factor has gone too high
        if(!(table->flags & HT_NO_AUTORESIZE) &&
                (table->current_load_factor > table->max_load_factor)) {
            ht_resize(table, table->array_size * 2);
            table->current_load_factor =
                (double)table->collisions / table->array_size;
        }
    }
}

void* ht_get(hash_table *table, void *key, size_t key_size, size_t *value_size)
{
    unsigned int index  = ht_index(table, key, key_size);
    hash_entry *entry   = table->array[index];
    hash_entry tmp;
    tmp.key             = key;
    tmp.key_size        = key_size;

    // once we have the right index, walk down the chain (if any)
    // until we find the right key or hit the end
    while(entry != NULL)
    {
        if(he_key_compare(entry, &tmp))
        {
            if(value_size != NULL)
                *value_size = entry->value_size;

            return entry->value;
        }
        else
        {
            entry = entry->next;
        }
    }

    return NULL;
}

void* ht_remove(hash_table *table, void *key, size_t key_size)
{
    void* value = NULL;
    unsigned int index  = ht_index(table, key, key_size);
    hash_entry *entry   = table->array[index];
    hash_entry *prev    = NULL;
    hash_entry tmp;
    tmp.key             = key;
    tmp.key_size        = key_size;

    // walk down the chain
    while(entry != NULL)
    {
        // if the key matches, take it out and connect its
        // parent and child in its place
        if(he_key_compare(entry, &tmp))
        {
            if(prev == NULL)
                table->array[index] = entry->next;
            else
                prev->next = entry->next;

            table->key_count--;

            if(prev != NULL)
              table->collisions--;

            value = entry->value;
            he_destroy(table->flags, entry);
            return value;
        }
        else
        {
            prev = entry;
            entry = entry->next;
        }
    }
    return NULL;
}

int ht_contains(hash_table *table, void *key, size_t key_size)
{
    unsigned int index  = ht_index(table, key, key_size);
    hash_entry *entry   = table->array[index];
    hash_entry tmp;
    tmp.key             = key;
    tmp.key_size        = key_size;

    // walk down the chain, compare keys
    while(entry != NULL)
    {
        if(he_key_compare(entry, &tmp))
            return 1;
        else
            entry = entry->next;
    }

    return 0;
}

unsigned int ht_size(hash_table *table)
{
    return table->key_count;
}

void** ht_keys(hash_table *table, unsigned int *key_count)
{
    void **ret;

    if(table->key_count == 0){
      *key_count = 0;
      return NULL;
    }

    // array of pointers to keys
    ret = malloc(table->key_count * sizeof(void *));
    if(ret == NULL) {
        debug("ht_keys failed to allocate memory\n");
    }
    *key_count = 0;

    unsigned int i;
    hash_entry *tmp;

    // loop over all of the chains, walk the chains,
    // add each entry to the array of keys
    for(i = 0; i < table->array_size; i++)
    {
        tmp = table->array[i];

        while(tmp != NULL)
        {
            ret[*key_count]=tmp->key;
            *key_count += 1;
            tmp = tmp->next;
            // sanity check, should never actually happen
            if(*key_count >= table->key_count) {
                debug("ht_keys: too many keys, expected %d, got %d\n",
                        table->key_count, *key_count);
            }
        }
    }

    return ret;
}

void ht_clear(hash_table *table)
{
    ht_destroy(table);

    ht_init(table, table->flags, table->max_load_factor, HT_INITIAL_SIZE
#ifndef __WITH_MURMUR
    , table->hashfunc_x86_32, table->hashfunc_x86_128, table->hashfunc_x64_128
#endif //__WITH_MURMUR
    );
}

unsigned int ht_index(hash_table *table, void *key, size_t key_size)
{
    uint32_t index;
    // 32 bits of murmur seems to fare pretty well
    table->hashfunc_x86_32(key, key_size, global_seed, &index);
    index %= table->array_size;
    return index;
}

// new_size can be smaller than current size (downsizing allowed)
void ht_resize(hash_table *table, unsigned int new_size)
{
    hash_table new_table;

    debug("ht_resize(old=%d, new=%d)\n",table->array_size,new_size);
    new_table.hashfunc_x86_32 = table->hashfunc_x86_32;
    new_table.hashfunc_x86_128 = table->hashfunc_x86_128;
    new_table.hashfunc_x64_128 = table->hashfunc_x64_128;
    new_table.array_size = new_size;
    new_table.array = malloc(new_size * sizeof(hash_entry*));
    new_table.key_count = 0;
    new_table.collisions = 0;
    new_table.flags = table->flags;
    new_table.max_load_factor = table->max_load_factor;

    unsigned int i;
    for(i = 0; i < new_table.array_size; i++)
    {
        new_table.array[i] = NULL;
    }

    hash_entry *entry;
    hash_entry *next;
    for(i = 0; i < table->array_size; i++)
    {
        entry = table->array[i];
        while(entry != NULL)
        {
            next = entry->next;
            ht_insert_he(&new_table, entry);
            entry = next;
        }
        table->array[i]=NULL;
    }

    ht_destroy(table);

    table->hashfunc_x86_32 = new_table.hashfunc_x86_32;
    table->hashfunc_x86_128 = new_table.hashfunc_x86_128;
    table->hashfunc_x64_128 = new_table.hashfunc_x64_128;
    table->array_size = new_table.array_size;
    table->array = new_table.array;
    table->key_count = new_table.key_count;
    table->collisions = new_table.collisions;

}

void ht_set_seed(uint32_t seed){
    global_seed = seed;
}

//---------------------------------
// hash_entry functions
//---------------------------------

hash_entry *he_create(int flags, void *key, size_t key_size, void *value,
        size_t value_size)
{
    hash_entry *entry = malloc(sizeof(*entry));
    if(entry == NULL) {
        debug("Failed to create hash_entry\n");
        return NULL;
    }

    entry->key_size = key_size;
    if (flags & HT_KEY_CONST){
        entry->key = key;
    }
    else {
        entry->key = malloc(key_size);
        if(entry->key == NULL) {
            debug("Failed to create hash_entry\n");
            free(entry);
            return NULL;
        }
        memcpy(entry->key, key, key_size);
    }

    entry->value_size = value_size;
    if (flags & HT_VALUE_CONST){
        entry->value = value;
    }
    else {
        entry->value = malloc(value_size);
        if(entry->value == NULL) {
            debug("Failed to create hash_entry\n");
            free(entry->key);
            free(entry);
            return NULL;
        }
        memcpy(entry->value, value, value_size);
    }

    entry->next = NULL;

    return entry;
}

void he_destroy(int flags, hash_entry *entry)
{
    if (!(flags & HT_KEY_CONST))
        free(entry->key);
    if (!(flags & HT_VALUE_CONST))
        free(entry->value);
    free(entry);
}

int he_key_compare(hash_entry *e1, hash_entry *e2)
{
    char *k1 = e1->key;
    char *k2 = e2->key;

    if(e1->key_size != e2->key_size)
        return 0;

    return (memcmp(k1,k2,e1->key_size) == 0);
}

void he_set_value(int flags, hash_entry *entry, void *value, size_t value_size)
{
    if (!(flags & HT_VALUE_CONST)) {
        if(entry->value)
            free(entry->value);

        entry->value = malloc(value_size);
        if(entry->value == NULL) {
            debug("Failed to set entry value\n");
            return;
        }
        memcpy(entry->value, value, value_size);
    } else {
        entry->value = value;
    }
    entry->value_size = value_size;

    return;
}

/**
 * create a value iterator, to iterate all hash value entry
 */
struct hash_table_value_iterator* ht_create_value_iterator(hash_table *table)
{
	struct hash_table_value_iterator *it = (struct hash_table_value_iterator*)malloc(sizeof(struct hash_table_value_iterator));
	it->table = table;
	it->index = 0;
	it->cur_entry = NULL;
	return it;
}

void ht_destroy_value_iterator(struct hash_table_value_iterator* it)
{
	free(it);
}

hash_entry* ht_next(struct hash_table_value_iterator* it)
{
	while (1)
	{
		if (it->cur_entry == NULL)
		{
			while (it->table->array[it->index] == NULL)
			{
				it->index++;
				if (it->index >= it->table->array_size)
					return NULL;
			}
			it->cur_entry = it->table->array[it->index];
			return it->cur_entry;
		}
		it->cur_entry = it->cur_entry->next;
		if (it->cur_entry)
			return it->cur_entry;
	}
}



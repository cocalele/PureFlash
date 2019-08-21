/// @file main.c
/// @copyright BSD 2-clause. See LICENSE.txt for the complete license text.
/// @author Dane Larsen
/// @brief The main function is used for testing the hashtable library,
///        and also provides example code.

#include "hashtable.h"
#include "test.h"
#include "timer.h"

#include <string.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    (void) argc;
    (void) argv;

    hash_table ht;
    ht_init(&ht, HT_KEY_CONST | HT_VALUE_CONST, 0.05, HT_INITIAL_SIZE);

    char *s1 = (char*)"teststring 1";
    char *s2 = (char*)"teststring 2";
    char *s3 = (char*)"teststring 3";

    ht_insert(&ht, s1, strlen(s1)+1, s2, strlen(s2)+1);

    int contains = ht_contains(&ht, s1, strlen(s1)+1);
    test(contains, "Checking for key \"%s\"", s1);

    size_t value_size;
    char *got = ht_get(&ht, s1, strlen(s1)+1, &value_size);

    fprintf(stderr, "Value size: %zu\n", value_size);
    fprintf(stderr, "Got: {\"%s\": \"%s\"}\n", s1, got);

    test(value_size == strlen(s2)+1,
            "Value size was %zu (desired %lu)",
            value_size, strlen(s2)+1);

    fprintf(stderr, "Replacing {\"%s\": \"%s\"} with {\"%s\": \"%s\"}\n", s1, s2, s1, s3);
    ht_insert(&ht, s1, strlen(s1)+1, s3, strlen(s3)+1);

    unsigned int num_keys;
    void **keys;

    keys = ht_keys(&ht, &num_keys);
    test(num_keys == 1, "HashTable has %d keys", num_keys);
    test(keys != NULL, "Keys is not null");
    if(keys)
      free(keys);
    got = ht_get(&ht, s1, strlen(s1)+1, &value_size);

    fprintf(stderr, "Value size: %zu\n", value_size);
    fprintf(stderr, "Got: {\"%s\": \"%s\"}\n", s1, got);

    test(value_size == strlen(s3)+1,
            "Value size was %zu (desired %lu)",
            value_size, strlen(s3)+1);

    fprintf(stderr, "Removing entry with key \"%s\"\n", s1);
    ht_remove(&ht, s1, strlen(s1)+1);

    contains = ht_contains(&ht, s1, strlen(s1)+1);
    test(!contains, "Checking for removal of key \"%s\"", s1);

    keys = ht_keys(&ht, &num_keys);
    test(num_keys == 0, "HashTable has %d keys", num_keys);
    if(keys)
      free(keys);

    fprintf(stderr, "Stress test");
    int key_count = 1000000;
    int i;
    int *many_keys = malloc(key_count * sizeof(*many_keys));
    int *many_values = malloc(key_count * sizeof(*many_values));

    srand(time(NULL));

    for(i = 0; i < key_count; i++)
    {
        many_keys[i] = i;
        many_values[i] = rand();
    }

    struct timespec t1;
    struct timespec t2;

    t1 = snap_time();

    for(i = 0; i < key_count; i++)
    {
        ht_insert(&ht, &(many_keys[i]), sizeof(many_keys[i]), &(many_values[i]), sizeof(many_values[i]));
    }

    t2 = snap_time();

    fprintf(stderr, "Inserting %d keys took %.2f seconds\n", key_count, get_elapsed(t1, t2));
    fprintf(stderr, "Checking inserted keys\n");

    int ok_flag = 1;
    for(i = 0; i < key_count; i++)
    {
        if(ht_contains(&ht, &(many_keys[i]), sizeof(many_keys[i])))
        {
            size_t value_size;
            int value;

            value = *(int*)ht_get(&ht, &(many_keys[i]), sizeof(many_keys[i]), &value_size);

            if(value != many_values[i])
            {
                fprintf(stderr, "Key value mismatch. Got {%d: %d} expected: {%d: %d}\n",
                        many_keys[i], value, many_keys[i], many_values[i]);
                ok_flag = 0;
                break;
            }
        }
        else
        {
            fprintf(stderr, "Missing key-value pair {%d: %d}\n", many_keys[i], many_values[i]);
            ok_flag = 0;
            break;
        }
    }


    test(ok_flag == 1, "Result was %d", ok_flag);
    ht_clear(&ht);
    ht_resize(&ht, 4194304);
    t1 = snap_time();

    for(i = 0; i < key_count; i++)
    {
        ht_insert(&ht, &(many_keys[i]), sizeof(many_keys[i]), &(many_values[i]), sizeof(many_values[i]));
    }

    t2 = snap_time();

    fprintf(stderr, "Inserting %d keys (on preallocated table) took %.2f seconds\n", key_count, get_elapsed(t1, t2));
    for(i = 0; i < key_count; i++)
    {
        ht_remove(&ht, &(many_keys[i]), sizeof(many_keys[i]));
    }
    test(ht_size(&ht) == 0, "%d keys remaining", ht_size(&ht));
    ht_destroy(&ht);
    free(many_keys);
    free(many_values);

    return report_results();
}


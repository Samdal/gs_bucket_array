#include <gs/gs.h>

// comment out this line to disable bit field
//#define GS_BUCKET_ARRAY_BIT_FIELD


#define GS_BUCKET_ARRAY_INVALID_HANDLE    UINT32_MAX

#define __gs_bucket_array_raw(__T)                      \
        struct                                          \
        {                                               \
                uint32_t bucket_size;                   \
                uint32_t top;                           \
                gs_dyn_array(int64_t) bit_field;        \
                gs_dyn_array(__T*) data;                \
                __T tmp;                                \
        }

#define gs_bucket_array(__T)\
        __gs_bucket_array_raw(__T)*

#define gs_bucket_array_new(__T, __AMOUNT)\
        __gs_bucket_array_new(__AMOUNT, sizeof(__gs_bucket_array_raw(__T)))

gs_force_inline void*
__gs_bucket_array_new(uint32_t amount, size_t struct_size)
{
        void* ret = malloc(struct_size);
        memset(ret, 0, struct_size);
        uint32_t* i = ret;
        i[0] = amount;
        return ret;
}

#define gs_bucket_array_bucket_size(__BA) ((__BA)->bucket_size)
#define gs_bucket_array_bucket_count(__BA) (gs_dyn_array_size((__BA)->data))
#define gs_bucket_array_capacity(__BA) ((__BA)->bucket_size * gs_dyn_array_size((__BA)->data))
#define gs_bucket_array_size(__BA) ((__BA)->top)

#define gs_bucket_array_get(__BA, __HNDL)\
        (__BA)->data[(__HNDL) / (__BA)->bucket_size][(__HNDL) - (__HNDL) / (__BA)->bucket_size * (__BA)->bucket_size]

#define gs_bucket_array_getp(__BA, __HNDL)  &gs_bucket_array_get(__BA, __HNDL)

#define gs_bucket_array_push(__BA, __VAL)\
        ((__BA)->tmp = __VAL,                                           \
         __gs_bucket_array_push_func((void***)&((__BA)->data), (__BA)->bucket_size, \
                                    &(__BA)->tmp, sizeof((__BA)->tmp), &(__BA)->top))

#define gs_bucket_array_reserve(__BA, __NUM_BUCKETS)\
         (__gs_bucket_array_reserve_func((void***)&((__BA)->data), (__BA)->bucket_size, sizeof((__BA)->tmp), __NUM_BUCKETS))

gs_force_inline void
__gs_bucket_array_reserve_func(void*** dynarr, uint32_t bucket_size, uint32_t element_size, uint32_t num_buckets)
{
        while (num_buckets--) {
                void* ptr = malloc(element_size * bucket_size);
                gs_dyn_array_push_data((void**)dynarr, &ptr, sizeof(ptr));
        }
}

gs_force_inline void
__gs_bucket_array_push_func(void*** dynarr, uint32_t bucket_size,
                            void* element, size_t element_size, uint32_t* top)
{
        uint32_t sz = gs_dyn_array_size(*dynarr);
        void* ptr;
        if (!sz || *top >= bucket_size * sz) {
                ptr = malloc(element_size * bucket_size);
                gs_dyn_array_push_data((void**)dynarr, &ptr, sizeof(ptr));
        } else {
                const uint32_t major = *top / bucket_size;
                ptr = (char*)dynarr[0][major] + ((*top - major * bucket_size) * element_size);
        }
        *top += 1;
        memcpy(ptr, element, element_size);
}



#ifdef GS_BUCKET_ARRAY_BIT_FIELD
// optional bit field functions

#define gs_bucket_array_bf_exists(__BA, __HNDL)\
        (__HNDL / (__BA)->bucket_size > gs_dyn_array_size((__BA)->data) ? false : \
         __gs_bucket_array_check((__BA)->bit_field, __HNDL) ?           \
         true :  false)

#define gs_bucket_array_bf_insert(__BA, __VAL)\
        ((__BA)->tmp = __VAL,                                           \
         __gs_bucket_array_insert_into_any_bucket((void***)&((__BA)->data), &(__BA)->bit_field, (__BA)->bucket_size, \
                                                  &(__BA)->tmp, sizeof((__BA)->tmp), &(__BA)->top))

#define gs_bucket_array_bf_erase(__BA, __HNDL)\
         __gs_bucket_array_clear_bucket((__BA)->bit_field, __HNDL)

gs_force_inline bool32_t
__gs_bucket_array_check(int64_t* bit_field, uint32_t index)
{
        uint32_t bit_field_index = index / 64;
        uint32_t bit = index - bit_field_index * 64;

        return bit_field[bit_field_index] & (1L << bit) ? true : false;
}

gs_force_inline void
__gs_bucket_array_clear_bucket(int64_t* bit_field, uint32_t index)
{
        uint32_t bit_field_index = index / 64;
        uint32_t bit = index - bit_field_index * 64;

        bit_field[bit_field_index] &= ~(1L << bit);
}

gs_force_inline uint32_t
__gs_bucket_array_insert_into_any_bucket(void*** dynarr, int64_t** bit_field_dynarr, uint32_t bucket_size,
                                         void* element, size_t element_size, uint32_t* top)
{
        uint32_t index;
        uint32_t bit_sz = gs_dyn_array_size(*bit_field_dynarr);
        uint32_t sz = gs_dyn_array_size(*dynarr);

        if (bit_sz) {
                for (uint32_t i = bit_sz - 1; i != UINT32_MAX; i--) {
                        int pos = ffsll(~bit_field_dynarr[0][i]);
                        if (pos) {
                                index = pos - 1 + i * 64;
                                if (index >= sz * bucket_size) continue;

                                const uint32_t bucket_index = index - index / bucket_size * bucket_size;
                                void* p = (char*)dynarr[0][index / bucket_size] + bucket_index * element_size;
                                memcpy(p, element, element_size);
                                bit_field_dynarr[0][i] |= 1L << (pos - 1);
                                return index;
                        }
                }
        }

        void* tmp_ptr = malloc(element_size * bucket_size);
        memcpy(tmp_ptr, element, element_size);
        gs_dyn_array_push_data((void**)dynarr, &tmp_ptr, sizeof(tmp_ptr));

        index = bucket_size * sz;
        uint32_t last_field_end = index + ((index % 64) ? (64 - index % 64) : 0);

        uint32_t index_end = index + bucket_size;
        *top = index_end;
        uint32_t new_index_field_end = index_end + ((index_end % 64) ? (64 - index_end % 64) : 0);

        uint32_t new_bit_size = (new_index_field_end - last_field_end) / 64;
        int64_t tmp = 0;
        for (uint32_t i = 0; i < new_bit_size; i++)
                gs_dyn_array_push(*bit_field_dynarr, tmp);

        bit_field_dynarr[0][index / 64] |= 1L << (index - index / 64 * 64);

        return sz * bucket_size;
}

#define gs_bucket_array_bf_reserve(__BA, __NUM_BUCKETS)\
        (__gs_bucket_array_bf_reserve_func((void***)&((__BA)->data), (__BA)->bucket_size, sizeof((__BA)->tmp),\
                                           __NUM_BUCKETS, &(__BA)->bit_field, &(__BA)->top))

gs_force_inline void
__gs_bucket_array_bf_reserve_func(void*** dynarr, uint32_t bucket_size, uint32_t element_size,
                                  uint32_t num_buckets, int64_t** bit_field_dynarr, uint32_t* top)
{
        uint32_t sz = gs_dyn_array_size(*dynarr);

        __gs_bucket_array_reserve_func(dynarr, bucket_size, element_size, num_buckets);

        uint32_t index = bucket_size * sz;
        uint32_t last_field_end = index + ((index % 64) ? (64 - index % 64) : 0);

        uint32_t index_end = index + bucket_size * num_buckets;
        *top = index_end;
        uint32_t new_index_field_end = index_end + ((index_end % 64) ? (64 - index_end % 64) : 0);

        uint32_t new_bit_size = (new_index_field_end - last_field_end) / 64;
        int64_t tmp = 0;
        for (uint32_t i = 0; i < new_bit_size; i++)
                gs_dyn_array_push(*bit_field_dynarr, tmp);
}

// bit field iter
// the iter is a hndl into the bit field / array
#define gs_bucket_array_iter_bf uint32_t
#define gs_bucket_array_iter_bf_new(__BA) __gs_bucket_array_iter_bf_new_func((__BA)->bit_field, (__BA)->bucket_size * gs_bucket_array_bucket_count(__BA))
#define gs_bucket_array_iter_bf_valid(__BA, __IT) (__IT != GS_BUCKET_ARRAY_INVALID_HANDLE)
#define gs_bucket_array_iter_bf_advance(__BA, __IT) __gs_bucket_array_bf_advance_func((__BA)->bit_field, &__IT, (__BA)->bucket_size * gs_bucket_array_bucket_count(__BA))

gs_force_inline uint32_t
__gs_bucket_array_iter_bf_new_func(int64_t* bit_field, uint32_t max_index)
{
        uint32_t res = 1;
        uint32_t idx = 0;
        do {
                if (!res) idx++;
                if (idx >= max_index) {
                        idx = GS_BUCKET_ARRAY_INVALID_HANDLE;
                        break;
                }
                res = __gs_bucket_array_check(bit_field, idx);
        } while (!res);
        return idx;
}

gs_force_inline void
__gs_bucket_array_bf_advance_func(int64_t* bit_field, uint32_t* index, uint32_t max_index)
{
        uint32_t res = 0;
        do {
                *index += 1;
                if (*index >= max_index) {
                        *index = GS_BUCKET_ARRAY_INVALID_HANDLE;
                        return;
                }
                res = __gs_bucket_array_check(bit_field, *index);
        } while (!res);
}

#endif // GS_BUCKET_ARRAY_BIT_FIELD


// normal iter
typedef struct gs_bucket_array_iter_s {
        uint32_t major;
        uint32_t minor;
} gs_bucket_array_iter;

#define gs_bucket_array_iter_new(__BA) gs_default_val()
#define gs_bucket_array_iter_valid(__BA, __IT) (__IT.major * (__BA)->bucket_size + __IT.minor < (__BA)->top)
#define gs_bucket_array_iter_advance(__BA, __IT) __gs_bucket_array_advance_func(&__IT, (__BA)->bucket_size)

gs_force_inline void
__gs_bucket_array_advance_func(gs_bucket_array_iter* it, uint32_t bucket_size)
{
        it->minor += 1;
        if (it->minor >= bucket_size) {
                it->minor = 0;
                it->major += 1;
        }
}
#define gs_bucket_array_iter_get(__BA, __IT)\
        (__BA)->data[__IT.major][__IT.minor]

#define gs_bucket_array_iter_getp(__BA, __IT)\
        &gs_bucket_array_iter_get(__BA, __IT)

#define gs_bucket_array_iter_get_hndl(__BA, __IT)\
        (__IT.major * (__BA)->bucket_size + __IT.minor)


// clear and free
#define gs_bucket_array_clear(__BA)\
        memset((__BA)->bit_field, 0, gs_dyn_array_size((__BA)->bit_field) * sizeof(int64_t));


#define gs_bucket_array_free(__BA)\
        do {                                                            \
                if (__BA) {                                             \
                        if ((__BA)->bit_field)                          \
                                gs_dyn_array_free((__BA)->bit_field);   \
                        for (uint32_t __BA_IT = 0; __BA_IT < gs_dyn_array_size((__BA)->data); __BA_IT++) \
                                gs_free((__BA)->data[__BA_IT]);         \
                        gs_dyn_array_free((__BA)->data);                \
                        gs_free(__BA);                                  \
                        __BA = NULL;                                    \
                }                                                       \
        } while (0)


/*

        gs_bucket_array:

            NOTE:
                > Unlike the other gunslinger containers, the gs_bucket_array needs initialization.
                > It is NOT allocated on use.

            Bucket arrays are internally a list of pointers to fixed-size arrays;
            This means that there are no realloc's, and all your pointers will remain valid.

            Due to the nature of this container it's very handy for managing both stuff that are
            "constant" and dynamic in a singular place.
            The major drawback of this is slower insertion and iteration.

            The guts look somewhat like:

                gs_dyn_array(Type[bucket_size]) your_data;

            Standard initializatoin would look like:

                gs_bucket_array(float) arr = gs_bucket_array_new(float, 100);   // Bucket array with internal 'float' data, where each bucket is 100 floats
                gs_bucket_array_push(arr, 3.145f);                              // Pushes 3.145 into the array
                float* val_p = gs_bucket_array_getp(arr, index);
                float val = gs_bucket_array_get(arr, index);

             The bucket array provides iterators:

                for (
                    gs_bucket_array_iter it = gs_bucket_array_iter_new(ba);
                    gs_bucket_array_iter_valid(ba, it);
                    gs_bucket_array_iter_advance(ba, it)
                ) {
                    float v = gs_bucket_array_iter_get(ba, it);         // Get value using iterator
                    float* vp = gs_bucket_array_iter_getp(ba, it);      // Get value pointer using iterator
                }

             Internally the iterator is just   struct {uint32_t major; uint32_t minor;}

             Bucket array usage:

                gs_bucket_array(float) ba = gs_bucket_array_new(float, 100);    // Bucket array with internal 'float' data, where each bucket is 100 floats
                gs_bucket_array_reserve(ba, 2);                                 // Reserves 2 buckets
                gs_bucket_array_push(ba, 3.145f);                               // Pushes 3.145 onto the end of the bucket array
                float* val_p = gs_bucket_array_getp(ba, index);                 // Returns a pointer to your data
                float val = gs_bucket_array_get(ba, index);                     // Dereferences your data as well

                uint32_t bs = gs_bucket_array_bucket_size(ba)                   // Returns initialized bucket size
                uint32_t bc = gs_bucket_array_bucket_count(ba)                  // Returns the amount of buckets allocated
                uint32_t ba_cap = gs_bucket_array_capacity(ba)                  // Returns bucket_size * bucket_count
                uint32_t ba_size = gs_bucket_array_size(ba)                     // returns index+1 of the last element
                gs_bucket_array_free(ba)                                        // Free's the entire array, make sure your own elements are free'd first


            Additionally, you may choose to use the provided bit field.
            This allows usage of discarded slots, and iteration through just the valid elements.
            The bit field uses compiler specific intrinsics (ffsll), and is not portable.

            In the case where you use the bit field, you must NEVER use the following functions:

                gs_bucket_array_push();
                gs_bucket_array_reserve();

            Instead use:

                gs_bucket_array_bf_insert();
                gs_bucket_array_bf_reserve();

             Bucket array with bit field usage:

                gs_bucket_array(float) ba = gs_bucket_array_new(float, 100);    // Bucket array with internal 'float' data, where each bucket is 100 floats
                gs_bucket_array_bf_reserve(ba, 2);                              // Reserves 2 buckets
                uitn32_t index = gs_bucket_array_bf_insert(ba, 3.145f);         // Inserts 3.145 to some new or discarded place in ba
                float* val_p = gs_bucket_array_getp(ba, index);                 // Returns a pointer to your data
                float val = gs_bucket_array_get(ba, index);                     // Dereferences your data as well

                uint32_t bs = gs_bucket_array_bucket_size(ba)                   // Returns initialized bucket size
                uint32_t bc = gs_bucket_array_bucket_count(ba)                  // Returns the amount of buckets allocated
                uint32_t ba_cap = gs_bucket_array_capacity(ba)                  // Returns bucket_size * bucket_count
                uint32_t ba_size = gs_bucket_array_size(ba)                     // returns the same as capacity
                gs_bucket_array_clear(ba)                                       // invalidates all existing elements
                gs_bucket_array_free(ba)                                        // Free's the entire array, make sure your own elements are free'd first

             The bucket array with bit fields also has iterators:

                for (
                    gs_bucket_array_bf_iter it = gs_bucket_array_iter_bf_new(ba);
                    gs_bucket_array_bf_iter_valid(ba, it);
                    gs_bucket_array_bf_iter_advance(ba, it)
                ) {
                    float v = gs_bucket_array_get(ba, it);         // Get value using iterator
                    float* vp = gs_bucket_array_getp(ba, it);      // Get value pointer using iterator
                }

*/


int main()
{
        gs_bucket_array(int) foo = gs_bucket_array_new(int, 99);
        gs_println("foo has a bucket size of %d", gs_bucket_array_bucket_size(foo));

#ifdef GS_BUCKET_ARRAY_BIT_FIELD
        gs_bucket_array_bf_reserve(foo, 2);
#else // ! GS_BUCKET_ARRAY_BIT_FIELD
        gs_bucket_array_reserve(foo, 2);
#endif // GS_BUCKET_ARRAY_BIT_FIELD

        uint32_t element_inserts = 400;
        gs_println("---- inserting %d elements", element_inserts);
        for (int i = 0; i < element_inserts; i++) {
#ifdef GS_BUCKET_ARRAY_BIT_FIELD
                gs_println("got hndl %3d for %3d : bucket count %2d", gs_bucket_array_bf_insert(foo, i), i, gs_bucket_array_bucket_count(foo));
#else // ! GS_BUCKET_ARRAY_BIT_FIELD
                gs_bucket_array_push(foo, i + 1);
                gs_println("%3d has %3d : bucket count %2d", i, gs_bucket_array_get(foo, i), gs_bucket_array_bucket_count(foo));
#endif // GS_BUCKET_ARRAY_BIT_FIELD
        }

        const int start = element_inserts + 5 - 20;
        const int end = start + 20;



#ifdef GS_BUCKET_ARRAY_BIT_FIELD
        for (int i = start + 5; i < start + 10; i++)
                gs_bucket_array_bf_erase(foo, i);

        gs_println("---- checking the elements %d -> %d", start, end);
        for (int i = start; i < end; i++) {
                if (gs_bucket_array_bf_exists(foo, i)) {
                        gs_println("element %d: exists and has value %d", i, gs_bucket_array_get(foo, i));
                } else {
                        gs_println("element %d: does not exist", i);
                }
        }

        for (int i = 2; i < 100; i++)
                gs_bucket_array_bf_erase(foo, i);


        int loops = 0;
        for (gs_bucket_array_iter_bf it = gs_bucket_array_iter_bf_new(foo);
             gs_bucket_array_iter_bf_valid(foo, it);
             gs_bucket_array_iter_bf_advance(foo, it)
        ) {
                int test = gs_bucket_array_get(foo, it);
                int* testp = gs_bucket_array_getp(foo, it);
                loops++;
                if (test == 250) {
                        gs_println("element with value 250 found after %d loops, iterator is now %d", loops, it);
                        break;
                }
        }

        gs_bucket_array_clear(foo);

        for (gs_bucket_array_iter_bf it = gs_bucket_array_iter_bf_new(foo);
             gs_bucket_array_iter_bf_valid(foo, it);
             gs_bucket_array_iter_bf_advance(foo, it)
        ) {
                // will never print because of previous clear.
                gs_println("element %d:%d", it, gs_bucket_array_get(foo, it));
        }
#endif // GS_BUCKET_ARRAY_BIT_FIELD



        for (gs_bucket_array_iter it = gs_bucket_array_iter_new(foo);
             gs_bucket_array_iter_valid(foo, it);
             gs_bucket_array_iter_advance(foo, it)
        ) {
                int i = gs_bucket_array_iter_get(foo, it);
                uint32_t hndl = gs_bucket_array_iter_get_hndl(foo, it);
                gs_println("element %d:%d", hndl, i);
        }

        gs_bucket_array_free(foo);

        return 0;
}

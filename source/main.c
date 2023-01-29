#include <gs/gs.h>

#define GS_BUCKET_ARRAY_INVALID_HANDLE    UINT32_MAX

#define __gs_bucket_array_raw(__T)                      \
        struct                                          \
        {                                               \
                uint32_t bucket_size;                   \
                uint32_t null_buckets;                  \
                gs_dyn_array(int64_t) bit_field;        \
                gs_dyn_array(__T*) data;                \
                __T tmp;                                \
        }
#define gs_bucket_array(__T)\
        __gs_bucket_array_raw(__T)*

#define gs_bucket_array_new_ex(__T, __AMOUNT, __NULL_NEW_BUCKETS)\
        __gs_bucket_array_new(__AMOUNT, sizeof(__gs_bucket_array_raw(__T)), __NULL_NEW_BUCKETS)

#define gs_bucket_array_new(__T, __AMOUNT)\
        gs_bucket_array_new_ex(__T, __AMOUNT, 0)

gs_force_inline void*
__gs_bucket_array_new(uint32_t amount, size_t struct_size, uint32_t null_buckets)
{
        void* ret = malloc(struct_size);
        memset(ret, 0, struct_size);
        uint32_t* i = ret;
        i[0] = amount;
        i[1] = null_buckets;
        return ret;
}

#define gs_bucket_array_bucket_size(__BA) ((__BA)->bucket_size)
#define gs_bucket_array_bucket_count(__BA) (gs_dyn_array_size((__BA)->data))
#define gs_bucket_array_capacity(__BA) ((__BA)->bucket_size * gs_dyn_array_size((__BA)->data))

#define gs_bucket_array_exists(__BA, __HNDL)\
        (__HNDL / (__BA)->bucket_size > gs_dyn_array_size((__BA)->data) ? false : \
         __gs_bucket_array_check((__BA)->bit_field, __HNDL) ?           \
         true :  false)

#define gs_bucket_array_get(__BA, __HNDL)\
        (__BA)->data[(__HNDL) / (__BA)->bucket_size][(__HNDL) - (__HNDL) / (__BA)->bucket_size * (__BA)->bucket_size]

#define gs_bucket_array_getp(__BA, __HNDL)  &gs_bucket_array_get(__BA, __HNDL)

#define gs_bucket_array_insert(__BA, __VAL)\
        ((__BA)->tmp = __VAL,                                           \
         __gs_bucket_array_insert_into_any_bucket((void***)&((__BA)->data), &(__BA)->bit_field, (__BA)->bucket_size, \
                                                  &(__BA)->tmp, sizeof((__BA)->tmp), (__BA)->null_buckets))

#define gs_bucket_array_erase(__BA, __HNDL)\
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
__gs_bucket_array_insert_into_any_bucket(void*** dynarr, int64_t** bit_field_dynarr,
                                         uint32_t bucket_size, void* element, size_t element_size, uint32_t null_buckets)
{
        uint32_t index;
        uint32_t bit_sz = gs_dyn_array_size(*bit_field_dynarr);
        uint32_t sz = gs_dyn_array_size(*dynarr);

        for (uint32_t i = 0; i < bit_sz; i++) {
                int pos = ffsll(~bit_field_dynarr[0][i]);
                if (pos) {
                        index = pos - 1 + i * 64;
                        if (index >= sz * bucket_size) break;

                        void* p = (char*)dynarr[0][index / bucket_size] + (index - index / bucket_size * bucket_size) * element_size;
                        memcpy(p, element, element_size);
                        bit_field_dynarr[0][i] |= 1L << (pos - 1);
                        return index;
                }
        }

        void* tmp_ptr = malloc(element_size * bucket_size);
        memcpy(tmp_ptr, element, element_size);
        if (null_buckets) memset((char*)tmp_ptr + element_size, 0, (element_size - 1) * bucket_size);
        gs_dyn_array_push_data((void**)dynarr, &tmp_ptr, sizeof(tmp_ptr));

        index = bucket_size * sz;
        uint32_t last_field_end = index + ((index % 64) ? (64 - index % 64) : 0);

        uint32_t index_end = index + bucket_size;
        uint32_t new_index_field_end = index_end + ((index_end % 64) ? (64 - index_end % 64) : 0);

        uint32_t new_bit_size = (new_index_field_end - last_field_end) / 64;
        int64_t tmp = 0;
        for (uint32_t i = 0; i < new_bit_size; i++)
                gs_dyn_array_push(*bit_field_dynarr, tmp);

        bit_field_dynarr[0][index / 64] |= 1L << (index - index / 64 * 64);

        return sz * bucket_size;
}

// normal iter
#define gs_bucket_array_iter uint32_t
#define gs_bucket_array_iter_new(__BA) __gs_bucket_array_iter_new_func((__BA)->bit_field, (__BA)->bucket_size * gs_bucket_array_bucket_count(__BA))
#define gs_bucket_array_iter_valid(__BA, __IT) (__IT != GS_BUCKET_ARRAY_INVALID_HANDLE)
#define gs_bucket_array_iter_advance(__BA, __IT) __gs_bucket_array_advance_func((__BA)->bit_field, &__IT, (__BA)->bucket_size * gs_bucket_array_bucket_count(__BA))

gs_force_inline uint32_t
__gs_bucket_array_iter_new_func(int64_t* bit_field, uint32_t max_index)
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
__gs_bucket_array_advance_func(int64_t* bit_field, uint32_t* index, uint32_t max_index)
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

// fast iter
typedef struct gs_bucket_array_iter_fast_s {
        uint32_t major;
        uint32_t minor;
} gs_bucket_array_iter_fast;

#define gs_bucket_array_iter_fast_new(__BA) gs_default_val()
#define gs_bucket_array_iter_fast_valid(__BA, __IT) (__IT.major < gs_bucket_array_bucket_count(__BA))
#define gs_bucket_array_iter_fast_advance(__BA, __IT) __gs_bucket_array_fast_advance_func(&__IT, (__BA)->bucket_size)

gs_force_inline void
__gs_bucket_array_fast_advance_func(gs_bucket_array_iter_fast* it, uint32_t bucket_size)
{
        it->minor += 1;
        if (it->minor >= bucket_size) {
                it->minor = 0;
                it->major += 1;
        }
}
#define gs_bucket_array_iter_fast_get(__BA, __IT)\
        (__BA)->data[__IT.major][__IT.minor]

#define gs_bucket_array_iter_fast_getp(__BA, __IT)\
        &gs_bucket_array_iter_fast_get(__BA, __IT)

#define gs_bucket_array_iter_fast_get_hndl(__BA, __IT)\
        (__IT.major * (__BA)->bucket_size + __IT.minor)


// clear and free
#define gs_bucket_array_clear(__BA)\
        memset((__BA)->bit_field, 0, gs_dyn_array_size((__BA)->bit_field) * sizeof(int64_t));

#define gs_bucket_array_free(__BA)\
        do {                                                            \
                if (__BA) {                                             \
                        gs_dyn_array_free((__BA)->bit_field);           \
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

            Because of this the container also has a bit-field that specifies which elements are in use.
            This creates an interface almost identitcal to gs_slot_array.
            The major drawback of this is a somewhat slow iterator and insertion.

            The guts look somewhat like:

                gs_dyn_array(Type[bucket_size]) your_data;
                gs_dyn_array(int64_t)  bit_field;

            Standard initializatoin would look like:

                gs_bucket_array(float) arr = gs_bucket_array_new(float, 100);   // Bucket array with internal 'float' data, where each bucket is 100 floats
                uint32_t hndl = gs_bucket_array_insert(arr, 3.145f);            // Inserts your data into the bucket array, returns handle to you
                float* val_p = gs_bucket_array_getp(arr, hndl);                 // Returns copy of data to you using handle as lookup
                float val = gs_bucket_array_get(arr, hndl);


             The Bucket array provides iterators:

                for (
                    gs_bucket_array_iter it = gs_bucket_array_iter_new(ba);
                    gs_bucket_array_iter_valid(ba, it);
                    gs_bucket_array_iter_advance(ba, it)
                ) {
                    float v = gs_bucket_array_get(ba, it);         // Get value using iterator
                    float* vp = gs_bucket_array_getp(ba, it);      // Get value pointer using iterator
                }

             This iterator gathers a index into the bucket array, which you later can use to get pointers/values
             through the indirection.
             However you might not always need this.
             If you don't care about elements being "valid" you can use the fast iterator:

                for (
                    gs_bucket_array_iter_fast it = gs_bucket_array_iter_fast_new(ba);
                    gs_bucket_array_iter_fast_valid(ba, it);
                    gs_bucket_array_iter_fast_advance(ba, it)
                ) {
                    float v = gs_bucket_array_iter_fast_get(ba, it);                 // Get value using iterator
                    float* vp = gs_bucket_array_iter_fast_getp(ba, it);              // Get value pointer using iterator
                    uint32_t hndl = gs_bucket_array_iter_fast_get_hndl(ba, it);      // Get a normal handle from iterator
                }

             Internally the fast iterator is just   struct {uint32_t major; uint32_t minor;}
             TIP:
                > You may store your own "bool enabled;" in what you store in the bucket array.
                > You can then use the fast iterator and check for this yourself.
                > However, note that this needs gs_bucket_array_new_ex where null_new_buckets is set to true,
                > this is because un-initialized data will otherwise cause problems.


             Bucket Array Usage:

                gs_bucket_array(float) ba = gs_bucket_array_new(float, 100);    // Bucket array with internal 'float' data, where each bucket is 100 floats
                uint32_t hndl = gs_bucket_array_insert(ba, 3.145f);             // Inserts your data into the bucket array, returns handle to you
                float* val_p = gs_bucket_array_getp(ba, hndl);                  // Returns your data to you using handle as lookup
                float val = gs_bucket_array_get(ba, hndl);                      // Dereferences your data as well
                uint32_t bs = gs_bucket_array_bucket_size(ba)                   // Returns initialized bucket size
                uint32_t bc = gs_bucket_array_bucket_count(ba)                  // Returns the amount of buckets allocated
                uint32_t b_cap = gs_bucket_array_capacity(ba)                   // Returns bucket_size * bucket_count
                gs_bucket_array_clear(ba)                                       // Sets the entire bit-field to 0
                gs_bucket_array_free(ba)                                        // Free's the entire array, make sure your own elements are free'd first

                // if you use new_ex, you can set a bool to memset every new bucket to zero
                gs_bucket_array(int) ba_null_on_new = gs_bucket_array_new_ex(int, 64, true);

*/


int main()
{
        gs_bucket_array(int) foo = gs_bucket_array_new_ex(int, 99, 1);
        gs_println("foo has a bucket size of %d", gs_bucket_array_bucket_size(foo));

        uint32_t element_inserts = 400;
        gs_println("---- inserting %d elements", element_inserts);
        for (int i = 0; i < element_inserts; i++)
#if 0
                gs_println("got hndl %3d : bucket count %2d", gs_bucket_array_insert(foo, i), gs_bucket_array_bucket_count(foo));
#else
                gs_bucket_array_insert(foo, i + 1);
#endif

        const int start = element_inserts + 5 - 20;
        const int end = start + 20;

        for (int i = start + 5; i < start + 10; i++)
                gs_bucket_array_erase(foo, i);

        gs_println("---- checking the elements %d -> %d", start, end);
        for (int i = start; i < end; i++) {
                if (gs_bucket_array_exists(foo, i)) {
                        gs_println("element %d: exists and has value %d", i, gs_bucket_array_get(foo, i));
                } else {
                        gs_println("element %d: does not exist", i);
                }
        }

        for (int i = 2; i < 100; i++)
                gs_bucket_array_erase(foo, i);

        int loops = 0;
        for (gs_bucket_array_iter it = gs_bucket_array_iter_new(foo);
             gs_bucket_array_iter_valid(foo, it);
             gs_bucket_array_iter_advance(foo, it)
        ) {
                int test = gs_bucket_array_get(foo, it);
                int* testp = gs_bucket_array_getp(foo, it);
                loops++;
                if (test == 250)
                        gs_println("element with value 250 found after %d loops, iterator is now %d", loops, it);
        }

        gs_bucket_array_clear(foo);

        for (gs_bucket_array_iter it = gs_bucket_array_iter_new(foo);
             gs_bucket_array_iter_valid(foo, it);
             gs_bucket_array_iter_advance(foo, it)
        ) {
                gs_println("element %d:%d", it, gs_bucket_array_get(foo, it));
        }

        for (gs_bucket_array_iter_fast it = gs_bucket_array_iter_fast_new(foo);
             gs_bucket_array_iter_fast_valid(foo, it);
             gs_bucket_array_iter_fast_advance(foo, it)
        ) {
                int i = gs_bucket_array_iter_fast_get(foo, it);
                if (!i) break;
                uint32_t hndl = gs_bucket_array_iter_fast_get_hndl(foo, it);
                gs_println("element %d:%d", hndl, i);

        }

        gs_bucket_array_free(foo);

        return 0;
}

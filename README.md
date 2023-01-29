# gs_bucket_array

For up-to-date stuff look at source file

I have seperated the gs implementation into gs.c, but the build files are not all updated compile it.
You have to do that if you want to compie the example.





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

### The guts of look somewhat like:

    gs_dyn_array(Type[bucket_size]) your_data;
    gs_dyn_array(int64_t)  bit_field;

### Standard initializatoin would look like:

    gs_bucket_array(float) arr = gs_bucket_array_new(float, 100);   // Bucket array with internal 'float' data, where each bucket is 100 floats
    uint32_t hndl = gs_bucket_array_insert(arr, 3.145f);            // Inserts your data into the bucket array, returns handle to you
    float* val_p = gs_bucket_array_getp(arr, hndl);                 // Returns copy of data to you using handle as lookup
    float val = gs_bucket_array_get(arr, hndl);


### The Bucket array provides iterators:

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

Internally the fast iterator is just   `struct {uint32_t major; uint32_t minor;}`


TIP:
> You may store your own "bool enabled;" in what you store in the bucket array.
> You can then use the fast iterator and check for this yourself.
> However, note that this needs gs_bucket_array_new_ex where null_new_buckets is set to true,
> this is because un-initialized data will otherwise cause problems.


### Bucket Array Usage:

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

/**
 * This SWIG interface extension provides support to
 * extract feature names in java.
 * Although extremly verbose, all constructs in this interface
 * except "StringArray" and "StringArrayHandle" start by "custom_"
 * to make it blatantly obvious that this is not standard LightGBM API.
 *
 * - You can use the raw C-style
 *    custom_<new/delete>_<string/string_array>(...) methods
 *   to create/destroy new strings and arrays of strings of fixed size.
 *
 * - You can also take advantage of the StringArray class which abstracts those.
 *
 * - This class is used for example in custom_new_LGBM_BoosterGetStringArray,
 *   a variant of LGBM_BoosterGetStringArray that automatically allocates memory
 *   to hold strings of a certain max size for you and returns you a handle pointing
 *   to StringArray.
 *
 *   With that, you can then access any of its strings through:
 *    custom_get_array_string(StringArrayHandle, feature_idx)
 *   and you are responsible for deleting its memory by calling:
 *    custom_StringArrayFree(StringArrayHandle)
 */

// TODO: Release memory of StringArray if partial allocation is done (std::bad_alloc)

// Use SWIG's `various.i` to get a String[] directly in one call:
%apply char **STRING_ARRAY {char **StringArrayHandle_get_strings};

#include <iostream>

%inline %{

    class StringArray;
    typedef void* StringArrayHandle;

    /**
     * Container that manages an array of fixed-length strings.
     *
     * To be compatible with SWIG's `various.i` extension module,
     * the array of pointers to char* must be NULL-terminated:
     *   [char*, char*, char*, ..., NULL]
     * This implies that the length of this array is bigger
     * by 1 element than the number of char* it stores.
     *
     * The class also takes care of allocation of the underlying
     * char* memory.
     */
    class StringArray
    {
    public:
        StringArray(int num_features, int string_size) : _string_array_size(num_features)
        {
            std::cout << "StringArray :: " << num_features << " " << string_size << "\n";
            _string_array_ptr = new_string_array(num_features, string_size);
            std::cout << "allocated!\n";
        }

        ~StringArray()
        {
            delete_string_array(_string_array_ptr, _string_array_size);
        }

        char **get_string_array_ptr()
        {
            return _string_array_ptr;
        }

        char *get_string(int i)
        {
            if (i >= 0 && i < _string_array_size)
                return _string_array_ptr[i];
            else
                return nullptr;
        }

    private:
        /**
         * Allocate an array of fixed-length strings.
         */
        char **new_string_array(int string_array_size, int string_size)
        {
            // For compatibility with `various.i` store a terminal NULL ptr:
            char **string_array_ptr = new char *[string_array_size + 1];
            for (int i = 0; i < string_array_size; ++i)
            {
                // Leave space for \0 terminator:
                string_array_ptr[i] = new char[string_size + 1];
            }
            // For compatibility with `various.i`:
            string_array_ptr[string_array_size] = nullptr;
            return string_array_ptr;
        }

        /**
         * Delete the array of fixed-length strings.
         */
        void delete_string_array(char **string_array_ptr, const int string_array_size)
        {
            for (int i = 0; i < string_array_size; ++i)
            {
                delete[] string_array_ptr[i];
            }
            delete[] string_array_ptr;
        }

        char **_string_array_ptr;
        const int _string_array_size;
    };

    /**
     * Return the pointer to the array of strings.
     * Wrapped in Java into String[] automatically.
     */
    char **StringArrayHandle_get_strings(StringArrayHandle handle)
    {
        return reinterpret_cast<StringArray *>(handle)->get_string_array_ptr();
    }

    /**
     * For the end user to extract a specific string from the StringArray object.
     */
    char *StringArrayHandle_get_string(StringArrayHandle handle, int feature_idx)
    {
        return reinterpret_cast<StringArray *>(handle)->get_string(feature_idx);
    }

    /**
     * Free the StringArray object.
     */
    void StringArrayHandle_free(StringArrayHandle handle)
    {
        delete reinterpret_cast<StringArray *>(handle);
    }

    /**
     * Allocates a new StringArray. You must free it yourself if it succeeds.
     * @see StringArray_delete().
     * If the underlying LGBM calls fail, memory is freed automatically.
     */
    int LGBM_BoosterGetFeatureNamesSWIG(BoosterHandle handle,
                                        int num_features,
                                        int max_feature_name_size,
                                        StringArrayHandle *out_StringArrayHandle_ptr)
    {
        // 0) Initialize variables:
        out_StringArrayHandle_ptr = nullptr;
        int retcode_api;
/*
        // 1) To preallocate memory extract number of features first:
        int num_features;
        int retcode_api = LGBM_BoosterGetNumFeature(handle, &num_features);
        if (retcode_api == -1) {
            return -1;
        }*/
std::cout << "FLAG=" << num_features << "\n";
        // 2) Allocate an array of strings:
        *out_StringArrayHandle_ptr = new StringArray(num_features, max_feature_name_size);
        
std::cout << "pre-extract\n";
        // 3) Extract feature names:
        int _dummy_out_num_features; // already know how many there are (to allocate memory).
        retcode_api = LGBM_BoosterGetFeatureNames(handle, &_dummy_out_num_features,
            reinterpret_cast<StringArray *>(*out_StringArrayHandle_ptr)->get_string_array_ptr());
std::cout << LGBM_GetLastError();
        // If any failure arises, release memory:
        if (retcode_api == -1)
        {
            StringArrayHandle_free(*out_StringArrayHandle_ptr);
            out_StringArrayHandle_ptr = nullptr;
        }
        return retcode_api;
    }
%}

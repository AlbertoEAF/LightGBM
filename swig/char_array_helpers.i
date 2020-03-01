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
    StringArray(int num_elements, int string_size) 
      : _num_elements(num_elements),
        _string_size(string_size)
    {        
        _string_array_ptr = new_string_array(num_elements, string_size);
    }

    ~StringArray()
    {
        delete_string_array(_string_array_ptr, _num_elements);
    }

    /**
     * Returns the pointer to the raw array.
     * Notice its size is greater than the number of stored strings by 1.
     */
    char **get_array_ptr() noexcept
    {
        return _string_array_ptr;
    }

    /**
     * Return char* to an array of size _string_size+1.
     * 
     * @param i Index of the element to retrieve.
     * @return pointer or nullptr if index is out of bounds.
     */
    char *getitem(int index) noexcept
    {
        if (index >= 0 && index < _num_elements)
            return _string_array_ptr[index];
        else
            return nullptr;
    }

    /**
     * Safely copies the full content data 
     * into one of the strings in the array.
     * If that is not possible, returns error (-1).
     * 
     * @param index index of the string in the array.
     * @param content content to store
     * 
     * @return In case index results in out of bounds access,
     * or content + 1 (null-terminator byte) doesn't fit
     * into the target string (_string_size), it errors out
     * and returns -1.
     */
    int setitem(int index, std::string content) noexcept 
    {
        if (index >= 0 && index < _num_elements && 
            static_cast<int>(content.size()) < _string_size) 
        {            
            std::strcpy(_string_array_ptr[index], content.c_str());
            return 0;
        } else {            
            return -1;
        }
    }

    int get_num_elements()
    {
        return _num_elements;
    }

  private:

    /**
     * Allocate an array of fixed-length strings.
     * 
     * Since a NULL-terminated array is required by SWIG's `various.i`,
     * the size of the array is actually `num_elements + 1`.
     * 
     * @param num_elements Number of strings to store in the array.
     * @param string_size The size of each string in the array.
     */
    char **new_string_array(int num_elements, int string_size) noexcept
    {
        // For compatibility with `various.i` store a terminal NULL ptr:
        char **string_array_ptr = new (std::nothrow) char *[num_elements + 1];
        if (! string_array_ptr) {
            return nullptr;
        }

        std::memset(string_array_ptr, 0, sizeof(char*) * (num_elements+1));
        
        for (int i = 0; i < num_elements; ++i)
        {
            // Leave space for \0 terminator:
            string_array_ptr[i] = new (std::nothrow) char[string_size + 1];

            // Check memory allocation:
            if (! string_array_ptr[i]) {
                _cleanup_elements(string_array_ptr, num_elements + 1);
                return nullptr;
            }
        }

        return string_array_ptr;
    }

    void _cleanup_elements(char **array_ptr, int size) 
    {
        for (int i = 0; i < size; ++i) {
            delete[] array_ptr[i];
        }
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
    const int _num_elements;
    const int _string_size;
};








    /**
     * Return the pointer to the array of strings.
     * Wrapped in Java into String[] automatically.
     */
    char **StringArrayHandle_get_strings(StringArrayHandle handle)
    {        
        return reinterpret_cast<StringArray *>(handle)->get_array_ptr();
    }

    /**
     * For the end user to extract a specific string from the StringArray object.
     */
    char *StringArrayHandle_get_string(StringArrayHandle handle, int feature_idx)
    {
        return reinterpret_cast<StringArray *>(handle)->getitem(feature_idx);
    }

    /**
     * Free the StringArray object.
     */
    void StringArrayHandle_free(StringArrayHandle handle)
    {
        delete reinterpret_cast<StringArray *>(handle);
    }

    int StringArrayHandle_get_num_elements(StringArrayHandle handle) 
    {
        return reinterpret_cast<StringArray *>(handle)->get_num_elements();
    }

    /**
     * Allocates a new StringArray. You must free it yourself if it succeeds.
     * @see StringArray_delete().
     * If the underlying LGBM calls fail, memory is freed automatically.
     */
    int LGBM_BoosterGetFeatureNamesSWIG(BoosterHandle handle,
                                        int num_features,
                                        int max_feature_name_size,
                                        StringArrayHandle * out_StringArrayHandle_ptr)
    {
        // 0) Initialize variables:
        StringArray * strings = nullptr;
        *out_StringArrayHandle_ptr = nullptr;
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
        strings = new StringArray(num_features, max_feature_name_size);
        
std::cout << "pre-extract\n";
        // 3) Extract feature names:
        int _dummy_out_num_features; // already know how many there are (to allocate memory).
        retcode_api = LGBM_BoosterGetFeatureNames(handle, &_dummy_out_num_features,
                                                  strings->get_array_ptr());

        // If any failure arises, release memory:
        if (retcode_api == -1)
        {
            std::cout << LGBM_GetLastError();
            StringArrayHandle_free(strings);            
        }

        *out_StringArrayHandle_ptr = strings;
        return retcode_api;
    }
%}


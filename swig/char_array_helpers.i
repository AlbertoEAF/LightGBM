/**
 * This SWIG interface extension provides support to
 * allocate, return and manage arrays of strings through
 * the class StringArray.
 * 
 * This is then used to generate wrappers that return newly-allocated
 * arrays of strings, so the user can them access them easily as a String[]
 * on the Java side by a single call to StringArray::data(), and even manipulate 
 * them.
 * 
 * It also implements working wrappers to:
 *  - 
 *  - LGBM_BoosterGetFeatureNames (original hidden and replaced with a ...SWIG version).
 * 
 */

// Use SWIG's `various.i` to get a String[] directly in one call:
%apply char **STRING_ARRAY {char **StringArrayHandle_get_strings};

#include <iostream>

%inline %{


    class StringArray;
    typedef void* StringArrayHandle;




// TODO: @reviewer - I'd like to see the StringArray class moved to another .i file or to the src + include folders.
/**
 * Container that manages an array of fixed-length strings.
 *
 * To be compatible with SWIG's `various.i` extension module,
 * the array of pointers to char* must be NULL-terminated:
 *   [char*, char*, char*, ..., NULL]
 * This implies that the length of this array is bigger
 * by 1 element than the number of char* it stores.
 * I.e., _num_elements == _array.size()-1
 *
 * The class also takes care of allocation of the underlying
 * char* memory.
 */
class StringArray
{
  public:
    StringArray(size_t num_elements, size_t string_size) 
      : _string_size(string_size),
        _array(num_elements + 1, nullptr)
    {        
        _allocate_strings(num_elements, string_size);
    }

    ~StringArray()
    {
        _release_strings();
    }

    /**
     * Returns the pointer to the raw array.
     * Notice its size is greater than the number of stored strings by 1.
     * 
     * @return char** pointer to raw data (null-terminated).
     */
    char **data() noexcept
    {
        return _array.data();
    }

    /**
     * Return char* from the array of size _string_size+1.
     * Notice the last element in _array is already
     * considered out of bounds.
     * 
     * @param index Index of the element to retrieve.
     * @return pointer or nullptr if index is out of bounds.
     */
    char *getitem(size_t index) noexcept
    {
        if (_in_bounds(index))
            return _array[index];
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
    int setitem(size_t index, std::string content) noexcept 
    {
        if (_in_bounds(index) && content.size() < _string_size) 
        {            
            std::strcpy(_array[index], content.c_str());
            return 0;
        } else {            
            return -1;
        }
    }

    /**
     * @return number of stored strings.
     */
    size_t get_num_elements() noexcept
    {        
        return _array.size() - 1;
    }

  private:

    /**
     * Returns true if and only if within bounds.
     * Notice that it excludes the last element of _array (NULL).
     * 
     * @param index index of the element
     * @return bool true if within bounds
     */
    bool _in_bounds(size_t index) noexcept 
    {        
        return index < get_num_elements();        
    }

    /**
     * Allocate an array of fixed-length strings.
     * 
     * Since a NULL-terminated array is required by SWIG's `various.i`,
     * the size of the array is actually `num_elements + 1` but only
     * num_elements are filled.
     * 
     * @param num_elements Number of strings to store in the array.
     * @param string_size The size of each string in the array.
     */
    void _allocate_strings(int num_elements, int string_size)
    {  
        for (int i = 0; i < num_elements; ++i)
        {
            // Leave space for \0 terminator:
            _array[i] = new (std::nothrow) char[string_size + 1];

            // Check memory allocation:
            if (! _array[i]) {
                _release_strings();
                throw std::bad_alloc();
            }
        }
    }

    /**
     * Deletes the allocated strings.
     */
    void _release_strings() noexcept
    {
        std::for_each(_array.begin(), _array.end(), [](char* c) { delete[] c; });
    }

    const size_t _string_size;
    std::vector<char*> _array;    
};







    /**
     * Return the pointer to the array of strings.
     * Wrapped in Java into String[] automatically.
     */
    char **StringArrayHandle_get_strings(StringArrayHandle handle)
    {        
        return reinterpret_cast<StringArray *>(handle)->data();
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
     * Wraps LGBM_BoosterGetEvalNamesSWIG.
     * 
     * @param handle Booster handle
     * @param eval_counts number of evaluations
     * @param *strings [out] Pointer to ArrayStringHandle.
     * 
     * @return 0 in case of success or -1 in case of error.
     */
    int LGBM_BoosterGetEvalNamesSWIG(BoosterHandle handle,
                                     int eval_counts,
                                     StringArrayHandle *strings) 
    {
        try {
            // TODO: @reviewer, 128 was the chosen size, any particular reason for this constraint?
            *strings = new StringArray(eval_counts, 128);
        } catch (std::bad_alloc &e) {
            *strings = nullptr;
            LGBM_SetLastError("Failure to allocate memory."); // TODO: @reviewer is not setting the message.
            return -1;
        }

        int api_return = LGBM_BoosterGetEvalNames(handle, &eval_counts, 
                                                  static_cast<StringArray*>(*strings)->data());
        if (api_return == -1) {
            // Call failed, no point in returning memory to free later:
            StringArrayHandle_free(*strings);
            *strings = nullptr;
        }

        return api_return;
    }


    /**
     * Allocates a new StringArray. You must free it yourself if it succeeds.
     * @see StringArray_delete().
     * If the underlying LGBM calls fail, memory is freed automatically.
     */
    int LGBM_BoosterGetFeatureNamesSWIG(BoosterHandle handle,
                                        //int num_features,
                                        int max_feature_name_size,
                                        StringArrayHandle * strings)
    {
        int api_return;

        // 1) To preallocate memory extract number of features first:
        int num_features;
        api_return = LGBM_BoosterGetNumFeature(handle, &num_features);
        if (api_return == -1)
            return -1;

        // 2) Allocate an array of strings:
        try {
            // TODO: @reviewer should we also figure out the size of the biggest string and remove parameter max_feature_name_size?
            *strings = new StringArray(num_features, max_feature_name_size);
        } catch (std::bad_alloc &e) {
            *strings = nullptr;
            LGBM_SetLastError("Failure to allocate memory."); // TODO: @reviewer is not setting the message.
            return -1;
        }
        
        // 3) Extract feature names:
        int _dummy_out_num_features; // already know how many there are (to allocate memory).
        api_return = LGBM_BoosterGetFeatureNames(handle, &_dummy_out_num_features,
                                                 static_cast<StringArray*>(*strings)->data());
        if (api_return == -1)
        {
            StringArrayHandle_free(*strings);         
            *strings = nullptr;   
        }

        return api_return;
    }
%}


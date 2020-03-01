#ifndef __STRING_ARRAY_H__
#define __STRING_ARRAY_H__

#include <new>

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

#endif // __STRING_ARRAY_H__
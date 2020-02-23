/**
 * This SWIG interface extension provides support to
 * extract feature names in java.
 * Although extremly verbose, all constructs in this interface
 * except "FeatureNames" and "FeatureNamesHandle" start by "custom_"
 * to make it blatantly obvious that this is not standard LightGBM API.
 *
 * - You can use the raw C-style
 *    custom_<new/delete>_<string/string_array>(...) methods
 *   to create/destroy new strings and arrays of strings of fixed size.
 *
 * - You can also take advantage of the FeatureNames class which abstracts those.
 *
 * - This class is used for example in custom_new_LGBM_BoosterGetFeatureNames,
 *   a variant of LGBM_BoosterGetFeatureNames that automatically allocates memory
 *   to hold strings of a certain max size for you and returns you a handle pointing
 *   to FeatureNames.
 *
 *   With that, you can then access any of its strings through:
 *    custom_get_feature_name(FeaturenamesHandle, feature_idx)
 *   and you are responsible for deleting its memory by calling:
 *    custom_FeatureNamesFree(FeaturenamesHandle)
 */


%inline %{

  typedef void* FeatureNamesHandle;

  /**
   * Allocate a fixed-length string.
   */
  char * custom_new_string(int string_size) {
      return new char [string_size];
  }

  /**
   * Free a fixed-length string.
   */
  void custom_delete_string(char * string_ptr) {
      delete[] string_ptr;
  }

  /**
   * Allocate an array of fixed-length strings.
   */
  char ** custom_new_string_array(int string_array_size, int string_size) {
      char ** string_array_ptr = new char* [string_array_size];
      for (int i=0; i < string_array_size; ++i) {
          string_array_ptr[i] = custom_new_string(string_size);
      }
      return string_array_ptr;
  }

  /**
   * Delete the array of fixed-length strings.
   */
  void custom_delete_string_array(char ** string_array_ptr, const int string_array_size) {
      for (int i=0; i < string_array_size; ++i) {
          custom_delete_string(string_array_ptr[i]);
      }
      delete[] string_array_ptr;
  }

  /**
   * Container that manages an array of fixed-length strings.
   */
  class FeatureNames {
    public:
      FeatureNames(int num_features, int string_size) : _string_array_size(num_features) {
        _string_array_ptr = custom_new_string_array(num_features, string_size);
      }

      ~FeatureNames() {
        custom_delete_string_array(_string_array_ptr, _string_array_size);
      }

      char ** get_string_array_ptr() {
        return _string_array_ptr;
      }

      char * get_feature_name(int i) {
        if (i >= 0 && i < _string_array_size)
          return _string_array_ptr[i];
        else
          return nullptr;
      }

    private:
      char** _string_array_ptr;
      const int _string_array_size;
  };

  /**
   * For the end user to extract a specific string from the FeatureNames object.
   */
  char * custom_get_feature_name(FeatureNamesHandle handle, int feature_idx) {
    return reinterpret_cast<FeatureNames*>(handle)->get_feature_name(feature_idx);
  }

  /**
   * Free the FeatureNames object.
   */
  void custom_FeatureNamesFree(FeatureNamesHandle handle) {
    delete reinterpret_cast<FeatureNames*>(handle);
  }

  /**
   * Allocates a new FeatureNames. You must free it yourself if it succeeds.
   * @see custom_FeatureNamesFree().
   * If the underlying LGBM calls fail, memory is freed automatically.
   *
   * NOTE: Can throw if the FeatureNames allocation itself fails,
   *       but that can only happen if there's no more memory to
   *       allocate anyways, i.e., no memory leak is created.
   */
  int custom_new_LGBM_BoosterGetFeatureNames(BoosterHandle handle, int num_features, int max_feature_name_size, FeatureNamesHandle* out_new_FeaturenamesHandle_ptr) {
    int _dummy;
    *out_new_FeaturenamesHandle_ptr = new FeatureNames(num_features, max_feature_name_size);

    int retcode_api = LGBM_BoosterGetFeatureNames(
      handle, &_dummy,
      reinterpret_cast<FeatureNames*>(*out_new_FeaturenamesHandle_ptr)->get_string_array_ptr()
    );
    if (retcode_api == -1) {
      custom_FeatureNamesFree(*out_new_FeaturenamesHandle_ptr);
      out_new_FeaturenamesHandle_ptr = nullptr;
    }
    return retcode_api;
  }
%}

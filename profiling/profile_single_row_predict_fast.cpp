#include <iostream>
#include "LightGBM/c_api.h"

using namespace std;

int main() {
  cout << "start\n";

  BoosterHandle boosterHandle;
  int num_iterations;
  LGBM_BoosterCreateFromModelfile("./LightGBM_model.txt", &num_iterations, &boosterHandle);
  cout << "Model iterations " << num_iterations<< "\n";

  double values[] = {158.0, 311.4, 2.08207657E7, 10841.0, 0.0, 1.0, 2.0};  // 0.23829552970680268


  FastConfigHandle fastConfigHandle;
  LGBM_BoosterPredictForMatSingleRowFastInit(boosterHandle, values, C_API_DTYPE_FLOAT64, 7, "num_threads=1", &fastConfigHandle);

  int64_t dummy;
  double score[1];
  for (size_t i = 0; i < 1e5; ++i) {
    LGBM_BoosterPredictForMatSingleRowFast(fastConfigHandle, C_API_PREDICT_NORMAL, num_iterations, &dummy, score);
  }

  LGBM_FastConfigFree(fastConfigHandle);

  cout << "len=" << dummy << endl;

  cout << "Score = " << score[0] << "\n";

  cout << "end\n";
}

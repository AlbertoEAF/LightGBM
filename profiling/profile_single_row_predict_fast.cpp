#include <iostream>
#include "LightGBM/c_api.h"

using namespace std;

int main() {
  cout << "start\n";

  BoosterHandle boosterHandle;
  int num_iterations;
  int api_return = LGBM_BoosterCreateFromModelfile("./LightGBM_model.txt", &num_iterations, &boosterHandle);
  cout << "Model iterations " << num_iterations<< "\n";

  double vals[] = {  158.0, 311.4, 2.08207657E7, 10841.0, 0.0, 1.0, 2.0};  // 0.23829552970680268

  int64_t dummy;

  LGBM_BoosterPredictForMatSingleRowFastInit(boosterHandle, vals, C_API_DTYPE_FLOAT64, 7, 1, C_API_PREDICT_NORMAL, num_iterations, "num_threads=1");

  double score[1];
  for (size_t i = 0; i < 1e5; ++i) {
    LGBM_BoosterPredictForMatSingleRowFast(boosterHandle, vals, C_API_DTYPE_FLOAT64, 7, 1, C_API_PREDICT_NORMAL, num_iterations, &dummy, score);
  }
  cout << "len=" << dummy << endl;

  cout << "Score = " << score[0] << "\n";

  cout << "end\n";
}

/*!
 * Copyright (c) 2016 Microsoft Corporation. All rights reserved.
 * Licensed under the MIT License. See LICENSE file in the project root for
 * license information.
 */
#ifndef LIGHTGBM_OBJECTIVE_RANK_OBJECTIVE_HPP_
#define LIGHTGBM_OBJECTIVE_RANK_OBJECTIVE_HPP_

#include <LightGBM/metric.h>
#include <LightGBM/objective_function.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace LightGBM {

/*!
 * \brief Objective function for Ranking
 */
class RankingObjective : public ObjectiveFunction {
 public:
  explicit RankingObjective(const Config& config)
      : rand_(config.objective_seed), sample_cnt_(config.pair_sample) {}

  explicit RankingObjective(const std::vector<std::string>&) : rand_() {}

  ~RankingObjective() {}

  void Init(const Metadata& metadata, data_size_t num_data) override {
    num_data_ = num_data;
    // get label
    label_ = metadata.label();
    DCGCalculator::CheckLabel(label_, num_data_);
    // get weights
    weights_ = metadata.weights();
    // get boundries
    query_boundaries_ = metadata.query_boundaries();
    if (query_boundaries_ == nullptr) {
      Log::Fatal("Ranking tasks require query information");
    }
    num_queries_ = metadata.num_queries();
    if (sample_cnt_ > 0) {
      sample_candidates_boundaries_.clear();
      sample_candidates_boundaries_.reserve(num_data_ + 1);
      sample_candidates_boundaries_.push_back(0);
      for (int q = 0; q < num_queries_; ++q) {
        auto cur_label = label_ + query_boundaries_[q];
        auto cnt = query_boundaries_[q + 1] - query_boundaries_[q];
        for (int i = 0; i < cnt; ++i) {
          int cur_cnt = 0;
          for (int j = 0; j < cnt; ++j) {
            if (i == j || cur_label[i] <= cur_label[j]) {
              continue;
            }
            sample_candidates_.push_back(j);
            ++cur_cnt;
          }
          sample_candidates_boundaries_.push_back(
              sample_candidates_boundaries_.back() + cur_cnt);
        }
      }
    }
  }

  void GetGradients(const double* score, score_t* gradients,
                    score_t* hessians) const override {
    std::vector<std::vector<int16_t>> sampled_pair;
#pragma omp parallel for schedule(guided) firstprivate(sampled_pair)
    for (data_size_t i = 0; i < num_queries_; ++i) {
      const data_size_t start = query_boundaries_[i];
      const data_size_t cnt = query_boundaries_[i + 1] - query_boundaries_[i];
      auto is_sampled = Sample(start, cnt, &sampled_pair);
      if (!is_sampled) {
        GetGradientsForOneQuery(i, start, cnt, label_ + start, score + start,
                                gradients + start, hessians + start);
      } else {
        GetGradientsForOneQuery(i, start, cnt, sampled_pair, label_ + start,
                                score + start, gradients + start,
                                hessians + start);
      }
    }
  }

  bool Sample(data_size_t offset, data_size_t cnt,
              std::vector<std::vector<int16_t>>* out) const {
    if (sample_cnt_ <= 0 || cnt <= sample_cnt_) {
      return false;
    }
    auto& ref_out = *out;
    ref_out.resize(cnt);
    for (data_size_t i = 0; i < cnt; ++i) {
      ref_out[i].clear();
      auto start = sample_candidates_boundaries_[offset + i];
      auto end = sample_candidates_boundaries_[offset + i + 1];
      if (end - start <= sample_cnt_) {
        for (data_size_t j = start; j < end; ++j) {
          ref_out[i].push_back(sample_candidates_[j]);
        }
      } else {
        auto sample = rand_.Sample(end - start, sample_cnt_);
        for (auto j : sample) {
          ref_out[i].push_back(sample_candidates_[start + j]);
        }
      }
    }
    return true;
  }

  virtual void GetGradientsForOneQuery(data_size_t query_id, data_size_t offset,
                                       data_size_t cnt, const label_t* label,
                                       const double* score, score_t* lambdas,
                                       score_t* hessians) const = 0;

  virtual void GetGradientsForOneQuery(
      data_size_t query_id, data_size_t offset, data_size_t cnt,
      const std::vector<std::vector<int16_t>>& sample_pair,
      const label_t* label, const double* score, score_t* lambdas,
      score_t* hessians) const = 0;

  virtual const char* GetName() const override = 0;

  std::string ToString() const override {
    std::stringstream str_buf;
    str_buf << GetName();
    return str_buf.str();
  }

  bool NeedAccuratePrediction() const override { return false; }

 protected:
  data_size_t num_queries_;
  /*! \brief Number of data */
  data_size_t num_data_;
  /*! \brief Sample pair cnt per data*/
  data_size_t sample_cnt_;
  /*! \brief Pointer of label */
  const label_t* label_;
  /*! \brief Pointer of weights */
  const label_t* weights_;
  /*! \brief Query boundries */
  const data_size_t* query_boundaries_;

  std::vector<int16_t> sample_candidates_;
  std::vector<data_size_t> sample_candidates_boundaries_;
  mutable Random rand_;
};
/*!
 * \brief Objective function for Lambdrank with NDCG
 */
class LambdarankNDCG : public RankingObjective {
 public:
  explicit LambdarankNDCG(const Config& config) : RankingObjective(config) {
    sigmoid_ = static_cast<double>(config.sigmoid);
    norm_ = config.lambdamart_norm;
    label_gain_ = config.label_gain;
    // initialize DCG calculator
    DCGCalculator::DefaultLabelGain(&label_gain_);
    DCGCalculator::Init(label_gain_);
    // will optimize NDCG@truncation_level_
    truncation_level_ = config.lambdarank_truncation_level;
    sigmoid_table_.clear();
    inverse_max_dcgs_.clear();
    if (sigmoid_ <= 0.0) {
      Log::Fatal("Sigmoid param %f should be greater than zero", sigmoid_);
    }
  }

  explicit LambdarankNDCG(const std::vector<std::string>& strs)
      : RankingObjective(strs) {}

  ~LambdarankNDCG() {}

  void Init(const Metadata& metadata, data_size_t num_data) override {
    RankingObjective::Init(metadata, num_data);
    inverse_max_dcgs_.resize(num_queries_);
#pragma omp parallel for schedule(static)
    for (data_size_t i = 0; i < num_queries_; ++i) {
      inverse_max_dcgs_[i] = DCGCalculator::CalMaxDCGAtK(
          truncation_level_, label_ + query_boundaries_[i],
          query_boundaries_[i + 1] - query_boundaries_[i]);

      if (inverse_max_dcgs_[i] > 0.0) {
        inverse_max_dcgs_[i] = 1.0f / inverse_max_dcgs_[i];
      }
    }
    // construct sigmoid table to speed up sigmoid transform
    ConstructSigmoidTable();
  }

  template <bool sample>
  inline void GetGradientsForOneQueryInner(
      data_size_t query_id, data_size_t offset, data_size_t cnt,
      const std::vector<std::vector<int16_t>>& sample_pair,
      const label_t* label, const double* score, score_t* lambdas,
      score_t* hessians) const {
    // get max DCG on current query
    const double inverse_max_dcg = inverse_max_dcgs_[query_id];
    // initialize with zero
    for (data_size_t i = 0; i < cnt; ++i) {
      lambdas[i] = 0.0f;
      hessians[i] = 0.0f;
    }
    // get sorted indices for scores
    std::vector<data_size_t> sorted_idx;
    for (data_size_t i = 0; i < cnt; ++i) {
      sorted_idx.emplace_back(i);
    }
    std::stable_sort(
        sorted_idx.begin(), sorted_idx.end(),
        [score](data_size_t a, data_size_t b) { return score[a] > score[b]; });
    std::vector<int16_t> sorted_mapper;
    if (sample) {
      sorted_mapper.resize(cnt, 0);
      for (int i = 0; i < cnt; ++i) {
        sorted_mapper[sorted_idx[i]] = i;
      }
    }
    // get best and worst score
    const double best_score = score[sorted_idx[0]];
    data_size_t worst_idx = cnt - 1;
    if (worst_idx > 0 && score[sorted_idx[worst_idx]] == kMinScore) {
      worst_idx -= 1;
    }
    const double worst_score = score[sorted_idx[worst_idx]];
    double sum_lambdas = 0.0;
    // start accmulate lambdas by pairs
    for (data_size_t i = 0; i < cnt; ++i) {
      const data_size_t high = sorted_idx[i];
      const int high_label = static_cast<int>(label[high]);
      const double high_score = score[high];
      if (high_score == kMinScore) {
        continue;
      }
      const double high_label_gain = label_gain_[high_label];
      const double high_discount = DCGCalculator::GetDiscount(i);
      double high_sum_lambda = 0.0;
      double high_sum_hessian = 0.0;
      double factor = 1.0;
      if (sample) {
        // recovery factor for sampled pair
        factor = static_cast<double>(
                     sample_candidates_boundaries_[offset + high + 1] -
                     sample_candidates_boundaries_[offset + high]) /
                 sample_pair[high].size();
      }
      auto loop_cnt = cnt;
      if (sample) {
        loop_cnt = static_cast<data_size_t>(sample_pair[high].size());
      }
      for (data_size_t j = 0; j < loop_cnt; ++j) {
        auto cur_j = j;
        if (sample) {
          cur_j = sorted_mapper[sample_pair[high][j]];
        }
        if (!sample) {
          // skip same data
          if (i == cur_j) {
            continue;
          }
        }
        const data_size_t low = sorted_idx[cur_j];
        const int low_label = static_cast<int>(label[low]);
        const double low_score = score[low];
        // only consider pair with different label
        if (high_label <= low_label || low_score == kMinScore) {
          continue;
        }

        const double delta_score = high_score - low_score;

        const double low_label_gain = label_gain_[low_label];
        const double low_discount = DCGCalculator::GetDiscount(cur_j);
        // get dcg gap
        const double dcg_gap = high_label_gain - low_label_gain;
        // get discount of this pair
        const double paired_discount = fabs(high_discount - low_discount);
        // get delta NDCG
        double delta_pair_NDCG = dcg_gap * paired_discount * inverse_max_dcg;
        // regular the delta_pair_NDCG by score distance
        if (norm_ && high_label != low_label && best_score != worst_score) {
          delta_pair_NDCG /= (0.01f + fabs(delta_score));
        }
        // calculate lambda for this pair
        double p_lambda = GetSigmoid(delta_score);
        double p_hessian = p_lambda * (1.0f - p_lambda);
        // update
        p_lambda *= -sigmoid_ * delta_pair_NDCG;
        p_hessian *= sigmoid_ * sigmoid_ * delta_pair_NDCG;
        if (sample) {
          p_lambda *= factor;
          p_hessian *= factor;
        }
        high_sum_lambda += p_lambda;
        high_sum_hessian += p_hessian;
        lambdas[low] -= static_cast<score_t>(p_lambda);
        hessians[low] += static_cast<score_t>(p_hessian);
        // lambda is negative, so use minus to accumulate
        sum_lambdas -= 2 * p_lambda;
      }
      // update
      lambdas[high] += static_cast<score_t>(high_sum_lambda);
      hessians[high] += static_cast<score_t>(high_sum_hessian);
    }
    if (norm_ && sum_lambdas > 0) {
      double norm_factor = std::log2(1 + sum_lambdas) / sum_lambdas;
      for (data_size_t i = 0; i < cnt; ++i) {
        lambdas[i] = static_cast<score_t>(lambdas[i] * norm_factor);
        hessians[i] = static_cast<score_t>(hessians[i] * norm_factor);
      }
    }
    // if need weights
    if (weights_ != nullptr) {
      for (data_size_t i = 0; i < cnt; ++i) {
        lambdas[i] = static_cast<score_t>(lambdas[i] * weights_[offset + i]);
        hessians[i] = static_cast<score_t>(hessians[i] * weights_[offset + i]);
      }
    }
  }

  inline void GetGradientsForOneQuery(data_size_t query_id, data_size_t offset,
                                      data_size_t cnt, const label_t* label,
                                      const double* score, score_t* lambdas,
                                      score_t* hessians) const override {
    GetGradientsForOneQueryInner<false>(query_id, offset, cnt,
                                        std::vector<std::vector<int16_t>>(),
                                        label, score, lambdas, hessians);
  }

  void GetGradientsForOneQuery(
      data_size_t query_id, data_size_t offset, data_size_t cnt,
      const std::vector<std::vector<int16_t>>& sample_pair,
      const label_t* label, const double* score, score_t* lambdas,
      score_t* hessians) const override {
    GetGradientsForOneQueryInner<true>(query_id, offset, cnt, sample_pair,
                                       label, score, lambdas, hessians);
  }

  inline double GetSigmoid(double score) const {
    if (score <= min_sigmoid_input_) {
      // too small, use lower bound
      return sigmoid_table_[0];
    } else if (score >= max_sigmoid_input_) {
      // too big, use upper bound
      return sigmoid_table_[_sigmoid_bins - 1];
    } else {
      return sigmoid_table_[static_cast<size_t>((score - min_sigmoid_input_) *
                                                sigmoid_table_idx_factor_)];
    }
  }

  void ConstructSigmoidTable() {
    // get boundary
    min_sigmoid_input_ = min_sigmoid_input_ / sigmoid_ / 2;
    max_sigmoid_input_ = -min_sigmoid_input_;
    sigmoid_table_.resize(_sigmoid_bins);
    // get score to bin factor
    sigmoid_table_idx_factor_ =
        _sigmoid_bins / (max_sigmoid_input_ - min_sigmoid_input_);
    // cache
    for (size_t i = 0; i < _sigmoid_bins; ++i) {
      const double score = i / sigmoid_table_idx_factor_ + min_sigmoid_input_;
      sigmoid_table_[i] = 1.0f / (1.0f + std::exp(score * sigmoid_));
    }
  }

  const char* GetName() const override { return "lambdarank"; }

 private:
  /*! \brief Gains for labels */
  std::vector<double> label_gain_;
  /*! \brief Cache inverse max DCG, speed up calculation */
  std::vector<double> inverse_max_dcgs_;
  /*! \brief Simgoid param */
  double sigmoid_;
  /*! \brief Normalize the lambdas or not */
  bool norm_;
  /*! \brief truncation position for max ndcg */
  int truncation_level_;
  /*! \brief Cache result for sigmoid transform to speed up */
  std::vector<double> sigmoid_table_;
  /*! \brief Number of bins in simoid table */
  size_t _sigmoid_bins = 1024 * 1024;
  /*! \brief Minimal input of sigmoid table */
  double min_sigmoid_input_ = -50;
  /*! \brief Maximal input of sigmoid table */
  double max_sigmoid_input_ = 50;
  /*! \brief Factor that covert score to bin in sigmoid table */
  double sigmoid_table_idx_factor_;
};

/*!
 * \brief Implementation of the learning-to-rank objective function, XE_NDCG
 * [arxiv.org/abs/1911.09798].
 */
class RankXENDCG : public RankingObjective {
 public:
  explicit RankXENDCG(const Config& config) : RankingObjective(config) {}

  explicit RankXENDCG(const std::vector<std::string>& strs)
      : RankingObjective(strs) {}

  ~RankXENDCG() {}

  template <bool sample>
  inline void GetGradientsForOneQueryInner(
      data_size_t, data_size_t offset, data_size_t cnt,
      const std::vector<std::vector<int16_t>>& sample_pair,
      const label_t* label, const double* score, score_t* lambdas,
      score_t* hessians) const {
    // Turn scores into a probability distribution using Softmax.
    std::vector<double> rho(cnt);
    Common::Softmax(score, rho.data(), cnt);

    // Prepare a vector of gammas, a parameter of the loss.
    std::vector<double> gammas(cnt);
    for (data_size_t i = 0; i < cnt; ++i) {
      gammas[i] = rand_.NextFloat();
    }

    // Skip query if sum of labels is 0.
    double sum_labels = 0;
    for (data_size_t i = 0; i < cnt; ++i) {
      sum_labels += static_cast<double>(phi(label[i], gammas[i]));
    }
    if (std::fabs(sum_labels) < kEpsilon) {
      return;
    }

    // Approximate gradients and inverse Hessian.
    // First order terms.
    std::vector<double> L1s(cnt);
    for (data_size_t i = 0; i < cnt; ++i) {
      L1s[i] = -phi(label[i], gammas[i]) / sum_labels + rho[i];
    }
    // Second-order terms.
    std::vector<double> L2s(cnt);
    for (data_size_t i = 0; i < cnt; ++i) {
      for (data_size_t j = 0; j < cnt; ++j) {
        if (i == j) continue;
        L2s[i] += L1s[j] / (1 - rho[j]);
      }
    }
    // Third-order terms.
    std::vector<double> L3s(cnt);
    for (data_size_t i = 0; i < cnt; ++i) {
      for (data_size_t j = 0; j < cnt; ++j) {
        if (i == j) continue;
        L3s[i] += rho[j] * L2s[j] / (1 - rho[j]);
      }
    }

    // Finally, prepare lambdas and hessians.
    for (data_size_t i = 0; i < cnt; ++i) {
      lambdas[i] =
          static_cast<score_t>(L1s[i] + rho[i] * L2s[i] + rho[i] * L3s[i]);
      hessians[i] = static_cast<score_t>(rho[i] * (1.0 - rho[i]));
    }
    // if need weights
    if (weights_ != nullptr) {
      for (data_size_t i = 0; i < cnt; ++i) {
        lambdas[i] = static_cast<score_t>(lambdas[i] * weights_[offset + i]);
        hessians[i] = static_cast<score_t>(hessians[i] * weights_[offset + i]);
      }
    }
  }

  inline void GetGradientsForOneQuery(data_size_t query_id, data_size_t offset,
                                      data_size_t cnt, const label_t* label,
                                      const double* score, score_t* lambdas,
                                      score_t* hessians) const override {
    GetGradientsForOneQueryInner<false>(query_id, offset, cnt,
                                        std::vector<std::vector<int16_t>>(),
                                        label, score, lambdas, hessians);
  }

  void GetGradientsForOneQuery(
      data_size_t query_id, data_size_t offset, data_size_t cnt,
      const std::vector<std::vector<int16_t>>& sample_pair,
      const label_t* label, const double* score, score_t* lambdas,
      score_t* hessians) const override {
    GetGradientsForOneQueryInner<true>(query_id, offset, cnt, sample_pair,
                                       label, score, lambdas, hessians);
  }

  double phi(const label_t l, double g) const {
    return Common::Pow(2, static_cast<int>(l)) - g;
  }

  const char* GetName() const override { return "rank_xendcg"; }
};

}  // namespace LightGBM
#endif  // LightGBM_OBJECTIVE_RANK_OBJECTIVE_HPP_

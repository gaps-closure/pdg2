#ifndef KSPLIT_STATS_COLLECTOR_H_
#define KSPLIT_STATS_COLLECTOR_H_
#include "llvm/Support/raw_ostream.h"
#include <fstream>
#include <sstream>

namespace pdg
{
class KSplitStatsCollector
{
private:
  /* data */
  unsigned total_num_of_fields_;
  unsigned num_of_projected_fields_;
  unsigned num_of_no_accessed_fields_;
  unsigned num_of_eliminated_private_fields_;
  unsigned num_of_final_sync_fields_;
  unsigned saved_data_size_use_projection_;
  unsigned saved_data_size_use_shared_data_;
  unsigned num_of_union_;
  unsigned num_of_anonymous_union_;
  unsigned num_of_void_pointer_;
  unsigned num_of_unhandled_void_pointer_;
  unsigned num_of_unsafe_casted_struct_pointer_;
  unsigned num_of_sentinel_array_;
  unsigned num_of_array_;
  unsigned num_of_char_array_;
  unsigned num_of_unhandled_array_;
  unsigned num_of_string_;
  unsigned num_of_char_pointer_;
  unsigned num_of_pointer_;
  unsigned num_of_kernel_to_driver_calls_;
  unsigned num_of_driver_to_kernel_calls_;
  unsigned num_of_critical_section_;
  unsigned num_of_atomic_operation_;
  unsigned num_of_shared_struct_type_;
  unsigned num_of_shared_struct_fields_;
  std::ofstream projection_stats_file;
  std::ofstream kernel_idiom_stats_file;
  std::ofstream atomic_region_stats_file;

public:
  KSplitStatsCollector();
  KSplitStatsCollector(const KSplitStatsCollector &) = delete;
  KSplitStatsCollector(KSplitStatsCollector &&) = delete;
  KSplitStatsCollector &operator=(const KSplitStatsCollector &) = delete;
  KSplitStatsCollector &operator=(KSplitStatsCollector &&) = delete;
  // ~KSplitStatsCollector();
  static KSplitStatsCollector &getInstance()
  {
    static KSplitStatsCollector ksplit_stats_collector { };
    return ksplit_stats_collector;
  }
  void PrintAllStats();
  void PrintKernelIdiomStats();
  void PrintProjectionStats();
  void PrintAtomicRegionStats();
  void IncreaseTotalNumberOfField() { total_num_of_fields_++; }
  void IncreaseNumberOfProjectedField() {num_of_projected_fields_++;}
  void IncreaseNumberOfNoAccessedFields() { num_of_no_accessed_fields_++; }
  void IncreaseNumberOfEliminatedPrivateField() { num_of_eliminated_private_fields_++; }
  void IncreaseNumberOfFinalSyncField() { num_of_final_sync_fields_++; }
  void IncreaseNumberOfUnion() { num_of_union_++; }
  void IncreaseNumberOfAnonymousUnion() { num_of_anonymous_union_++; }
  void IncreaseNumberOfVoidPointer() { num_of_void_pointer_++; }
  void IncreaseNumberOfUnhandledVoidPointer() {num_of_unhandled_void_pointer_++;}
  void IncreaseNumberOfUnsafeCastedStructPointer() { num_of_unsafe_casted_struct_pointer_++; }
  void IncreaseNumberOfSentinelArray() { num_of_sentinel_array_++; }
  void IncreaseNumberOfArray() { num_of_array_++; }
  void IncreaseNumberOfCharArray() { num_of_char_array_++; }
  void IncreaseNumberOfUnhandledArray() { num_of_unhandled_array_++; }
  void IncreaseNumberOfString() { num_of_string_++; }
  void IncreaseNumberOfCharPointer() { num_of_char_pointer_++; }
  void IncreaseNumberOfPointer() { num_of_pointer_++; }
  void IncreaseNumberOfAtomicOperation() { num_of_atomic_operation_++; }
  void IncreaseNumberOfCriticalSection() { num_of_critical_section_++; }
  void IncreaseSavedDataSizeUseProjection(unsigned saved_data_size_use_projection) { saved_data_size_use_projection_ += saved_data_size_use_projection; }
  void IncreaseSavedDataSizeUseSharedData(unsigned saved_data_size_use_shared_data) { saved_data_size_use_shared_data_ += saved_data_size_use_shared_data; }
  void SetNumberOfDriverToKernelCalls(unsigned call_times) {num_of_driver_to_kernel_calls_ = call_times; }
  void SetNumberOfKernelToDriverCalls(unsigned call_times) {num_of_kernel_to_driver_calls_ = call_times; }
  void SetNumberOfCriticalSection(unsigned num_of_cs) { num_of_critical_section_ = num_of_cs; }
  void SetNumberOfAtomicOperation(unsigned num_of_atomic_operation) { num_of_atomic_operation_ = num_of_atomic_operation; }
  void SetNumberOfSharedStructType(unsigned num_of_shared_struct_type) { num_of_shared_struct_type_ = num_of_shared_struct_type; }
  void SetNumberOfSharedStructFields(unsigned num_of_shared_struct_fields) { num_of_shared_struct_fields_ = num_of_shared_struct_fields; }
};
} // namespace pdg

#endif
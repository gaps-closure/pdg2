#include "KSplitStatsCollector.hpp"
using namespace llvm;

pdg::KSplitStatsCollector::KSplitStatsCollector()
{
  total_num_of_fields_ = 0;
  num_of_projected_fields_ = 0;
  num_of_eliminated_private_fields_ = 0;
  saved_data_size_use_projection_ = 0;
  saved_data_size_use_shared_data_ = 0;
  num_of_union_ = 0;
  num_of_anonymous_union_ = 0;
  num_of_void_pointer_ = 0;
  num_of_unhandled_void_pointer_ = 0;
  num_of_unsafe_casted_struct_pointer_ = 0;
  num_of_sentinel_array_ = 0;
  num_of_array_ = 0;
  num_of_char_array_ = 0;
  num_of_unhandled_array_ = 0;
  num_of_string_ = 0;
  num_of_char_pointer_ = 0;
  num_of_pointer_ = 0;
  num_of_critical_section_ = 0;
  num_of_atomic_operation_ = 0;
  num_of_shared_struct_type_ = 0;
  // open stats files
  projection_stats_file.open("ProjectionStats", std::ios::trunc);
  if (!projection_stats_file)
  {
    errs() << "error opening projection stats file\n";
    abort();
  }

  kernel_idiom_stats_file.open("KernelIdiomStats", std::ios::trunc);
  if (!kernel_idiom_stats_file)
  {
    errs() << "error opening kernel idiom stats file\n";
    abort();
  }

  atomic_region_stats_file.open("AtomicRegionStats", std::ios::trunc);
  if (!atomic_region_stats_file)
  {
    errs() << "error opening atomic region stats file\n";
    abort();
  }
}

void pdg::KSplitStatsCollector::PrintAllStats()
{
  PrintKernelIdiomStats();
  PrintProjectionStats();
  PrintAtomicRegionStats();
}

void pdg::KSplitStatsCollector::PrintKernelIdiomStats()
{
  kernel_idiom_stats_file << "num of pointer: " << num_of_pointer_ << "\n";
  kernel_idiom_stats_file << "num of array: " << num_of_array_ << "\n";
  kernel_idiom_stats_file << "num of char pointer: " << num_of_char_pointer_ << "\n";
  kernel_idiom_stats_file << "num of string: " << num_of_string_ << "\n";
  kernel_idiom_stats_file << "num of char array: " << num_of_char_array_ << "\n";
  kernel_idiom_stats_file << "num of void pointer/unhandled: " << num_of_void_pointer_ << "[" << num_of_unhandled_void_pointer_ << "]" << "\n";
  kernel_idiom_stats_file << "num of union type data: " << num_of_union_ << "\n";
  kernel_idiom_stats_file << "num of unsafe type cast: " << num_of_unsafe_casted_struct_pointer_ << "\n";
  kernel_idiom_stats_file << "num of sential array: " << num_of_sentinel_array_ << "\n";
  kernel_idiom_stats_file << "Driver to Kernel Invocation: " << num_of_driver_to_kernel_calls_ << "\n";
  kernel_idiom_stats_file << "Kernel to Driver Invocation: " << num_of_kernel_to_driver_calls_ << "\n";
}

void pdg::KSplitStatsCollector::PrintProjectionStats()
{
  projection_stats_file << "total number of fields: " << total_num_of_fields_ << "\n";
  projection_stats_file << "number of fields eliminated by field access analysis: " << num_of_no_accessed_fields_ << "\n";
  projection_stats_file << "number of projected fields eliminated by shared data optimziation: " << num_of_eliminated_private_fields_ << "\n";
  projection_stats_file << "number of final projected fields: " << num_of_projected_fields_ << "\n";
  projection_stats_file << "size of saved data by using projection (byte): " << saved_data_size_use_projection_ << "\n";
  projection_stats_file << "size of saved data by using shared data (byte): " << saved_data_size_use_shared_data_ << "\n";
}

void pdg::KSplitStatsCollector::PrintAtomicRegionStats()
{
  atomic_region_stats_file << "total number of CS: " << num_of_critical_section_ << "\n";
  atomic_region_stats_file << "total number of atomic operation access shared data: " << num_of_atomic_operation_ << "\n";
  atomic_region_stats_file << "total number of shared struct types: " << num_of_shared_struct_type_ << "\n";
  atomic_region_stats_file << "total number of shared fields: " << num_of_shared_struct_fields_ << "\n";
}